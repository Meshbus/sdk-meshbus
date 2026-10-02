# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Install byte-pinned upstream tools; retain each upstream license in the image."""
import argparse
import hashlib
import pathlib
import shutil
import subprocess
import tempfile


def archive_matches(path, digest):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest() == digest


def download(url, digest, path, *, cache_dir=None):
    cached = cache_dir / f'{digest}.archive' if cache_dir is not None else None
    if cached is not None and cached.exists():
        shutil.copyfile(cached, path)
        if archive_matches(path, digest):
            print(f"Using verified cache for {url.rsplit('/', 1)[-1]}", flush=True)
            return
        print(f"Discarding corrupt cache for {url.rsplit('/', 1)[-1]}", flush=True)
        cached.unlink()
        path.unlink()
    print(f"Downloading {url.rsplit('/', 1)[-1]}", flush=True)
    try:
        subprocess.run(['curl', '--fail', '--location', '--silent', '--show-error',
                        '--retry', '3', '--retry-all-errors', '--connect-timeout', '30',
                        '--max-time', '900', '--output', str(path), url], check=True)
        if not archive_matches(path, digest):
            raise RuntimeError(f"checksum mismatch: {url}")
    except (OSError, RuntimeError, subprocess.CalledProcessError):
        path.unlink(missing_ok=True)
        raise
    if cached is not None:
        cache_dir.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(dir=cache_dir, prefix=f'.{digest}-', delete=False) as stream:
            pending = pathlib.Path(stream.name)
        try:
            shutil.copyfile(path, pending)
            pending.replace(cached)
        finally:
            pending.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--light', action='store_true', help='Install only actionlint and gitleaks')
    parser.add_argument('--prefix', type=pathlib.Path, default=pathlib.Path('/opt/tools'))
    parser.add_argument('--cache-dir', type=pathlib.Path,
                        help='Reuse downloaded archives after checking their pinned SHA-256')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        if not args.light:
            sdk = pathlib.Path('/opt/zephyr-sdk-1.0.1')
            base = 'https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v1.0.1/'
            archives = {
                'zephyr-sdk-1.0.1_linux-x86_64_minimal.tar.xz': ('ca9bc0ff66fafca1dac9d592a36d953cf16d096a9d09b1c0357f021cf9f6a7eb', pathlib.Path('/opt')),
                'toolchain_gnu_linux-x86_64_arm-zephyr-eabi.tar.xz': ('21b85981cb5a1818d9bc53d82af80f208946ec038b982ff1907287572ed3a634', sdk / 'gnu'),
                'toolchain_gnu_linux-x86_64_riscv64-zephyr-elf.tar.xz': ('01750834c471fbdb335c1b8b8aee17010a1968938957db85640c366235771a38', sdk / 'gnu'),
                'toolchain_gnu_linux-x86_64_x86_64-zephyr-elf.tar.xz': ('960fd8d8690ce130b141736e494a7bae7da1f96956f8da405de66429670b62e9', sdk / 'gnu'),
            }
            for name, (digest, destination) in archives.items():
                download(base + name, digest, root / name, cache_dir=args.cache_dir)
                destination.mkdir(parents=True, exist_ok=True)
                subprocess.run(['tar', 'xf', root / name, '-C', destination], check=True)
            subprocess.run([sdk / 'setup.sh', '-h', '-c'], check=True)
            # Ubuntu 24.04's ccache predates the minimum supported by sdk-zephyr.
            download('https://github.com/ccache/ccache/releases/download/v4.12.1/'
                     'ccache-4.12.1-linux-x86_64.tar.xz',
                     '742e6a6e17c0a060046874eece2949b221c228e1119698a4c6e0b096cbc87152',
                     root / 'ccache.tar.xz', cache_dir=args.cache_dir)
            ccache = pathlib.Path('/opt/tools/ccache')
            ccache.mkdir(parents=True)
            subprocess.run(['tar', 'xf', root / 'ccache.tar.xz', '--strip-components=1',
                            '-C', ccache], check=True)
            pathlib.Path('/usr/local/bin/ccache').symlink_to(ccache / 'ccache')
        tools = [
            ('grype', 'https://github.com/anchore/grype/releases/download/v0.119.0/grype_0.119.0_linux_amd64.tar.gz', '3fa2dc4b924621ab65404cf08d0b8438d896d80ab949c9d5a4ca283c36004c9b'),
            ('actionlint', 'https://github.com/rhysd/actionlint/releases/download/v1.7.12/actionlint_1.7.12_linux_amd64.tar.gz', '8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8'),
            ('gitleaks', 'https://github.com/gitleaks/gitleaks/releases/download/v8.30.1/gitleaks_8.30.1_linux_x64.tar.gz', '551f6fc83ea457d62a0d98237cbad105af8d557003051f41f3e7ca7b3f2470eb'),
            ('trivy', 'https://github.com/aquasecurity/trivy/releases/download/v0.74.0/trivy_0.74.0_Linux-64bit.tar.gz', '2ae6fe3ee734b7fdf11335663e18c75ea12dccc76062f09f164a3b0f8be4371a'),
        ]
        for name, url, digest in tools:
            if args.light and name not in ('actionlint', 'gitleaks'):
                continue
            download(url, digest, root / 'tool.tar.gz', cache_dir=args.cache_dir)
            destination = args.prefix / name
            destination.mkdir(parents=True, exist_ok=True)
            subprocess.run(['tar', 'xf', root / 'tool.tar.gz', '-C', destination], check=True)
            if not args.light:
                (pathlib.Path('/usr/local/bin') / name).symlink_to(destination / name)


if __name__ == "__main__":
    main()
