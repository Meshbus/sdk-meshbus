# SPDX-FileCopyrightText: 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0
"""Check local Markdown links and RST includes without an external site build.

URLs and anchors are not crawled. Fenced examples are not treated as links.
"""
import argparse
from pathlib import Path
import re
import subprocess
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[2]


def broken_links(path, root):
    # Authored website Markdown uses public routes, validated in web/dist.
    if path.suffix == '.md' and path.relative_to(root).as_posix().startswith('web/content/'):
        return []
    text = re.sub(r'^\s*(`{3,}|~{3,}).*?^\s*\1\s*$', '', path.read_text(), flags=re.M | re.S)
    links = re.findall(r'\]\(([^\s)]+)(?:\s+"[^"]*")?\)', text)
    links += re.findall(r'^\s*\.\. (?:include|literalinclude|image|figure)::\s+(\S+)', text, flags=re.M)
    errors = []
    for link in links:
        link = link.strip('<>')
        parsed = urlsplit(link)
        if parsed.scheme or link.startswith(('#', '//')) or not parsed.path:
            continue
        target = unquote(parsed.path)
        # Repository docs use leading slash for repository-relative assets.
        resolved = (root / target.lstrip('/') if target.startswith('/') else path.parent / target).resolve()
        if not resolved.exists():
            errors.append(f'{path.relative_to(root)}: {link}')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base')
    args = parser.parse_args()
    command = ['git', 'diff', '--name-only', '--diff-filter=ACMR', args.base, 'HEAD'] if args.base else ['git', 'ls-files']
    names = subprocess.check_output(command, cwd=ROOT, text=True).splitlines()
    errors = []
    for name in names:
        path = ROOT / name
        if path.suffix in ('.md', '.rst') and path.is_file():
            errors += broken_links(path, ROOT)
    if errors:
        raise SystemExit('\n'.join(errors))
    print('Local documentation links and includes passed (external URLs/anchors not checked).')


if __name__ == '__main__':
    main()
