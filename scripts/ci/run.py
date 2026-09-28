# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Run CI layers using the repository's existing build and packaging commands."""
import argparse
import hashlib
import platform
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
WORKSPACE = ROOT.parent
OUT = WORKSPACE / 'ci-output'


def run(*args, cwd=ROOT, env=None, **kwargs):
    subprocess.run([str(a) for a in args], cwd=cwd, env=env, check=True, **kwargs)


def host(python_only=False):
    paths = ['scripts/ci/tests', 'scripts/tests', 'scripts/remote/tests']
    # Release fixtures consume Zephyr board metadata and native image tools.
    if not python_only:
        paths.append('scripts/release/tests')
    for path in paths:
        run(sys.executable, '-m', 'unittest', 'discover', '-s', path, '-q')
    if python_only:
        return
    crate = ROOT / 'scripts/meshbus/Cargo.toml'
    run('cargo', 'fmt', '--manifest-path', crate, '--check')
    run('cargo', 'clippy', '--locked', '--manifest-path', crate, '--all-targets', '--', '-D', 'warnings')
    run('cargo', 'test', '--locked', '--manifest-path', crate)


def twister(mode, shard):
    plan = json.loads((WORKSPACE / 'snapshot/plan.json').read_text())
    roots = plan['test_roots'] if mode == 'runtime' else plan['compile_roots']
    output = OUT / f'{mode}-{shard}'
    selected = WORKSPACE / 'snapshot' / f'{mode}-{shard}.json'
    command = ['west', 'twister', '--load-tests', selected, '--enable-slow', '--inline-logs',
               '--report-filtered', '--runtime-artifact-cleanup', 'pass', '-j', '2', '-O', output]
    for root in roots:
        command += ['-T', ROOT / root]
    if mode == 'compile':
        command += ['--build-only']
    run('ccache', '--zero-stats')
    try:
        run(*command, cwd=WORKSPACE)
        from test_plan import verify
        verify(json.loads(selected.read_text()), [output / 'twister.json'], mode == 'runtime')
    finally:
        run('ccache', '--show-stats')


def cli(target, candidate):
    env = os.environ.copy()
    if 'windows' in target and sys.platform != 'win32':
        env.setdefault('XWIN_VERSION', '17')
        env.setdefault('XWIN_SDK_VERSION', '10.0.26100')
        env.setdefault('XWIN_CRT_VERSION', '14.44.17.14')
        env.setdefault('XWIN_CACHE_DIR', str(WORKSPACE / 'xwin-cache'))
        env['CARGO'] = str(ROOT / 'scripts/ci/cargo-xwin.sh')
    if target == 'aarch64-unknown-linux-gnu':
        env.update(CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER='aarch64-linux-gnu-gcc',
                   CC_aarch64_unknown_linux_gnu='aarch64-linux-gnu-gcc',
                   CXX_aarch64_unknown_linux_gnu='aarch64-linux-gnu-g++',
                   PKG_CONFIG_ALLOW_CROSS='1',
                   PKG_CONFIG_LIBDIR='/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig')
    run(sys.executable, ROOT / 'scripts/release/release.py', 'cli', '--workspace', WORKSPACE,
        '--target', target, '--output', OUT / 'parts', '--cargo-target-dir', WORKSPACE / 'cargo-build',
        *([] if candidate else ['--development']), cwd=WORKSPACE, env=env)
    part = OUT / 'parts/cli' / target
    tools = {'builder_image': env.get('BUILDER_IMAGE'), 'host': platform.platform(),
             'rustc': subprocess.check_output(['rustc', '-vV'], text=True),
             'xwin_selection': {key: env.get(key) for key in
                               ('XWIN_VERSION', 'XWIN_SDK_VERSION', 'XWIN_CRT_VERSION')}}
    if sys.platform == 'darwin':
        tools['macos'] = subprocess.check_output(['sw_vers'], text=True)
        tools['xcode'] = subprocess.check_output(['xcodebuild', '-version'], text=True)
        tools['apple_sdk'] = subprocess.check_output(['xcrun', '--show-sdk-version'], text=True).strip()
    if 'windows' in target and sys.platform != 'win32':
        cache = Path(env['XWIN_CACHE_DIR'])
        tools['xwin_files'] = {}
        for path in sorted(cache.rglob('*')):
            if path.is_file():
                with path.open('rb') as stream:
                    tools['xwin_files'][path.relative_to(cache).as_posix()] = hashlib.file_digest(stream, 'sha256').hexdigest()
        if not tools['xwin_files']:
            raise ValueError('Windows cross build has no SDK/CRT provenance')
    (part / 'tool-environment.json').write_text(json.dumps(tools, indent=2) + '\n')
    sys.path.insert(0, str(ROOT / 'scripts/release'))
    import artifacts
    artifacts.checksums(part)



def product(board, candidate):
    if candidate:
        run('west', 'release', 'build', '--workspace', WORKSPACE, '--target', board,
            '--build-root', WORKSPACE / 'product-build', '--output', OUT / 'firmware-parts', cwd=WORKSPACE)
        archives = list((OUT / 'firmware-parts').rglob('*-edk.tar.xz'))
        for index, archive in enumerate(archives):
            # The CLI emits compiler output before its JSON summary.
            with (OUT / f'edk-qualification-{index}.log').open('w') as report:
                run(os.environ['MESHBUS_CLI'], 'edk', 'qualify', archive,
                    '--zephyr-sdk', os.environ['ZEPHYR_SDK_INSTALL_DIR'],
                    '--packages-root', ROOT / 'samples/subsys/llext/apps/snake',
                    '--packages-root', ROOT / 'samples/subsys/llext/apps/cxx_hello',
                    '--packages-output', OUT / f'extensions-{index}', cwd=WORKSPACE,
                    stdout=report, stderr=subprocess.STDOUT)
    else:
        run('west', 'build', '--sysbuild', '-b', board, ROOT / 'apps/meshbus',
            '-d', WORKSPACE / 'product-build', cwd=WORKSPACE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('layer', choices=('host', 'runtime', 'compile', 'cli', 'product'))
    parser.add_argument('--target')
    parser.add_argument('--shard', type=int, default=0)
    parser.add_argument('--candidate', action='store_true')
    parser.add_argument('--python-only', action='store_true')
    args = parser.parse_args()
    OUT.mkdir(exist_ok=True)
    if args.layer == 'host':
        host(args.python_only)
    elif args.layer in ('runtime', 'compile'):
        twister(args.layer, args.shard)
    elif args.layer == 'cli':
        cli(args.target, args.candidate)
    else:
        product(args.target, args.candidate)


if __name__ == '__main__':
    main()
