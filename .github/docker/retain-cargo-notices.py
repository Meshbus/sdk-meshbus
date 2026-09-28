# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Preserve the installed cross tool's fetched crate notices before cache cleanup."""
import json
from pathlib import Path
import shutil
import tomllib

output = Path('/opt/builder/cargo-licenses')
output.mkdir()
records = []
for root in sorted(Path('/opt/cargo/registry/src').glob('*/*')):
    manifest = root / 'Cargo.toml'
    if not manifest.exists():
        continue
    package = tomllib.loads(manifest.read_text())['package']
    target = output / root.name
    target.mkdir()
    shutil.copy2(manifest, target / 'Cargo.toml')
    for path in root.iterdir():
        if path.name.lower().startswith(('license', 'copying', 'copyright', 'notice')):
            if path.is_dir():
                shutil.copytree(path, target / path.name)
            elif path.is_file():
                shutil.copy2(path, target / path.name)
    records.append({key: package.get(key) for key in ('name', 'version', 'license', 'license-file', 'repository')})
(output / 'inventory.json').write_text(json.dumps(records, indent=2) + '\n')
