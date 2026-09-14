// SPDX-License-Identifier: Apache-2.0
use super::{inputs, project};
use anyhow::{Result, ensure};
use serde_json::{Value, json};
use std::path::Path;

pub fn run(root: &Path) -> Result<()> {
    let mut checks = vec![];
    let mut check = |name: &str, action: &str, result: Result<()>| {
        checks.push(json!({"check":name,"ok":result.is_ok(),
            "diagnostic":result.err().map(|e|format!("{e:#}")),"action":action}));
    };
    let declaration = project::read(root);
    let lock = inputs::read_lock(root);
    let mut identity = Value::Null;
    let mut budget = json!({"heap":"unknown until build computes ELF memory requirements"});
    match (&declaration, &lock) {
        (Ok(input), Ok(lock)) => {
            let r = &lock.release;
            identity = json!({"target":r.target,"firmware":r.firmware,"profile":r.profile,
                "edk":r.edk,"sdk":r.sdk,"tools":r.tools,"toolchain":r.toolchain});
            check(
                "platform",
                "select inputs for this host, then app update",
                if r.host_platform == inputs::platform() {
                    Ok(())
                } else {
                    Err(anyhow::anyhow!("lock belongs to another host platform"))
                },
            );
            check(
                "edk",
                "restore the EDK origin and run app sync, or select a new source and app update",
                r.edk.verify().and_then(|_| inputs::check_edk(r)),
            );
            check(
                "compiler",
                "restore the pinned compiler installation or select compatible inputs and app update",
                r.toolchain
                    .verify()
                    .and_then(|_| inputs::check_compiler(&r.edk.path, &r.toolchain.path)),
            );
            for name in ["cmake", "ninja", "python"] {
                check(
                    name,
                    "restore this pinned tool path or select new inputs and app update",
                    r.tools
                        .get(name)
                        .ok_or_else(|| anyhow::anyhow!("missing tool {name}"))
                        .and_then(|t| t.verify()),
                );
            }
            if let Some(sdk) = &r.sdk {
                check(
                    "sdk",
                    "restore the SDK origin and app sync, or explicitly update its identity",
                    sdk.verify(),
                );
            }
            check(
                "project compatibility",
                "correct requires/dependencies or select a compatible EDK and SDK",
                project::check_requirements(
                    input,
                    &r.edk.path,
                    r.sdk.as_ref().map(|s| s.path.as_path()),
                    false,
                ),
            );
            match project::stack_budget(input, &r.edk.path) {
                Ok(value) => budget["stack"] = value,
                Err(error) => check(
                    "stack budget",
                    "reduce stack-size or use firmware with a larger app stack",
                    Err(error),
                ),
            }
        }
        _ => {
            if let Err(e) = declaration {
                check("project", "correct llext.yaml", Err(e));
            }
            if let Err(e) = lock {
                check(
                    "lock",
                    "select target and app sync; use app update for an intentional declaration change",
                    Err(e),
                );
            }
        }
    }
    let ready = checks.iter().all(|c| c["ok"] == true);
    crate::host::print(
        &json!({"ready":ready,"identity":identity,"checks":checks,"budget":budget}),
    )?;
    ensure!(ready, "project is not ready; see doctor checks and actions");
    Ok(())
}
