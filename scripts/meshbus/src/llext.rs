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
pub fn tool(sdk: &Path, name: &str) -> Result<PathBuf> {
    // SDK 1.x and SDK 0.x layout. Target libc must match the EDK's flags.
    for (directory, prefix) in [
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
fn sdk_path(args: &LlextArgs) -> Result<PathBuf> {
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
pub fn build(args: &LlextArgs) -> Result<PathBuf> {
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
            manifest["target"] == context.target && manifest["host"]["version"] == context.version,
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
    let mut configure = Command::new("cmake");
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
        Command::new("cmake")
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
    let heap = Elf::parse(&bytes)?.heap(&edk::config(&config)?)?;
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
    Ok(packaged)
}
pub fn run(args: LlextArgs) -> Result<()> {
    host::print(&serde_json::json!({"package":build(&args)?}))
}
