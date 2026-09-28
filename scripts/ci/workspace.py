# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Update each CI workspace from the source manifest; retain version records only."""

import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

import yaml
from west.manifest import Manifest

SHA = re.compile(r"[0-9a-f]{40}\Z")


def run(*args, cwd, capture=False, env=None):
    result = subprocess.run([str(a) for a in args], cwd=cwd, check=True,
                            text=True, stdout=subprocess.PIPE if capture else None, env=env)
    return result.stdout.strip() if capture else None


def destination(workspace, relative):
    path = Path(relative)
    if path.is_absolute() or not path.parts or ".." in path.parts:
        raise ValueError(f"unsafe manifest path: {relative}")
    target = (workspace / path).resolve()
    if not target.is_relative_to(workspace) or target == workspace:
        raise ValueError(f"manifest path escapes workspace: {relative}")
    return target


def require_fixed(projects):
    moving = [p["name"] for p in projects if not SHA.fullmatch(p["revision"])]
    if moving:
        raise ValueError("CI requires full commit SHAs in west.yml and imports: " + ", ".join(moving))


def init(workspace):
    if not (workspace / ".west").exists():
        run("west", "init", "-l", "meshbus", cwd=workspace)
    actual = run("west", "config", "manifest.path", cwd=workspace, capture=True)
    if actual != "meshbus":
        raise ValueError("CI requires the manifest checkout at <workspace>/meshbus")


def validate(workspace, manifest):
    projects = manifest.projects[1:]
    require_fixed([{"name": p.name, "revision": p.revision} for p in projects])
    source = workspace / 'meshbus'
    occupied = set()
    for project in projects:
        if not manifest.is_active(project):
            continue
        path = destination(workspace, project.path)
        if path == source or path.is_relative_to(source) or source.is_relative_to(path):
            raise ValueError("dependency overlaps the source checkout")
        if any(path == other or path.is_relative_to(other) or other.is_relative_to(path) for other in occupied):
            raise ValueError("duplicate dependency destination")
        occupied.add(path)
        if not project.url.startswith('https://'):
            raise ValueError("CI dependencies must use public HTTPS URLs")
        if project.submodules:
            raise ValueError(f"submodules need explicit snapshot support: {project.name}")
        if path.exists():
            if not (path / '.git').exists():
                raise ValueError(f"dependency destination is not a Git checkout: {path}")
            remotes = run('git', 'remote', cwd=path, capture=True).splitlines()
            urls = [run('git', 'remote', 'get-url', remote, cwd=path, capture=True) for remote in remotes]
            if project.url not in urls or run('git', 'status', '--porcelain', cwd=path, capture=True):
                raise ValueError(f"refusing to replace an unrelated or dirty dependency: {path}")


def update(workspace, source):
    anonymous = os.environ.copy()
    anonymous.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM='1',
                     GIT_TERMINAL_PROMPT='0', GIT_CONFIG_COUNT='2',
                     GIT_CONFIG_KEY_0='credential.helper', GIT_CONFIG_VALUE_0='',
                     GIT_CONFIG_KEY_1='safe.directory', GIT_CONFIG_VALUE_1=str(source))
    args = []
    if seed := os.environ.get('MESHBUS_WEST_SEED'):
        # Let west clone matching paths from the image, then fetch only missing
        # revisions from the source manifest. Never replace the job's manifest.
        args = ['--path-cache', Path(seed).resolve(strict=True)]
    run("west", "update", "--narrow", "-o=--depth=1", *args, cwd=workspace, env=anonymous)


def setup(workspace, output=None, reference=None):
    source = workspace / 'meshbus'
    record = {
        'source_revision': run('git', 'rev-parse', 'HEAD', cwd=source, capture=True),
        'manifest_sha256': hashlib.sha256((source / 'west.yml').read_bytes()).hexdigest(),
    }
    if reference and record != json.loads((reference / 'source.json').read_text()):
        raise ValueError('snapshot source identity differs from the triggering checkout')
    init(workspace)
    # Check the root, self imports and any already available project imports
    # before updating. Missing imports are checked after west fetches them.
    validate(workspace, Manifest.from_topdir(str(workspace), importer=lambda project, path: None))
    update(workspace, source)
    manifest = Manifest.from_topdir(str(workspace))
    validate(workspace, manifest)
    for project in manifest.projects[1:]:
        if manifest.is_active(project) and project.sha('HEAD') != project.revision:
            raise ValueError(f"dependency differs from declared revision: {project.name}")
    frozen = manifest.as_frozen_dict(active_only=True)
    if reference:
        expected = yaml.safe_load((reference / 'west-frozen.yml').read_text())
        if frozen != expected:
            raise ValueError('updated west graph differs from the recorded graph')
    run('west', 'zephyr-export', cwd=workspace)
    if output:
        output.mkdir(parents=True, exist_ok=True)
        (output / 'west-frozen.yml').write_text(yaml.safe_dump(frozen, sort_keys=False))
        (output / 'source.json').write_text(json.dumps(record, indent=2) + '\n')
    return frozen


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, required=True)
    records = parser.add_mutually_exclusive_group()
    records.add_argument('--record', type=Path, help='Write source and dependency version records')
    records.add_argument('--verify', type=Path, help='Compare with a prior job\'s version records')
    args = parser.parse_args()
    setup(args.workspace.resolve(strict=True),
          args.record.resolve() if args.record else None,
          args.verify.resolve(strict=True) if args.verify else None)


if __name__ == '__main__':
    main()
