# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Verify and unpack the actual CLI candidate before native execution."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
import tarfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'release'))
import artifacts  # noqa: E402


def verify_architecture(data, target):
    arm = target.startswith('aarch64-')
    if 'linux' in target:
        valid = (len(data) >= 20 and data[:6] == b'\x7fELF\x02\x01'
                 and int.from_bytes(data[18:20], 'little') == (183 if arm else 62))
    elif 'windows' in target:
        offset = int.from_bytes(data[60:64], 'little') if len(data) >= 64 else len(data)
        valid = (data[:2] == b'MZ' and offset >= 64 and len(data) >= offset + 6
                 and data[offset:offset + 4] == b'PE\x00\x00'
                 and int.from_bytes(data[offset + 4:offset + 6], 'little') == (0xaa64 if arm else 0x8664))
    elif 'darwin' in target:
        valid = (len(data) >= 8 and data[:4] == b'\xcf\xfa\xed\xfe'
                 and int.from_bytes(data[4:8], 'little') == (0x100000c if arm else 0x1000007))
    else:
        valid = False
    if not valid:
        raise ValueError('executable format/architecture differs from artifact target')


def unpack(part, output, execute=True):
    artifacts.verify_checksums(part)
    record = json.loads((part / 'release-part.json').read_text())
    expected_os = 'windows' if sys.platform == 'win32' else 'darwin' if sys.platform == 'darwin' else 'linux'
    expected_arch = 'aarch64' if platform.machine().lower() in ('arm64', 'aarch64') else 'x86_64'
    if execute and (expected_os not in record['target'] or not record['target'].startswith(expected_arch + '-')):
        raise ValueError('artifact target differs from native runner')
    archives = list(part.glob('*.zip')) + list(part.glob('*.tar.gz'))
    if len(archives) != 1:
        raise ValueError('expected one CLI archive')
    output.mkdir(parents=True, exist_ok=True)
    archive = archives[0]
    if archive.suffix == '.zip':
        with zipfile.ZipFile(archive) as stream:
            for name in stream.namelist():
                artifacts.relative(name)
            stream.extractall(output)
    else:
        with tarfile.open(archive) as stream:
            stream.extractall(output, filter='data')
    root = output / 'meshbus'
    artifacts.verify_checksums(root)
    binary = root / record['binary']['file']
    data = binary.read_bytes()
    if hashlib.sha256(data).hexdigest() != record['binary']['sha256']:
        raise ValueError('archive binary differs from release part')
    # ARM hosts may transparently emulate x86 binaries; successful execution
    # alone must not qualify a mislabeled ARM archive.
    verify_architecture(data, record['target'])
    binary.chmod(0o755)
    if execute:
        version = subprocess.check_output([binary, '--version'], text=True).strip()
        if version != f"meshbus {record['version']}":
            raise ValueError(f'native CLI identity differs: {version}')
        subprocess.run([binary, '--help'], check=True)
        # Exercise offline crypto/package behavior in the distributed executable,
        # including a negative tamper case, without touching devices.
        fixture = Path(__file__).resolve().parents[1] / 'meshbus/tests/fixtures/dfota-v1.json'
        with tempfile.TemporaryDirectory() as temporary:
            package = Path(temporary)
            for name, data in json.loads(fixture.read_text()).items():
                artifacts.relative(name)
                (package / name).write_bytes(bytes.fromhex(data))
            command = [str(binary.resolve()), 'firmware', 'package', 'verify', str(package),
                       '--manifest-public-key', str(package / 'public-key'),
                       '--image-public-key', str(package / 'public-key')]
            subprocess.run(command, check=True)
            signature = package / 'manifest.sig'
            damaged = bytearray(signature.read_bytes())
            damaged[0] ^= 1
            signature.write_bytes(damaged)
            if subprocess.run(command, capture_output=True).returncode == 0:
                raise ValueError('native artifact accepted a tampered package')
        (output / 'native-validation.json').write_text(json.dumps({
            'target': record['target'], 'host': platform.platform(),
            'binary_sha256': record['binary']['sha256'],
            'checks': ['archive-checksums', 'binary-architecture', 'native-version', 'help', 'signed-fixture', 'tamper-rejection'],
            'hardware': 'not-run', 'result': 'passed'}, indent=2) + '\n')
    print(binary.resolve())
    return binary


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('part', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    unpack(args.part, args.output)
