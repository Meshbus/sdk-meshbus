// SPDX-License-Identifier: Apache-2.0
use crate::{archive, edk, elf::Elf, host, metadata};
use anyhow::{Context, Result, ensure};
use clap::Args;
use serde_json::Value;
use std::{
    fs,
    path::{Path, PathBuf},
    process::Command,
};
#[derive(Debug, Args)]
pub struct LlextArgs {
    #[arg(short = 'd', long)]
    pub build_dir: Option<PathBuf>,
    #[arg(short = 'o', long)]
    pub output_dir: Option<PathBuf>,
    #[arg(long)]
    pub llext_sdk: Option<PathBuf>,
    #[arg(long)]
    pub zephyr_sdk: Option<PathBuf>,
    #[arg(long)]
    pub force_edk: bool,
    pub source_dir: PathBuf,
    #[arg(trailing_var_arg = true, allow_hyphen_values = true)]
    pub cmake_args: Vec<String>,
}
/// Inputs for a package build, independent of command-line parsing.
#[derive(Debug)]
pub struct BuildRequest {
    pub tools: Option<BuildTools>,
    pub build_dir: Option<PathBuf>,
    pub output_dir: Option<PathBuf>,
    pub llext_sdk: Option<PathBuf>,
    pub zephyr_sdk: Option<PathBuf>,
    pub force_edk: bool,
    pub source_dir: PathBuf,
    pub cmake_args: Vec<String>,
}

/// Explicit host tools for managed project builds; legacy builds use PATH.
#[derive(Debug)]
pub struct BuildTools {
    pub cmake: PathBuf,
    pub ninja: PathBuf,
    pub python: PathBuf,
}

impl From<LlextArgs> for BuildRequest {
    fn from(args: LlextArgs) -> Self {
        Self {
            tools: None,
            build_dir: args.build_dir,
            output_dir: args.output_dir,
            llext_sdk: args.llext_sdk,
            zephyr_sdk: args.zephyr_sdk,
            force_edk: args.force_edk,
            source_dir: args.source_dir,
            cmake_args: args.cmake_args,
        }
    }
}

