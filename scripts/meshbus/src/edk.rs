// SPDX-License-Identifier: Apache-2.0
use crate::{archive, elf::Elf, host, metadata};
use anyhow::{Context, Result, ensure};
use clap::{Args, Subcommand};
use serde_json::{Value, json};
use std::{
    collections::{BTreeMap, BTreeSet},
    fs,
    io::Write,
    path::{Path, PathBuf},
    process::{Command, Stdio},
};
const PREFIX: &str = "include/build/host";
// Accept already published EDKs as well as the per-image public output layout.
const GENERATED: [&str; 2] = [
    "/generated/meshbus/",
    "/modules/meshbus/subsys/meshbus/meshbus/",
];
const AUTOCONF: &str = "include/zephyr/include/generated/zephyr/autoconf.h";
const PRIVATE_KEY_PATH_CONFIGS: [&str; 2] = [
    "CONFIG_MCUBOOT_SIGNATURE_KEY_FILE",
    "CONFIG_MCUBOOT_ENCRYPTION_KEY_FILE",
];
const LEGACY_SHARED_PUBLIC: [&str; 2] = [
    "include/meshbus/include/zephyr/display/",
    "include/meshbus/include/zephyr/zui/",
];
const SDK_INCLUDE: &str = "include/meshbus/include/";
const LEGACY_PUBLIC: &str = "include/meshbus/include/zephyr/meshbus/";
const PUBLIC_MODULES: [&str; 19] = [
    "bluetooth",
    "channel",
    "clock",
    "contact",
    "desktop",
    "display",
    "firmware",
    "fs",
    "gnss",
    "indicator",
    "input",
    "llext",
    "management",
    "meshcore",
    "message",
    "notify",
    "power",
    "radio",
    "telemetry",
];

// A complete layout is required. Keep old archives usable without exporting
// arbitrary include/ directories or accepting stale headers from a mixed build.
fn public_roots(root: &Path) -> Result<Vec<String>> {
    let namespace = format!("{SDK_INCLUDE}meshbus/");
    let namespaced = root.join(&namespace).is_dir();
    let legacy = root.join(LEGACY_PUBLIC).is_dir();
    let flat = PUBLIC_MODULES
        .iter()
        .any(|module| root.join(SDK_INCLUDE).join(module).is_dir());
    ensure!(
        namespaced as u8 + legacy as u8 + flat as u8 <= 1,
        "mixed public SDK layouts in EDK"
    );
    let flat_shared = root.join(format!("{SDK_INCLUDE}zui")).is_dir();
    let legacy_shared = LEGACY_SHARED_PUBLIC
        .iter()
        .any(|prefix| root.join(prefix).is_dir());
    ensure!(
        !(flat_shared && legacy_shared),
        "mixed shared public SDK layouts in EDK"
    );
    let mut prefixes: Vec<String> = if flat_shared {
        ["display", "zui"]
            .into_iter()
            .map(|module| format!("{SDK_INCLUDE}{module}/"))
            .collect()
    } else {
        LEGACY_SHARED_PUBLIC
            .into_iter()
            .map(str::to_owned)
            .collect()
    };
    if legacy {
        prefixes.push(LEGACY_PUBLIC.to_owned());
    } else {
        let include = if flat { SDK_INCLUDE } else { &namespace };
        prefixes.extend(
            PUBLIC_MODULES
                .iter()
                .map(|module| format!("{include}{module}/")),
        );
    }
    for prefix in &prefixes {
        ensure!(
            root.join(prefix).is_dir(),
            "missing EDK public root {prefix}"
        );
    }
    Ok(prefixes)
}

