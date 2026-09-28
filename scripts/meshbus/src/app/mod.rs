// SPDX-License-Identifier: Apache-2.0
//! Customer MBA workflows. Release builds remain owned by `west release`.
pub mod bundle;
mod develop;
pub mod device;
mod diagnostics;
mod doctor;
mod inputs;
mod install;
mod project;
mod session;
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
    #[arg(long, global = true)]
    pub device: Option<String>,
    #[arg(long, global = true)]
    pub log_file: Option<PathBuf>,
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
    /// Capture a software frame associated with the last confirmed run.
    Capture {
        output: PathBuf,
    },
    /// Map an explicit section-relative offset using this run's verified symbols.
    Diagnose {
        #[arg(long)]
        section: Option<String>,
        #[arg(long)]
        offset: Option<u64>,
    },
    /// Debounce source changes and deploy serially; Ctrl-C retains the last app.
    Watch {
        #[command(flatten)]
        build: BuildArgs,
        #[arg(long)]
        atomic: bool,
    },
    /// Build, install, confirm start, then follow this Session's shared device log.
    Run {
        #[command(flatten)]
        build: BuildArgs,
        #[arg(long)]
        atomic: bool,
        #[arg(long)]
        follow_seconds: Option<u64>,
    },
    /// Follow logs while querying the exact Session; Ctrl-C keeps the app running.
    Logs {
        #[arg(long)]
        session: Option<u64>,
        #[arg(long)]
        follow_seconds: Option<u64>,
    },
    /// Verify, upload and commit one complete package on the bound device.
    Install {
        manifest: Option<PathBuf>,
        #[arg(long)]
        replace: bool,
        #[arg(long)]
        atomic: bool,
    },
    /// Query the persistent installed package and any interrupted transaction.
    Installed {
        id: Option<String>,
    },
    /// Retry the commit, or explicitly discard an uncommitted install.
    Recover {
        id: String,
        #[arg(long)]
        abort: bool,
    },
    /// Select the retained previous complete version.
    Rollback {
        id: String,
    },
    /// Remove only registered app files, preserving saves by default.
    Uninstall {
        id: String,
        #[arg(long)]
        remove_saves: bool,
    },
    /// Collect or verify a versioned MBA/sidecar file set.
    Package {
        mba: PathBuf,
    },
    /// Describe a set of local release inputs without publishing anything.
    Source(SourceArgs),
    /// Select a release by exact target, firmware and profile.
    Target {
        #[arg(long)]
        source: PathBuf,
        #[arg(long)]
        target: Option<String>,
        #[arg(long)]
        firmware: Option<String>,
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
    /// Start an MBA by its metadata ID and installed path.
    Start {
        id: String,
        #[arg(long)]
        path: Option<String>,
    },
    /// Query the latest or an exact managed Session.
    Status {
        #[arg(long)]
        session: Option<u64>,
    },
    /// Cooperatively stop exactly the selected app; never abort its thread.
    Stop {
        id: String,
        #[arg(long)]
        session: Option<u64>,
        #[arg(long, default_value_t=5000, value_parser=clap::value_parser!(u32).range(1..=30000))]
        timeout_ms: u32,
    },
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
    crate::edk::named_exports(&m)?;
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
        image_sha256: m["host"]["image-sha256"].as_str().map(str::to_owned),
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
        AppCommand::Run {
            build,
            atomic,
            follow_seconds,
        } => {
            let guard = develop::InterruptGuard::install()?;
            let (mut client, _, started) = develop::deploy(
                &args.project,
                &build,
                args.device.as_deref(),
                args.log_file,
                atomic,
            )?;
            develop::follow(&mut client, started.session_id, follow_seconds, &guard)
        }
        AppCommand::Logs {
            session: id,
            follow_seconds,
        } => {
            let guard = develop::InterruptGuard::install()?;
            let (mut client, binding) =
                device::open(&args.project, args.device.as_deref(), args.log_file)?;
            client.follow_logs()?;
            let current = session::status(&mut client, id.unwrap_or(0))?;
            ensure!(current.session_id != 0, "no managed Session");
            let build = match client.file_hash(&current.path) {
                Ok((length, sha256)) => serde_json::json!({"length":length,"mba_sha256":sha256}),
                Err(error) => {
                    serde_json::json!({"identity":"unknown","reason":format!("{error:#}")})
                }
            };
            crate::host::print(
                &serde_json::json!({"device":binding,"session":current,"build":build,"log_scope":"shared firmware stream; state is queried, never inferred from log lines"}),
            )?;
            develop::follow(&mut client, current.session_id, follow_seconds, &guard)
        }
        AppCommand::Watch { build, atomic } => {
            develop::watch(&args.project, &build, args.device.as_deref(), atomic)
        }
        AppCommand::Capture { output } => {
            diagnostics::capture(&args.project, args.device.as_deref(), &output)
        }
        AppCommand::Diagnose { section, offset } => {
            diagnostics::diagnose(&args.project, section.as_deref(), offset)
        }
        AppCommand::Build(inputs) => {
            let package = project::build(&args.project, &inputs)?;
            crate::host::print(&serde_json::json!({"package":package}))
        }
        AppCommand::Package { mba } => {
            let manifest = bundle::collect(&mba)?;
            crate::host::print(&serde_json::json!({"manifest":manifest}))
        }
        AppCommand::Source(inputs) => create_source(inputs),
        AppCommand::Target {
            source,
            target,
            firmware,
            profile,
        } => {
            project::read(&args.project)?;
            if let Some(device) = args.device.as_deref() {
                ensure!(
                    target.is_none() && firmware.is_none(),
                    "choose --device or explicit target/firmware"
                );
                device::target(&args.project, device, &source, &profile)
            } else {
                crate::host::print(&inputs::select(
                    &args.project,
                    &source,
                    target.as_deref().context("supply --target or --device")?,
                    firmware
                        .as_deref()
                        .context("supply --firmware or --device")?,
                    &profile,
                    None,
                )?)
            }
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
        AppCommand::Install {
            manifest,
            replace,
            atomic,
        } => {
            let manifest = manifest
                .map(Ok)
                .unwrap_or_else(|| install::default_manifest(&args.project))?;
            let (mut client, binding) =
                device::open(&args.project, args.device.as_deref(), args.log_file)?;
            session::check_host(&args.project, &binding.host)?;
            crate::host::print(&install::install(
                &mut client,
                &binding.host,
                &manifest,
                replace,
                atomic,
            )?)
        }
        AppCommand::Installed { id } => {
            let (mut client, _) =
                device::open(&args.project, args.device.as_deref(), args.log_file)?;
            match id {
                Some(id) => crate::host::print(&install::command(&mut client, "status", &id, "")?),
                None => crate::host::print(&install::list(&mut client)?),
            }
        }
        AppCommand::Recover { id, abort } => {
            let (mut client, _) =
                device::open(&args.project, args.device.as_deref(), args.log_file)?;
            install::idle(&mut client, &id)?;
            crate::host::print(&install::command(
                &mut client,
                if abort { "abort" } else { "commit" },
                &id,
                "",
            )?)
        }
        AppCommand::Rollback { id } => {
            let (mut client, _) =
                device::open(&args.project, args.device.as_deref(), args.log_file)?;
            let installed = install::command(&mut client, "status", &id, "")?;
            ensure!(
                !installed.previous_bundle.is_empty(),
                "no retained previous version"
            );
            install::idle(&mut client, &id)?;
            crate::host::print(&install::command(
                &mut client,
                "rollback",
                &id,
                &installed.previous_bundle,
            )?)
        }
        AppCommand::Uninstall { id, remove_saves } => {
            let (mut client, _) =
                device::open(&args.project, args.device.as_deref(), args.log_file)?;
            install::idle(&mut client, &id)?;
            crate::host::print(&install::command(
                &mut client,
                "uninstall",
                &id,
                &remove_saves.to_string(),
            )?)
        }
        AppCommand::Start { id, path } => {
            let (mut client, binding) =
                device::open(&args.project, args.device.as_deref(), args.log_file)?;
            session::check_host(&args.project, &binding.host)?;
            let path = match path {
                Some(path) => path,
                None => {
                    let installed = install::command(&mut client, "status", &id, "")?;
                    ensure!(
                        !installed.mba_path.is_empty() && installed.pending_bundle.is_empty(),
                        "app has no committed launchable package"
                    );
                    installed.mba_path
                }
            };
            session::print_result(&session::start(&mut client, &id, &path)?)
        }
        AppCommand::Status { session: id } => {
            let (mut client, _) =
                device::open(&args.project, args.device.as_deref(), args.log_file)?;
            crate::host::print(&session::status(&mut client, id.unwrap_or(0))?)
        }
        AppCommand::Stop {
            id,
            session: instance,
            timeout_ms,
        } => {
            let (mut client, _) =
                device::open(&args.project, args.device.as_deref(), args.log_file)?;
            let status = session::stop(&mut client, &id, instance, timeout_ms)?;
            session::print_result(&status)?;
            ensure!(
                status.resources_reclaimed,
                "Session resources are not reclaimed; replacement is forbidden"
            );
            Ok(())
        }
    }
}
pub(crate) use project::metadata;