pub fn tool(sdk: &Path, name: &str) -> Result<PathBuf> {
    // SDK 1.x and SDK 0.x layout. Target libc must match the EDK's flags.
    for (directory, prefix) in [
        ("bin", "arm-zephyr-eabi"),
        ("gnu/arm-zephyr-eabi/bin", "arm-zephyr-eabi"),
        ("arm-zephyr-eabi/bin", "arm-zephyr-eabi"),
    ] {
        let path = sdk
            .join(directory)
            .join(format!("{prefix}-{name}{}", std::env::consts::EXE_SUFFIX));
        if path.is_file() {
            return Ok(path);
        }
    }
    anyhow::bail!(
        "missing GNU Arm compiler tool {name} below {}",
        sdk.display()
    )
}
fn sdk_path(args: &BuildRequest) -> Result<PathBuf> {
    if let Some(p) = &args.zephyr_sdk {
        return Ok(p.canonicalize()?);
    }
    if let Some(p) = std::env::var_os("ZEPHYR_SDK_INSTALL_DIR") {
        return Ok(PathBuf::from(p).canonicalize()?);
    }
    if let Some(build) = &args.build_dir {
        return edk::ContextData::read(build)?.toolchain();
    }
    anyhow::bail!("set --zephyr-sdk or ZEPHYR_SDK_INSTALL_DIR")
}
// argv[0] dispatch keeps custom CMake compiler commands portable without shell scripts.
pub fn internal_tool() -> Option<Result<i32>> {
    let executable = std::env::args_os().next()?;
    let name = Path::new(&executable).file_stem()?.to_str()?;
    if name == "xxd" {
        return Some((|| {
            let args = std::env::args_os().skip(1).collect::<Vec<_>>();
            ensure!(
                args.len() == 3 && args[0] == "-ip",
                "internal xxd supports -ip input output"
            );
            let bytes = fs::read(&args[1])?;
            let mut text = String::new();
            for chunk in bytes.chunks(30) {
                text.push_str(&host::hex(chunk));
                text.push('\n');
            }
            fs::write(&args[2], text)?;
            Ok(0)
        })());
    }
    let compiler = name.strip_prefix("arm-zephyr-eabi-")?;
    if !["gcc", "g++"].contains(&compiler) {
        return None;
    }
    Some((|| {
        let sdk = PathBuf::from(
            std::env::var_os("MESHBUS_REAL_TOOLCHAIN")
                .context("internal compiler wrapper has no toolchain")?,
        );
        let status = Command::new(tool(&sdk, compiler)?)
            .args(std::env::args_os().skip(1))
            .args(["-Os", "-g0", "-fno-merge-constants"])
            .status()?;
        Ok(status.code().unwrap_or(1))
    })())
}
pub fn build(args: &BuildRequest) -> Result<PathBuf> {
    ensure!(
        args.build_dir.is_some() || args.llext_sdk.is_some(),
        "pass --build-dir or --llext-sdk"
    );
    ensure!(
        !args.force_edk || args.build_dir.is_some(),
        "--force-edk requires --build-dir"
    );
    let source = args.source_dir.canonicalize()?;
    ensure!(
        source.join("CMakeLists.txt").is_file(),
        "extension CMakeLists.txt missing"
    );
    let data: Value = serde_yaml_ng::from_slice(&host::read(&source.join("llext.yaml"), 65536)?)?;
    let data = if data.get("project-schema").is_some() {
        crate::app::metadata(&data)?
    } else {
        data
    };
    let id = metadata::string(&data, "id")?;
    let output = args
        .output_dir
        .clone()
        .or_else(|| {
            args.build_dir
                .as_ref()
                .map(|b| edk::app_build(b).join("zephyr/llext"))
        })
        .context("--output-dir required without host build")?;
    fs::create_dir_all(&output)?;
    let output = output.canonicalize()?;
    let temporary = tempfile::tempdir()?;
    let extraction = tempfile::tempdir()?;
    let sdk = if let Some(root) = &args.llext_sdk {
        let root = root.canonicalize()?;
        if root.is_file() {
            archive::extract_edk(&root, extraction.path())?
        } else {
            root
        }
    } else {
        let archive = edk::create(
            args.build_dir.as_ref().unwrap(),
            temporary.path(),
            true,
            false,
        )?;
        archive::extract_edk(&archive, extraction.path())?
    };
    let manifest = edk::manifest(&sdk)?;
    if let Some(build) = &args.build_dir {
        let context = edk::ContextData::read(build)?;
        ensure!(
            manifest["target"] == context.target
                && manifest["host"]["version"] == context.version
                && manifest["interface-abi"] == serde_json::to_value(context.interface_abi()?)?,
            "EDK differs from host build"
        );
    }
    let toolchain = sdk_path(args)?;
    for name in ["gcc", "g++", "ld", "objcopy"] {
        tool(&toolchain, name)?;
    }
    let build = output
        .join("build")
        .join(source.file_name().context("source directory has no name")?);
    fs::create_dir_all(&build)?;
    let wrappers = build.join("toolchain-wrapper");
    fs::create_dir_all(&wrappers)?;
    for name in ["arm-zephyr-eabi-gcc", "arm-zephyr-eabi-g++", "xxd"] {
        let p = wrappers.join(format!("{name}{}", std::env::consts::EXE_SUFFIX));
        if p.exists() {
            fs::remove_file(&p)?;
        }
        fs::copy(std::env::current_exe()?, p)?;
    }
    let mut paths = vec![
        wrappers,
        tool(&toolchain, "gcc")?.parent().unwrap().to_path_buf(),
    ];
    paths.extend(std::env::split_paths(
        &std::env::var_os("PATH").unwrap_or_default(),
    ));
    let path = std::env::join_paths(paths)?;
    let cmake = args
        .tools
        .as_ref()
        .map_or(Path::new("cmake"), |t| t.cmake.as_path());
    let mut configure = Command::new(cmake);
    if let Some(tools) = &args.tools {
        configure.arg(format!("-DCMAKE_MAKE_PROGRAM={}", tools.ninja.display()));
        configure.arg(format!("-DPython3_EXECUTABLE={}", tools.python.display()));
    }
    configure
        .arg("-S")
        .arg(&source)
        .arg("-B")
        .arg(&build)
        .args(["-G", "Ninja"])
        .arg(format!(
            "-DLLEXT_EDK_INSTALL_DIR={}",
            sdk.to_string_lossy().replace('\\', "/")
        ))
        .args(&args.cmake_args);
    configure
        .env("PATH", &path)
        .env("MESHBUS_REAL_TOOLCHAIN", &toolchain)
        .env("LLEXT_EDK_INSTALL_DIR", &sdk)
        .env("ZEPHYR_SDK_INSTALL_DIR", &toolchain);
    host::run(&mut configure)?;
    host::run(
        Command::new(cmake)
            .arg("--build")
            .arg(&build)
            .env("PATH", &path)
            .env("MESHBUS_REAL_TOOLCHAIN", &toolchain)
            .env("ZEPHYR_SDK_INSTALL_DIR", &toolchain),
    )?;
    let candidates = host::files(&build)?
        .into_iter()
        .filter(|p| {
            p.extension().is_some_and(|e| e == "llext")
                && !["normalized.llext", "packaged.llext"]
                    .iter()
                    .any(|n| p.file_name().is_some_and(|f| f == *n))
                && !p
                    .file_name()
                    .unwrap()
                    .to_string_lossy()
                    .contains(".normalized")
        })
        .collect::<Vec<_>>();
    ensure!(
        candidates.len() == 1,
        "expected exactly one .llext build output"
    );
    let elf = &candidates[0];
    let normalized = build.join("normalized.llext");
    host::run(
        Command::new(tool(&toolchain, "ld")?)
            .args(["-r", "-o"])
            .arg(&normalized)
            .arg(elf),
    )?;
    let bytes = host::read(&normalized, 64 * 1024 * 1024)?;
    let config = sdk.join("include/zephyr/include/generated/zephyr/autoconf.h");
    let config = edk::config(&config)?;
    let parsed = Elf::parse(&bytes)?;
    let heap = parsed.heap(&config)?;
    let imports = parsed.required_imports(&bytes)?;
    let report = preflight(&data, &manifest, &config, heap, &imports)?;
    let capability_path = build.join("arduboy-capabilities.json");
    let capabilities: Value = if capability_path.is_file() {
        serde_json::from_slice(&host::read(&capability_path, 1024 * 1024)?)?
    } else {
        Value::Null
    };
    fs::write(
        output.join(format!("{id}.build.json")),
        serde_json::to_vec_pretty(
            &serde_json::json!({"schema":1,"preflight":report,"sdk":capabilities,
            "edk":manifest["edk"],"host":manifest["host"],"target":manifest["target"],
            "metadata_version":manifest["metadata-version"],"interface_abi":manifest["interface-abi"]}),
        )?,
    )?;
    eprintln!("LLEXT preflight: {report}");
    ensure!(
        report["missing_imports"].as_array().unwrap().is_empty(),
        "EDK missing imports: {}",
        report["missing_imports"]
    );
    ensure!(
        report["capacity_errors"].as_array().unwrap().is_empty(),
        "LLEXT capacity check failed: {}",
        report["capacity_errors"]
    );
    let blob = metadata::build(
        &data,
        &source,
        u32::try_from(
            manifest["metadata-version"]
                .as_u64()
                .context("invalid metadata version")?,
        )?,
        metadata::string(&manifest["host"], "version")?,
        metadata::string(&manifest, "target")?,
        heap,
        manifest["interface-abi"]
            .as_u64()
            .map(u32::try_from)
            .transpose()?,
    )?;
    let metadata = build.join("meshbus.meta");
    fs::write(&metadata, blob)?;
    let packaged = output.join(format!("{id}.mba"));
    let pending = build.join("packaged.llext");
    host::run(
        Command::new(tool(&toolchain, "objcopy")?)
            .args(["--remove-section", ".meshbus.llext.meta"])
            .arg(&normalized)
            .arg(&pending),
    )?;
    host::run(
        Command::new(tool(&toolchain, "objcopy")?)
            .arg("--add-section")
            .arg(format!(".meshbus.llext.meta={}", metadata.display()))
            .args(["--set-section-flags", ".meshbus.llext.meta=readonly"])
            .arg(&pending)
            .arg(&packaged),
    )?;
    // Drop intermediates with .llext suffix so a repeat build has one compiler output.
    fs::remove_file(normalized)?;
    fs::remove_file(pending)?;
    if build.join("arduboy-resources.json").is_file() {
        resource_collection(&build, &packaged, id)?;
    }
    Ok(packaged)
}

