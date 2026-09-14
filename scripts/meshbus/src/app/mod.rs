// SPDX-License-Identifier: Apache-2.0
//! Customer MBA workflows. Release builds remain owned by `west release`.
mod doctor;
mod inputs;
mod project;
use anyhow::{Context, Result, ensure};
use clap::{Args, Subcommand, ValueEnum};
use std::{
    collections::BTreeMap,
    path::{Path, PathBuf},
};

#[derive(Debug, Args)]
pub struct AppArgs {
    #[arg(long, global = true, default_value = ".")]
    pub project: PathBuf,
    #[arg(long, global = true)]
    pub cache_dir: Option<PathBuf>,
    #[command(subcommand)]
    pub command: AppCommand,
}
#[derive(Debug, Clone, Copy, ValueEnum)]
pub enum Template {
    Native,
    Arduboy,
}
#[derive(Debug, Default, Args)]
pub struct BuildArgs {
    #[arg(long)]
    pub edk: Option<PathBuf>,
    #[arg(long)]
    pub toolchain: Option<PathBuf>,
    #[arg(long)]
    pub sdk: Option<PathBuf>,
    #[arg(long)]
    pub locked: bool,
    /// Local-first builds never access the network, with or without this flag.
    #[arg(long)]
    pub offline: bool,
}
#[derive(Debug, Args)]
pub struct SourceArgs {
    #[arg(long)]
    pub output: PathBuf,
    #[arg(long)]
    pub edk: PathBuf,
    #[arg(long)]
    pub toolchain: PathBuf,
    #[arg(long)]
    pub sdk: Option<PathBuf>,
    #[arg(long, default_value = "default")]
    pub profile: String,
    #[arg(long)]
    pub cmake: Option<PathBuf>,
    #[arg(long)]
    pub ninja: Option<PathBuf>,
    #[arg(long)]
    pub python: Option<PathBuf>,
}
#[derive(Debug, Subcommand)]
pub enum AppCommand {
    New {
        directory: PathBuf,
        #[arg(long, value_enum, default_value = "native")]
        template: Template,
    },
    Build(BuildArgs),
    /// Describe a set of local release inputs without publishing anything.
    Source(SourceArgs),
    /// Select a release by exact target, firmware and profile.
    Target {
        #[arg(long)]
        source: PathBuf,
        #[arg(long)]
        target: String,
        #[arg(long)]
        firmware: String,
        #[arg(long, default_value = "default")]
        profile: String,
    },
    /// Verify/cache local inputs, preserving an existing lock.
    Sync {
        #[arg(long)]
        locked: bool,
        #[arg(long)]
        offline: bool,
    },
    /// Explicitly replace the lock from the selected local source.
    Update {
        #[arg(long)]
        offline: bool,
    },
    /// Diagnose project inputs without changing them.
    Doctor,
}
fn executable(explicit: Option<&Path>, name: &str) -> Result<PathBuf> {
    if let Some(path) = explicit {
        return Ok(path.canonicalize()?);
    }
    std::env::split_paths(&std::env::var_os("PATH").unwrap_or_default())
        .map(|p| p.join(name))
        .find(|p| p.is_file())
        .with_context(|| format!("missing tool {name}; supply its local path"))?
        .canonicalize()
        .map_err(Into::into)
}
fn create_source(args: SourceArgs) -> Result<()> {
    use inputs::{Artifact, Release, Source};
    ensure!(
        !args.output.exists(),
        "source already exists; choose a new output path"
    );
    let edk = Artifact::local(&args.edk)?;
    let m = inputs::edk_manifest(&edk.path)?;
    let compiler = crate::llext::tool(&args.toolchain, "gcc")?.canonicalize()?;
    let toolchain = Artifact::local(compiler.parent().unwrap().parent().unwrap())?;
    let mut tools = BTreeMap::new();
    for (name, path, default) in [
        ("cmake", args.cmake.as_deref(), "cmake"),
        ("ninja", args.ninja.as_deref(), "ninja"),
        ("python", args.python.as_deref(), "python3"),
    ] {
        tools.insert(name.into(), Artifact::local(&executable(path, default)?)?);
    }
    let mut release = Release {
        target: crate::metadata::string(&m, "target")?.into(),
        firmware: crate::metadata::string(&m["host"], "build-revision")?.into(),
        profile: args.profile,
        host_platform: inputs::platform(),
        edk,
        toolchain,
        sdk: args.sdk.as_deref().map(Artifact::local).transpose()?,
        tools,
        metadata_version: m["metadata-version"]
            .as_u64()
            .context("missing metadata version")?
            .try_into()?,
        interface_abi: m["interface-abi"].as_u64().map(u32::try_from).transpose()?,
    };
    inputs::check_edk(&release)?;
    let parent = args
        .output
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or(Path::new("."));
    std::fs::create_dir_all(parent)?;
    let parent = parent.canonicalize()?;
    if let Ok(relative) = release.edk.path.strip_prefix(&parent) {
        // Keep the EDK beside its release index relocatable as one directory.
        release.edk.path = relative.into();
    }
    let source = Source {
        schema: 1,
        minimum_cli: env!("CARGO_PKG_VERSION").into(),
        releases: vec![release],
    };
    inputs::write_json(&args.output, &source)?;
    crate::host::print(&serde_json::json!({"source":args.output,"release":source.releases[0]}))
}
fn cache_dir(explicit: Option<PathBuf>) -> Result<PathBuf> {
    if let Some(path) = explicit {
        return Ok(path);
    }
    let root = std::env::var_os("HOME").context("set --cache-dir when HOME is unavailable")?;
    Ok(PathBuf::from(root).join("Library/Caches/meshbus/inputs"))
}
pub fn run(args: AppArgs) -> Result<()> {
    match args.command {
        AppCommand::New {
            directory,
            template,
        } => project::create(&directory, template),
        AppCommand::Build(inputs) => {
            let package = project::build(&args.project, &inputs)?;
            crate::host::print(&serde_json::json!({"package":package}))
        }
        AppCommand::Source(inputs) => create_source(inputs),
        AppCommand::Target {
            source,
            target,
            firmware,
            profile,
        } => {
            project::read(&args.project)?;
            crate::host::print(&inputs::select(
                &args.project,
                &source,
                &target,
                &firmware,
                &profile,
            )?)
        }
        AppCommand::Sync { locked, offline: _ } => crate::host::print(&inputs::sync(
            &args.project,
            &cache_dir(args.cache_dir)?,
            locked,
            false,
        )?),
        AppCommand::Update { offline: _ } => crate::host::print(&inputs::sync(
            &args.project,
            &cache_dir(args.cache_dir)?,
            false,
            true,
        )?),
        AppCommand::Doctor => doctor::run(&args.project),
    }
}
pub(crate) use project::metadata;
