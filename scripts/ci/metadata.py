# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Fast syntax and ownership checks; Twister remains the test schema authority."""
from pathlib import Path
import subprocess
import tomllib

import yaml

import impact

ROOT = Path(__file__).resolve().parents[2]


class UniqueLoader(yaml.SafeLoader):
    pass


def mapping(loader, node, deep=False):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in result:
            raise ValueError(f'duplicate YAML key: {key} at line {key_node.start_mark.line + 1}')
        result[key] = loader.construct_object(value_node, deep=deep)
    return result


UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, mapping)


def main():
    # Validate impact ownership even on full runs, which bypass narrowing.
    impact.load_policy(ROOT)
    names = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0')
    scenarios = {}
    count = 0
    for name in names:
        path = ROOT / name
        if not path.is_file():
            continue
        if path.suffix in ('.yaml', '.yml'):
            data = yaml.load(path.read_text(), Loader=UniqueLoader)
            if path.name in ('testcase.yaml', 'sample.yaml'):
                if not isinstance(data, dict) or not isinstance(data.get('tests'), dict) or not data['tests']:
                    raise ValueError(f'{name}: missing tests')
                for scenario in data['tests']:
                    if scenario in scenarios:
                        raise ValueError(f'duplicate scenario {scenario}: {name}, {scenarios[scenario]}')
                    scenarios[scenario] = name
            count += 1
        elif path.suffix == '.toml':
            tomllib.loads(path.read_text())
            count += 1
    # Product board identities are checked by release.py matrix in prepare,
    # using the frozen Zephyr board inventory rather than guessed names here.
    print(f'Metadata: {count} documents, {len(scenarios)} unique scenarios')


if __name__ == '__main__':
    main()