/// Collect the MBA and its immutable sidecar for verified, capacity-checked installation.
pub fn resource_collection(build: &Path, package: &Path, id: &str) -> Result<PathBuf> {
    let spec: Value =
        serde_json::from_slice(&host::read(&build.join("arduboy-resources.json"), 65536)?)?;
    ensure!(
        spec["schema"] == 1 && spec["identity"] == id,
        "resource identity differs from MBA"
    );
    let file = metadata::string(&spec, "file")?;
    ensure!(file == format!("{id}.abr"), "invalid sidecar filename");
    let directory = metadata::string(&spec, "directory")?;
    ensure!(
        directory.starts_with("/extra/apps/")
            && !directory.ends_with('/')
            && directory.split('/').skip(1).all(|p| !p.is_empty()
                && p != "."
                && p != ".."
                && p.bytes()
                    .all(|b| b.is_ascii_alphanumeric() || b"_-".contains(&b))),
        "invalid resource installation directory"
    );
    ensure!(
        spec["destination"] == format!("{directory}/{file}"),
        "resource destination mismatch"
    );
    let sidecar = host::read(&build.join(file), 64 * 1024 * 1024)?;
    ensure!(
        spec["length"] == sidecar.len() && spec["sha256"] == host::hash(&sidecar),
        "sidecar changed since compilation"
    );
    let mba = host::read(package, 64 * 1024 * 1024)?;
    let collection = package.parent().unwrap().join(format!("{id}.install"));
    fs::create_dir_all(&collection)?;
    fs::write(collection.join(file), &sidecar)?;
    fs::write(collection.join(format!("{id}.mba")), &mba)?;
    let files = [(file.to_owned(), sidecar), (format!("{id}.mba"), mba)]
        .into_iter()
        .map(|(name, bytes)| {
            serde_json::json!({"destination":format!("{directory}/{name}"),
            "name":name,"length":bytes.len(),"sha256":host::hash(&bytes)})
        })
        .collect::<Vec<_>>();
    let manifest = collection.join("install.json");
    host::json(
        &manifest,
        &serde_json::json!({"schema":1,"id":id,"resource":spec,"files":files}),
    )?;
    eprintln!(
        "LLEXT resource installation collection: {}",
        manifest.display()
    );
    Ok(manifest)
}
pub fn run(args: LlextArgs) -> Result<()> {
    let package = build(&args.into())?;
    host::print(
        &serde_json::json!({"report":package.with_extension("build.json"),"package":package}),
    )
}