#[derive(Debug, Args)]
#[command(args_conflicts_with_subcommands = true, subcommand_negates_reqs = true)]
pub struct EdkArgs {
    #[command(subcommand)]
    command: Option<EdkCommand>,
    #[arg(short = 'd', long, required = true)]
    pub build_dir: Option<PathBuf>,
    #[arg(short = 'o', long, required = true)]
    pub output_dir: Option<PathBuf>,
    #[arg(long)]
    pub development: bool,
    #[arg(long)]
    pub force: bool,
}
#[derive(Debug, Subcommand)]
enum EdkCommand {
    /// Verify archive integrity and compiler inputs offline, without a toolchain.
    Verify {
        archive: PathBuf,
    },
    Qualify(QualifyArgs),
}
#[derive(Debug, Args)]
pub struct QualifyArgs {
    pub archive: PathBuf,
    #[arg(long)]
    pub host_build: Option<PathBuf>,
    #[arg(long)]
    pub zephyr_sdk: Option<PathBuf>,
    #[arg(long)]
    pub comparison_archive: Option<PathBuf>,
    #[arg(long = "packages-root")]
    pub package_roots: Vec<PathBuf>,
    #[arg(long)]
    pub packages_output: Option<PathBuf>,
}
pub fn config(path: &Path) -> Result<BTreeMap<String, String>> {
    let text = fs::read_to_string(path)?;
    Ok(text
        .lines()
        .filter_map(|line| {
            if let Some(rest) = line.strip_prefix("#define CONFIG_") {
                let (key, value) = rest.split_once(' ')?;
                Some((
                    format!("CONFIG_{key}"),
                    value.trim().trim_matches('"').to_owned(),
                ))
            } else {
                let (k, v) = line.split_once('=')?;
                k.starts_with("CONFIG_")
                    .then(|| (k.to_owned(), v.trim_matches('"').to_owned()))
            }
        })
        .collect())
}
pub fn cache(path: &Path, key: &str) -> Result<String> {
    let text = fs::read_to_string(path)?;
    text.lines()
        .find_map(|l| {
            let (k, v) = l.split_once('=')?;
            (k.split(':').next() == Some(key)).then(|| v.to_owned())
        })
        .ok_or_else(|| anyhow::anyhow!("missing cache value {key}"))
}
pub fn macro_value(path: &Path, key: &str) -> Result<String> {
    let text = fs::read_to_string(path)?;
    text.lines()
        .find_map(|l| {
            let s = l.strip_prefix("#define ")?;
            let rest = s.strip_prefix(key)?;
            if !rest.is_empty() && !rest.starts_with(char::is_whitespace) {
                return None;
            }
            Some(rest.trim().trim_matches('"').to_owned())
        })
        .ok_or_else(|| anyhow::anyhow!("missing generated macro {key}"))
}
pub fn app_build(build: &Path) -> PathBuf {
    for name in ["meshbus", "app"] {
        if build.join(name).join("zephyr/.config").is_file() {
            return build.join(name);
        }
    }
    build.to_path_buf()
}
pub fn git_state(root: &Path) -> Value {
    let revision = host::output(
        Command::new("git")
            .arg("-C")
            .arg(root)
            .args(["rev-parse", "HEAD"]),
    )
    .ok();
    let dirty = host::output(Command::new("git").arg("-C").arg(root).args([
        "status",
        "--porcelain",
        "--untracked-files=normal",
    ]))
    .map_or(true, |s| !s.is_empty());
    json!({"revision":revision,"dirty":dirty||revision.is_none()})
}
pub fn provenance(workspace: &Path, firmware: &Path) -> Result<Value> {
    let listing = host::output(Command::new("west").current_dir(workspace).args([
        "list",
        "-f",
        "{name}\t{abspath}",
    ]))?;
    let mut projects = serde_json::Map::new();
    for line in listing.lines() {
        let (name, path) = line
            .split_once('\t')
            .ok_or_else(|| anyhow::anyhow!("invalid west project listing"))?;
        if name == "manifest" {
            if Path::new(path).canonicalize()? == firmware.canonicalize()? {
                projects.insert("meshbus".into(), git_state(Path::new(path)));
            }
            continue;
        }
        projects.insert(name.into(), git_state(Path::new(path)));
    }
    ensure!(
        projects.contains_key("meshbus"),
        "SDK missing from west graph"
    );
    Ok(json!({"firmware":git_state(firmware),"projects":projects}))
}
pub fn clean(provenance: &Value) -> bool {
    provenance["firmware"]["dirty"] == false
        && provenance["projects"]
            .as_object()
            .is_some_and(|m| m.values().all(|s| s["dirty"] == false))
}
pub struct ContextData {
    pub build: PathBuf,
    pub info: Value,
    pub config: BTreeMap<String, String>,
    pub target: String,
    pub version: String,
    pub firmware: PathBuf,
    pub workspace: PathBuf,
    pub provenance: Value,
}
impl ContextData {
    pub fn read(build: &Path) -> Result<Self> {
        let build = app_build(build).canonicalize()?;
        let info: Value = serde_yaml_ng::from_slice(&fs::read(build.join("build_info.yml"))?)?;
        let config = config(&build.join("zephyr/.config"))?;
        let target = config
            .get("CONFIG_BOARD_TARGET")
            .context("missing CONFIG_BOARD_TARGET")?
            .to_owned();
        let board = &info["cmake"]["board"];
        let name = metadata::string(board, "name")?;
        let qualifiers = board["qualifiers"].as_str().unwrap_or("");
        ensure!(
            target
                == if qualifiers.is_empty() {
                    name.to_owned()
                } else {
                    format!("{name}/{qualifiers}")
                },
            "build_info target differs from Kconfig"
        );
        let source = PathBuf::from(metadata::string(
            &info["cmake"]["application"],
            "source-dir",
        )?);
        let firmware = PathBuf::from(host::output(
            Command::new("git")
                .arg("-C")
                .arg(&source)
                .args(["rev-parse", "--show-toplevel"]),
        )?);
        let workspace = PathBuf::from(host::output(
            Command::new("west").current_dir(&firmware).arg("topdir"),
        )?);
        let provenance = provenance(&workspace, &firmware)?;
        let version = macro_value(
            &build.join("zephyr/include/generated/zephyr/app_version.h"),
            "APP_VERSION_STRING",
        )?;
        ensure!(!version.is_empty(), "empty host version");
        let revision = macro_value(
            &build.join("zephyr/include/generated/zephyr/app_version.h"),
            "APP_BUILD_VERSION",
        )?;
        if regex::Regex::new("^[0-9a-fA-F]{7,40}$")?.is_match(&revision) {
            ensure!(
                provenance["firmware"]["revision"]
                    .as_str()
                    .is_some_and(|current| current.starts_with(&revision.to_ascii_lowercase())),
                "host build revision differs from current firmware checkout"
            );
        }

        Ok(Self {
            build,
            info,
            config,
            target,
            version,
            firmware,
            workspace,
            provenance,
        })
    }
    pub fn require_llext(&self) -> Result<()> {
        ensure!(
            self.config
                .get("CONFIG_MBS_LLEXT")
                .or_else(|| self.config.get("CONFIG_MESHBUS_LLEXT"))
                .is_some_and(|v| v == "y"),
            "host does not enable LLEXT"
        );
        ensure!(
            self.config.get("CONFIG_ZUI").is_some_and(|v| v == "y"),
            "host does not enable ZUI"
        );
        Ok(())
    }
    pub fn metadata_version(&self) -> Result<u32> {
        let generated = self.build.join("zephyr/include/generated");
        let current = generated.join("mbs_llext_metadata_version.h");
        // Existing host builds remain readable without rewriting their outputs.
        let s = if current.exists() {
            macro_value(&current, "MBS_LLEXT_METADATA_VERSION")?
        } else {
            macro_value(
                &generated.join("meshbus_llext_metadata_version.h"),
                "MESHBUS_LLEXT_METADATA_VERSION",
            )?
        };
        let n = s.trim_end_matches(['u', 'U']).parse()?;
        ensure!(n > 0, "invalid metadata version");
        Ok(n)
    }
    pub fn interface_abi(&self) -> Result<Option<u32>> {
        if self.metadata_version()? == 1 {
            return Ok(None);
        }
        let value = macro_value(
            &self
                .build
                .join("zephyr/include/generated/mbs_llext_metadata_version.h"),
            "MBS_LLEXT_INTERFACE_ABI",
        )?;
        let abi = value.trim_end_matches(['u', 'U']).parse::<u32>()?;
        ensure!(abi > 0, "invalid interface ABI");
        Ok(Some(abi))
    }
    pub fn toolchain(&self) -> Result<PathBuf> {
        Ok(PathBuf::from(metadata::string(
            &self.info["cmake"]["toolchain"],
            "path",
        )?))
    }
}
pub fn normalize(s: &str) -> String {
    regex::Regex::new("[^a-z0-9._-]+")
        .unwrap()
        .replace_all(&s.to_ascii_lowercase(), "-")
        .trim_matches(['-', '.'])
        .to_owned()
}
pub fn cmake_var(text: &str, key: &str) -> Option<String> {
    text.lines().find_map(|l| {
        let rest = l.strip_prefix(&format!("set({key} \""))?;
        Some(rest.strip_suffix("\")")?.to_owned())
    })
}
pub fn flags(root: &Path) -> Result<Vec<String>> {
    let text = fs::read_to_string(root.join("cmake.cflags"))?;
    let flags = cmake_var(&text, "LLEXT_CFLAGS").context("missing LLEXT_CFLAGS")?;
    Ok(flags
        .split(';')
        .filter(|s| !s.is_empty())
        .map(|s| {
            s.replace(
                "${CMAKE_CURRENT_LIST_DIR}",
                &root.to_string_lossy().replace('\\', "/"),
            )
        })
        .collect())
}
fn paths_valid(root: &Path) -> Result<()> {
    let text = fs::read_to_string(root.join("cmake.cflags"))?;
    for token in text.split([';', '"']) {
        for prefix in [
            "-I${CMAKE_CURRENT_LIST_DIR}/",
            "-imacros${CMAKE_CURRENT_LIST_DIR}/",
        ] {
            if let Some(p) = token.strip_prefix(prefix) {
                let path = archive::relative(p)?;
                ensure!(
                    root.join(path).exists(),
                    "EDK flag references missing path: {p}"
                );
            }
        }
    }
    let autoconf = root.join(AUTOCONF);
    if autoconf.is_file() {
        let values = config(&autoconf)?;
        for key in PRIVATE_KEY_PATH_CONFIGS {
            ensure!(
                values.get(key).is_none_or(String::is_empty),
                "private key path in EDK config: {key}"
            );
        }
    }
    Ok(())
}
fn scrub_private_key_paths(root: &Path) -> Result<()> {
    let path = root.join(AUTOCONF);
    if !path.is_file() {
        return Ok(());
    }
    let mut text = fs::read_to_string(&path)?;
    for key in PRIVATE_KEY_PATH_CONFIGS {
        let prefix = format!("#define {key} ");
        text = text
            .lines()
            .map(|line| {
                if line.starts_with(&prefix) {
                    format!("{prefix}\"\"")
                } else {
                    line.to_owned()
                }
            })
            .collect::<Vec<_>>()
            .join("\n")
            + "\n";
    }
    fs::write(path, text)?;
    Ok(())
}
fn sdk_owned(p: &str, build: &str) -> bool {
    p.starts_with("include/meshbus/")
        || p.contains("/modules/meshbus/")
        || p.starts_with(&format!("{build}/generated/"))
        || GENERATED.iter().any(|layout| p.contains(layout))
}
fn build_prefix(build: &Path, workspace: &Path) -> Result<String> {
    ensure!(
        build.is_absolute() && workspace.is_absolute(),
        "EDK build and workspace paths must be absolute"
    );
    let path = build.strip_prefix(workspace).unwrap_or(build);
    let path = path.to_string_lossy().replace('\\', "/");
    let prefix = format!("include/{}", path.trim_start_matches('/'));
    archive::relative(&prefix)?;
    Ok(prefix)
}
fn scrub_paths(
    root: &Path,
    scope: &Path,
    mut replacements: Vec<(PathBuf, &'static str)>,
) -> Result<()> {
    replacements.sort_by_key(|(p, _)| std::cmp::Reverse(p.as_os_str().len()));
    for p in host::files(scope)? {
        let bytes = fs::read(&p)?;
        let Ok(mut text) = String::from_utf8(bytes) else {
            continue;
        };
        let original = text.clone();
        for (path, replacement) in &replacements {
            let s = path.to_string_lossy();
            text = text
                .replace(s.as_ref(), replacement)
                .replace(&s.replace('\\', "/"), replacement)
                .replace(&s.replace('\\', "\\\\"), replacement);
        }
        if text != original {
            ensure!(
                p.strip_prefix(root)?
                    .starts_with("include/zephyr/include/generated"),
                "absolute host path outside generated EDK headers: {}",
                p.display()
            );
            fs::write(p, text)?;
        }
    }
    Ok(())
}
fn prune_empty_directories(root: &Path) -> Result<()> {
    fn prune(path: &Path) -> Result<bool> {
        let mut empty = true;
        for entry in fs::read_dir(path)? {
            let entry = entry?;
            let ty = entry.file_type()?;
            ensure!(
                !ty.is_symlink(),
                "symlink is not permitted: {}",
                entry.path().display()
            );
            if ty.is_dir() {
                if prune(&entry.path())? {
                    fs::remove_dir(entry.path())?;
                } else {
                    empty = false;
                }
            } else {
                ensure!(ty.is_file(), "special file in EDK");
                empty = false;
            }
        }
        Ok(empty)
    }

    let _ = prune(root)?;
    Ok(())
}
fn prune_empty_ancestors(path: &Path, stop: &Path) -> Result<()> {
    let mut current = path.parent();
    while let Some(directory) = current {
        if directory == stop {
            break;
        }
        ensure!(
            directory.starts_with(stop),
            "EDK path escaped pruning root: {}",
            directory.display()
        );
        if fs::read_dir(directory)?.next().is_some() {
            break;
        }
        fs::remove_dir(directory)?;
        current = directory.parent();
    }
    Ok(())
}
pub fn filter(root: &Path, build: &Path, workspace: &Path) -> Result<()> {
    let public_roots = public_roots(root)?;
    let files = host::files(root)?;
    let mut public = BTreeSet::new();
    let mut required = BTreeSet::new();
    let re = regex::Regex::new(r#"(?m)^\s*#\s*include\s*"(meshbus/[A-Za-z0-9_.-]+\.pb\.h)""#)?;
    for p in &files {
        let relative = p.strip_prefix(root)?.to_string_lossy().replace('\\', "/");
        if public_roots
            .iter()
            .any(|prefix| relative.starts_with(prefix.as_str()))
            && relative.ends_with(".h")
        {
            public.insert(p.clone());
            for c in re.captures_iter(&fs::read_to_string(p)?) {
                required.insert(c[1].to_owned());
            }
        }
    }
    ensure!(!public.is_empty(), "no public SDK headers");
    let mut keep = public;
    let mut prefixes = BTreeSet::new();
    for needed in &required {
        let mut matches = vec![];
        for p in &files {
            let r = p.strip_prefix(root)?.to_string_lossy().replace('\\', "/");
            if let Some((prefix, suffix)) = GENERATED.iter().find_map(|layout| r.split_once(layout))
                && format!("meshbus/{suffix}") == *needed
            {
                matches.push((p.clone(), prefix.to_owned()));
            }
        }
        ensure!(
            matches.len() == 1,
            "generated dependency {needed} must have one match"
        );
        let (p, prefix) = matches.remove(0);
        keep.insert(p);
        prefixes.insert(prefix);
    }
    ensure!(
        prefixes.len() == 1,
        "expected one generated EDK build prefix"
    );
    let old = prefixes.into_iter().next().unwrap();
    let expected = build_prefix(build, workspace)?;
    ensure!(
        old == expected,
        "generated EDK headers outside host build: expected {expected}, found {old}"
    );
    for p in files {
        let r = p.strip_prefix(root)?.to_string_lossy().replace('\\', "/");
        if sdk_owned(&r, &old) && !keep.contains(&p) {
            fs::remove_file(p)?;
        }
    }
    scrub_private_key_paths(root)?;
    // CMake may retain a lexical path such as /tmp while canonicalization
    // yields /private/tmp. Scrub the configured build spelling as well.
    scrub_paths(
        root,
        &root.join("include/zephyr/include/generated"),
        vec![(build.to_path_buf(), "<host-build>")],
    )?;
    if old != PREFIX {
        ensure!(
            !root.join(PREFIX).exists(),
            "canonical EDK build path already exists"
        );
        fs::create_dir_all(root.join(PREFIX).parent().unwrap())?;
        let old_path = root.join(&old);
        fs::rename(&old_path, root.join(PREFIX))?;
        prune_empty_ancestors(&old_path, &root.join("include"))?;
    }
    let quoted = regex::Regex::new(r#""((?:\\.|[^"\\])*)""#)?;
    for name in ["cmake.cflags", "Makefile.cflags"] {
        let mut out = vec![];
        for line in fs::read_to_string(root.join(name))?.lines() {
            let line = line.replace(&old, PREFIX);
            let cmake = name == "cmake.cflags";
            let active = if cmake {
                line.starts_with("set(LLEXT_") && line.ends_with("\")")
            } else {
                line.starts_with("LLEXT_") && line.contains('=')
            };
            if !active {
                out.push(line);
                continue;
            }
            let (first, last, tokens) = if cmake {
                let first = line.find('"').context("invalid CMake flags")?;
                let last = line.rfind('"').unwrap();
                (
                    first,
                    last,
                    line[first + 1..last]
                        .split(';')
                        .map(str::to_owned)
                        .collect::<Vec<_>>(),
                )
            } else {
                let first = line.find('=').unwrap();
                (
                    first,
                    line.len(),
                    quoted
                        .captures_iter(&line[first + 1..])
                        .map(|c| c[1].to_owned())
                        .collect(),
                )
            };
            let mut kept = vec![];
            for token in tokens {
                if token.is_empty()
                    || (token.contains("/include/meshbus/")
                        && !token.contains("/include/meshbus/include"))
                {
                    continue;
                }
                let mut token = token;
                for marker in ["${CMAKE_CURRENT_LIST_DIR}/", "$(LLEXT_EDK_INSTALL_DIR)/"] {
                    for option in ["-I", "-imacros"] {
                        let prefix = format!("{option}{marker}");
                        if let Some(path) = token.strip_prefix(&prefix) {
                            token = format!(
                                "{prefix}{}",
                                archive::relative(path)?
                                    .to_string_lossy()
                                    .replace('\\', "/")
                            );
                        }
                    }
                }
                kept.push(token);
            }
            out.push(if cmake {
                format!("{}{}{}", &line[..first + 1], kept.join(";"), &line[last..])
            } else {
                format!(
                    "{}= {}",
                    line[..first].trim_end(),
                    kept.into_iter()
                        .map(|t| format!("\"{t}\""))
                        .collect::<Vec<_>>()
                        .join(" ")
                )
            });
        }
        fs::write(root.join(name), out.join("\n") + "\n")?;
    }
    for directory in ["modules/meshbus", "generated"] {
        let generated_sdk = root.join(PREFIX).join(directory);
        if generated_sdk.is_dir() {
            prune_empty_directories(&generated_sdk)?;
        }
    }
    paths_valid(root)?;
    Ok(())
}
pub fn digest(root: &Path) -> Result<String> {
    let mut records = vec![];
    for p in host::files(root)? {
        let r = p.strip_prefix(root)?.to_string_lossy().replace('\\', "/");
        if r.starts_with("include/") || ["cmake.cflags", "Makefile.cflags"].contains(&r.as_str()) {
            let b = fs::read(p)?;
            records.push(json!({"path":r,"sha256":host::hash(&b),"size":b.len()}));
        }
    }
    ensure!(
        root.join("cmake.cflags").is_file()
            && root.join("Makefile.cflags").is_file()
            && root.join("include").is_dir(),
        "incomplete EDK compiler inputs"
    );
    records.sort_by(|a, b| a["path"].as_str().cmp(&b["path"].as_str()));
    Ok(host::hash(&serde_json::to_vec(&records)?))
}
pub fn manifest(root: &Path) -> Result<Value> {
    let m: Value =
        serde_json::from_slice(&host::read(&root.join("edk-release.json"), 1024 * 1024)?)?;
    ensure!(
        m["schema"] == 1 && m["publishable"].is_boolean() && m.get("profile").is_none(),
        "invalid EDK manifest"
    );
    metadata::string(&m, "target")?;
    metadata::string(&m["host"], "version")?;
    metadata::string(&m["host"], "application")?;
    metadata::string(&m["toolchain"], "compiler")?;
    ensure!(
        m["metadata-version"]
            .as_u64()
            .is_some_and(|v| v > 0 && v <= u32::MAX.into()),
        "invalid metadata version"
    );
    ensure!(
        m["edk"]["sdk-sha256"] == digest(root)?,
        "EDK compiler-input digest mismatch"
    );
    paths_valid(root)?;
    Ok(m)
}
fn standard(ctx: &ContextData) -> Result<PathBuf> {
    host::run(
        Command::new("west")
            .current_dir(&ctx.workspace)
            .args(["build", "-d"])
            .arg(&ctx.build)
            .args(["-t", "llext-edk"]),
    )?;
    let candidates = [
        ctx.build.join("zephyr/llext-edk.tar.xz"),
        ctx.build.join("llext-edk.tar.xz"),
    ];
    candidates
        .into_iter()
        .find(|p| p.is_file())
        .context("standard EDK archive not produced")
}
fn scrub(root: &Path, ctx: &ContextData) -> Result<()> {
    let sdk = PathBuf::from(host::output(
        Command::new("west").current_dir(&ctx.workspace).args([
            "list",
            "meshbus",
            "-f",
            "{abspath}",
        ]),
    )?);
    let zephyr = PathBuf::from(metadata::string(
        &ctx.info["cmake"]["zephyr"],
        "zephyr-base",
    )?);
    let replacements = vec![
        (ctx.build.clone(), "<host-build>"),
        (sdk, "<sdk-meshbus>"),
        (ctx.firmware.clone(), "<firmware>"),
        (zephyr, "<zephyr>"),
        (ctx.toolchain()?, "<toolchain>"),
        (ctx.workspace.clone(), "<workspace>"),
    ];
    scrub_paths(root, root, replacements)
}
pub fn create(build: &Path, out: &Path, development: bool, force: bool) -> Result<PathBuf> {
    ensure!(!force || development, "--force requires --development");
    let ctx = ContextData::read(build)?;
    ctx.require_llext()?;
    ensure!(
        development || clean(&ctx.provenance),
        "release EDK requires clean committed source and dependencies"
    );
    let raw = standard(&ctx)?;
    let temporary = tempfile::tempdir()?;
    eprintln!("meshbus edk: extracting standard archive");
    let root = archive::extract_edk(&raw, temporary.path())?;
    eprintln!("meshbus edk: filtering public compiler inputs");
    let cmake = fs::read_to_string(root.join("cmake.cflags"))?;
    let name = cmake_var(&cmake, "LLEXT_EDK_BOARD_NAME").unwrap_or_default();
    let q = cmake_var(&cmake, "LLEXT_EDK_BOARD_QUALIFIERS").unwrap_or_default();
    let target = if q.is_empty() {
        name
    } else {
        format!("{name}/{q}")
    };
    ensure!(
        target == ctx.target
            || cmake_var(&cmake, "LLEXT_EDK_BOARD_TARGET").as_deref() == Some(&ctx.target),
        "EDK target mismatch"
    );
    let configured_build = PathBuf::from(cache(
        &ctx.build.join("CMakeCache.txt"),
        "APPLICATION_BINARY_DIR",
    )?);
    filter(&root, &configured_build, &ctx.workspace)?;
    eprintln!("meshbus edk: normalizing provenance");
    scrub(&root, &ctx)?;
    let compiler = PathBuf::from(cache(
        &ctx.build.join("CMakeCache.txt"),
        "CMAKE_C_COMPILER",
    )?);
    let compiler_version = host::output(Command::new(&compiler).arg("-dumpfullversion"))?;
    let host_bytes = host::read(&ctx.build.join("zephyr/zephyr.elf"), 64 * 1024 * 1024)?;
    let host_config = config(&ctx.build.join("zephyr/include/generated/zephyr/autoconf.h"))?;
    let exports = if host_config
        .get("CONFIG_LLEXT_EXPORT_BUILTINS_BY_SLID")
        .is_some_and(|v| v == "1")
    {
        None
    } else {
        Some(Elf::parse(&host_bytes)?.builtin_exports(&host_bytes)?)
    };
    let manifest = json!({"schema":1,"interface-abi":ctx.interface_abi()?,"exported-symbols":exports,"metadata-version":ctx.metadata_version()?,"publishable":!development&&clean(&ctx.provenance),"host":{"application":"app","version":ctx.version,"build-revision":macro_value(&ctx.build.join("zephyr/include/generated/zephyr/app_version.h"),"APP_BUILD_VERSION")?,"source-revision":ctx.provenance["firmware"]["revision"]},"target":ctx.target,"edk":{"header-policy":"meshbus-public-v1","sdk-sha256":digest(&root)?},"zephyr":{"version":ctx.info["cmake"]["zephyr"]["version"],"revision":ctx.provenance["projects"]["zephyr"]["revision"]},"toolchain":{"name":ctx.info["cmake"]["toolchain"]["name"],"identity":format!("{}/{}-{compiler_version}",ctx.toolchain()?.file_name().unwrap().to_string_lossy(),compiler.file_name().unwrap().to_string_lossy()),"compiler":compiler.file_name().unwrap().to_string_lossy()},"provenance":ctx.provenance});
    host::json(&root.join("edk-release.json"), &manifest)?;
    fs::copy(
        Path::new(metadata::string(
            &ctx.info["cmake"]["zephyr"],
            "zephyr-base",
        )?)
        .join("LICENSE"),
        root.join("LICENSE.txt"),
    )?;
    fs::write(
        root.join("NOTICE.txt"),
        "Meshbus public EDK. Header files retain their original SPDX licenses and copyright notices. Generated from the exact host identified by edk-release.json; no firmware signing keys or compiler binaries are included.\n",
    )?;
    let name = format!(
        "app-{}-{}{}-edk.tar.xz",
        normalize(&ctx.version),
        normalize(&ctx.target),
        if development { "-dev" } else { "" }
    );
    let output = out.join(name);
    if output.exists() {
        ensure!(force, "EDK output already exists");
        let old = tempfile::tempdir()?;
        let old = archive::extract_edk(&output, old.path())?;
        let m = self::manifest(&old)?;
        ensure!(
            m["publishable"] == false
                && m["target"] == manifest["target"]
                && m["host"]["version"] == manifest["host"]["version"],
            "refusing to replace different or publishable EDK identity"
        );
    }
    eprintln!("meshbus edk: writing release archive");
    archive::pack(&root, &output, "llext-edk")?;
    archive::sidecar(&output)?;
    Ok(output)
}
pub fn qualify(args: &QualifyArgs) -> Result<Value> {
    archive::verify_sidecar(&args.archive)?;
    let temp = tempfile::tempdir()?;
    let root = archive::extract_edk(&args.archive, temp.path())?;
    let m = manifest(&root)?;
    for name in ["LICENSE.txt", "NOTICE.txt"] {
        ensure!(
            !host::read(&root.join(name), 1024 * 1024)?.is_empty(),
            "EDK notice missing"
        );
    }
    let sdk = if let Some(sdk) = &args.zephyr_sdk {
        sdk.clone()
    } else {
        ContextData::read(
            args.host_build
                .as_ref()
                .context("--zephyr-sdk or --host-build required")?,
        )?
        .toolchain()?
    };
    if let Some(build) = &args.host_build {
        let ctx = ContextData::read(build)?;
        ctx.require_llext()?;
        ensure!(
            m["target"] == ctx.target
                && m["host"]["version"] == ctx.version
                && m["metadata-version"] == ctx.metadata_version()?
                && m["interface-abi"] == serde_json::to_value(ctx.interface_abi()?)?,
            "EDK identity differs from host build"
        );
    }
    let windows_host_path = regex::Regex::new(r"[A-Za-z]:\\\\")?;
    let public_roots = public_roots(&root)?;
    let mut headers = vec![];
    for p in host::files(&root)? {
        let r = p.strip_prefix(&root)?.to_string_lossy().replace('\\', "/");
        if public_roots
            .iter()
            .any(|prefix| r.starts_with(prefix.as_str()))
            && r.ends_with(".h")
        {
            headers.push(p.clone());
        } else if sdk_owned(&r, PREFIX) {
            ensure!(
                GENERATED
                    .iter()
                    .any(|layout| r.starts_with(&format!("{PREFIX}{layout}")))
                    && r.ends_with(".pb.h"),
                "private SDK file in EDK"
            );
        }
        let b = fs::read(p)?;
        let s = String::from_utf8_lossy(&b);
        ensure!(
            !s.contains("/Users/") && !s.contains("/home/") && !windows_host_path.is_match(&s),
            "host path in EDK"
        );
    }
    ensure!(!headers.is_empty(), "EDK has no public headers");
    let base = flags(&root)?;
    for (language, compiler, std) in [("c", "gcc", "c17"), ("c++", "g++", "c++17")] {
        let compiler = crate::llext::tool(&sdk, compiler)?;
        for header in &headers {
            let include = header
                .strip_prefix(root.join("include/meshbus/include"))?
                .to_string_lossy()
                .replace('\\', "/");
            let mut command = Command::new(&compiler);
            command
                .args(
                    base.iter().filter(|s| {
                        !s.starts_with("-std=") && !s.starts_with("-fdiagnostics-color=")
                    }),
                )
                .arg(format!("-std={std}"))
                .args([
                    "-fdiagnostics-color=never",
                    "-fsyntax-only",
                    "-x",
                    language,
                    "-",
                ])
                .stdin(Stdio::piped())
                .stdout(Stdio::null())
                .stderr(Stdio::piped());
            let mut child = command.spawn()?;
            child
                .stdin
                .take()
                .unwrap()
                .write_all(format!("#include <{include}>\n").as_bytes())?;
            let output = child.wait_with_output()?;
            ensure!(
                output.status.success(),
                "{language} header {include} failed: {}",
                String::from_utf8_lossy(&output.stderr)
            );
        }
    }
    let mut packages = vec![];
    for source in &args.package_roots {
        let output = args
            .packages_output
            .as_ref()
            .context("--packages-output required")?;
        packages.push(crate::llext::build(&crate::llext::LlextArgs {
            build_dir: None,
            output_dir: Some(output.clone()),
            llext_sdk: Some(root.clone()),
            zephyr_sdk: Some(sdk.clone()),
            force_edk: false,
            source_dir: source.clone(),
            cmake_args: vec![],
        })?);
    }
    if let Some(comparison) = &args.comparison_archive {
        archive::verify_sidecar(comparison)?;
        ensure!(
            fs::read(comparison)? == fs::read(&args.archive)?,
            "EDK archives are not reproducible"
        );
    }
    Ok(
        json!({"schema":1,"target":m["target"],"sdk-sha256":m["edk"]["sdk-sha256"],"checks":{"public-header-c":{"status":"passed","count":headers.len()},"public-header-cxx":{"status":"passed","count":headers.len()},"official-packages":{"status":if packages.is_empty(){"not-run"}else{"passed"},"artifacts":packages},"reproducibility":{"status":if args.comparison_archive.is_some(){"passed"}else{"not-run"}}}}),
    )
}
pub fn run(args: EdkArgs) -> Result<()> {
    match args.command {
        Some(EdkCommand::Verify { archive: path }) => {
            archive::verify_sidecar(&path)?;
            let temp = tempfile::tempdir()?;
            let root = archive::extract_edk(&path, temp.path())?;
            let manifest = manifest(&root)?;
            host::print(&json!({"verified":true,"manifest":manifest}))
        }
        Some(EdkCommand::Qualify(q)) => host::print(&qualify(&q)?),
        None => {
            let path = create(
                args.build_dir.as_ref().context("--build-dir required")?,
                args.output_dir.as_ref().context("--output-dir required")?,
                args.development,
                args.force,
            )?;
            host::print(&json!({"edk":path}))
        }
    }
}
