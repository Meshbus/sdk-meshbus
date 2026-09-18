# SPDX-License-Identifier: Apache-2.0
"""Discover application profiles using Zephyr's hardware model, not filename guessing."""
import argparse
import json
import os
from pathlib import Path
import re
import sys


def discover(boards, *, zephyr_base=None, board_roots=(), soc_roots=()):
    boards = Path(boards).resolve()
    repository = boards.parents[2]
    zephyr = Path(zephyr_base or os.environ.get("ZEPHYR_BASE", repository.parent / "zephyr")).resolve()
    if not (zephyr / "scripts/list_boards.py").is_file():
        raise ValueError(f"Zephyr board discovery is unavailable: {zephyr}; set ZEPHYR_BASE")
    sys.path.insert(0, str(zephyr / "scripts"))
    import list_boards

    files = []
    for path in sorted(boards.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"symlink in board profiles: {path}")
        if not path.is_file() or path.suffix not in {".conf", ".overlay"}:
            continue
        parts = path.relative_to(boards).parts
        if len(parts) != 3 or not all(re.fullmatch(r"[a-z0-9_]+", part) for part in parts[:2]):
            raise ValueError(f"profile must use boards/<vendor>/<board>/<file>: {path}")
        files.append(path)
    primary = [p for p in files if p.suffix == ".conf" and not p.stem.endswith("_mcuboot")]
    if not primary:
        raise ValueError(f"no Meshbus APP profiles found under {boards}")
    args = argparse.Namespace(board=None, board_dir=[],
                              board_roots=[repository, zephyr, *map(Path, board_roots)],
                              soc_roots=[repository, zephyr, *map(Path, soc_roots)])
    hardware = list_boards.find_v2_boards(args)
    result, used, identities, output_names = [], set(), set(), set()
    for profile in primary:
        vendor, name, _ = profile.relative_to(boards).parts
        board = hardware.get(name)
        if board is None or board.vendor != vendor:
            raise ValueError(f"unknown board or vendor mismatch: {vendor}/{name}")
        candidates = [f"{name}/{q}" for q in list_boards.board_v2_qualifiers(board)
                      if not any(part.startswith("mb_") for part in q.split("/"))]
        matches = [target for target in candidates if target.replace("/", "_") == profile.stem]
        if len(matches) != 1:
            raise ValueError(f"APP profile must match exactly one ordinary Zephyr target: {profile}")
        target = matches[0]
        if target in identities:
            raise ValueError(f"duplicate target: {target}")
        if profile.stem in output_names:
            raise ValueError(f"normalized target output collision: {profile.stem}")
        identities.add(target)
        output_names.add(profile.stem)
        companions = [profile, profile.with_suffix(".overlay"),
                      profile.with_name(profile.stem + "_mcuboot.conf"),
                      profile.with_name(profile.stem + "_mcuboot.overlay")]
        used.update(companions)
        result.append({"id": name, "board": target, "vendor": vendor,
                       "profile": str(profile)})
    orphaned = set(files) - used
    if orphaned:
        raise ValueError(f"configuration has no matching APP profile: {sorted(orphaned)[0]}")
    return sorted(result, key=lambda item: item["board"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boards", type=Path, required=True)
    parser.add_argument("--zephyr-base", type=Path)
    parser.add_argument("--board-root", type=Path, action="append", default=[])
    parser.add_argument("--soc-root", type=Path, action="append", default=[])
    parser.add_argument("--target", help="Print the APP profile for one complete target")
    args = parser.parse_args()
    try:
        profiles = discover(args.boards, zephyr_base=args.zephyr_base,
                            board_roots=args.board_root, soc_roots=args.soc_root)
        if args.target:
            matches = [p for p in profiles if p["board"] == args.target]
            if not matches:
                raise ValueError(f"unsupported Meshbus target: {args.target}")
            print(matches[0]["profile"])
        else:
            print(json.dumps(profiles, indent=2))
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"board profiles: {error}\n")


if __name__ == "__main__":
    main()