/// Static limits are necessary checks; free runtime heap is still checked by the host.
pub fn preflight(
    data: &Value,
    manifest: &Value,
    config: &std::collections::BTreeMap<String, String>,
    heap: u32,
    imports: &std::collections::BTreeSet<String>,
) -> Result<Value> {
    let exports = manifest["exported-symbols"].as_array();
    let missing = imports
        .iter()
        .filter(|name| {
            exports.is_some_and(|values| !values.iter().any(|v| v.as_str() == Some(name.as_str())))
        })
        .collect::<Vec<_>>();
    if exports.is_none() {
        eprintln!("warning: legacy EDK has no exported-symbols inventory; imports unchecked");
    }
    let limit = |key: &str| -> Result<Option<u64>> {
        config
            .get(key)
            .map(|v| v.parse::<u64>().with_context(|| format!("invalid {key}")))
            .transpose()
    };
    let stack = data["stack-size"].as_u64().context("invalid stack-size")?;
    let dynamic_heap = u64::from(heap)
        * (100 + limit("CONFIG_MBS_LLEXT_DYNAMIC_HEAP_MARGIN_PERCENT")?.unwrap_or(0));
    let dynamic_heap = dynamic_heap.div_ceil(100);
    let mut checks = Vec::new();
    let mut errors = Vec::new();
    for (key, requested) in [
        ("CONFIG_MBS_LLEXT_APP_MAX_HEAP_SIZE", u64::from(heap)),
        ("CONFIG_MBS_LLEXT_APP_HEAP_RESERVE_SIZE", u64::from(heap)),
        ("CONFIG_MBS_LLEXT_TOTAL_HEAP_MAX_SIZE", dynamic_heap),
        ("CONFIG_MBS_DESKTOP_APP_SHARED_STACK_SIZE", stack),
    ] {
        if let Some(maximum) = limit(key)? {
            checks.push(serde_json::json!({"limit":key,"requested":requested,"maximum":maximum}));
            if requested > maximum {
                errors.push(format!("{key}: requested {requested} exceeds {maximum}"));
            }
        }
    }
    Ok(
        serde_json::json!({"imports_checked":exports.is_some(),"imports":imports,
        "missing_imports":missing,"heap":heap,"dynamic_heap":dynamic_heap,
        "stack":stack,"capacity_checks":checks,"capacity_errors":errors,
        "runtime_allocation":"not proven by static checks"}),
    )
}
