# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Restore only the CLI schema project from the prepared source snapshot."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'release'))
import cli_source


def setup(workspace, snapshot):
    source = workspace / 'meshbus'
    _, project = cli_source.read(snapshot, source)
    proto = cli_source.destination(workspace, source, project)
    if not proto.exists():
        subprocess.run(['git', 'init', str(proto)], check=True)
        subprocess.run(['git', '-C', str(proto), 'remote', 'add', 'origin', project['url']], check=True)
        subprocess.run(['git', '-C', str(proto), '-c', 'credential.helper=',
                        'fetch', '--depth=1', 'origin', project['revision']], check=True)
        subprocess.run(['git', '-C', str(proto), 'checkout', '--detach', project['revision']], check=True)
    _, proto = cli_source.verify(snapshot, source, workspace)
    if output := os.environ.get('GITHUB_ENV'):
        with open(output, 'a') as stream:
            stream.write(f'MESHBUS_PROTO_ROOT={proto}\n')
    print(f'CLI schema: {project["revision"]} at {proto}')
    return proto


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('record', 'setup'))
    parser.add_argument('--workspace', type=Path, default=Path.cwd())
    parser.add_argument('--snapshot', type=Path, default=Path('snapshot'))
    args = parser.parse_args()
    if args.mode == 'record':
        import release
        cli_source.record(args.snapshot, release.provenance(args.workspace, args.workspace / 'meshbus'))
    else:
        setup(args.workspace.resolve(), args.snapshot.resolve())
