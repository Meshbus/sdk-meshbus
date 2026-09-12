# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

"""
West extension command for ZUI (Zephyr UI) utilities.

This is the parent command that groups all ZUI-related subcommands:
- zui icons: Convert PNG icons to C source code
- zui xbms: Convert PNG bitmaps to header-only raw XBM arrays

Usage:
    west zui icons <input_directory> <output_directory> [options]
    west zui xbms <input_directory> <output_directory> [options]
"""

import argparse
import os
import re
import sys
from pathlib import Path
from typing import List, Optional, Tuple

from west.commands import WestCommand
from west import log

# Try to import icon module
try:
    from assets.icon import (
        file2image,
        file2images,
        file2image_raw,
        gif_frame_count,
        gif_frame_rate,
        is_file_an_icon,
    )
except ImportError:
    # Add parent directory to path for development
    sys.path.insert(0, str(Path(__file__).parent))
    from assets.icon import (
        file2image,
        file2images,
        file2image_raw,
        gif_frame_count,
        gif_frame_rate,
        is_file_an_icon,
    )


# C code templates for ZUI icon generation (Zephyr style)
ICONS_TEMPLATE_H_HEADER = '''#pragma once

#include <{include_path}>

'''

ICONS_TEMPLATE_H_ICON_NAME = 'extern const struct zui_icon {name};\n'

ICONS_TEMPLATE_C_HEADER = '''#include "{assets_filename}.h"

'''

ICONS_TEMPLATE_C_FRAME = 'static const uint8_t {name}[] = {data};\n'
ICONS_TEMPLATE_C_DATA = 'static const uint8_t *const {name}[] = {data};\n'
ICONS_TEMPLATE_C_SIZES = 'static const uint16_t {name}_sizes[] = {data};\n'
ICONS_TEMPLATE_C_ICONS = (
    'const struct zui_icon {name} = {{\n'
    '\t.width = {width}, .height = {height}, .frame_count = {frame_count}, '
    '.frame_rate = {frame_rate}, .frames = _{name}, .frame_sizes = _{name}_sizes\n}};\n'
)

XBMS_TEMPLATE_H_HEADER = '''#pragma once

#include <{include_path}>

'''

XBMS_TEMPLATE_H_BITMAP = 'static const uint8_t {name}[] = {data};\n'

# Maximum supported icon dimensions
MAX_IMAGE_WIDTH = 2**16 - 1
MAX_IMAGE_HEIGHT = 2**16 - 1
MAX_ICON_FRAME_SIZE = 2**16 - 1
MAX_ICON_FRAME_COUNT = 2**8 - 1
MAX_ICON_FRAME_RATE = 2**8 - 1


class Zui(WestCommand):
    """West command for ZUI utilities with subcommands."""

    def __init__(self):
        super().__init__(
            'zui',
            'ZUI (Zephyr UI) utilities',
            'ZUI asset generation and management tools.',
            accepts_unknown_args=False,
        )

    def do_add_parser(self, parser_adder):
        parser = parser_adder.add_parser(
            self.name,
            help=self.help,
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description=self.description,
        )

        # Add subcommands
        subparsers = parser.add_subparsers(dest='subcommand', required=True,
                                            help='ZUI subcommands')

        # Icons subcommand
        icons_parser = subparsers.add_parser(
            'icons',
            help='Generate icon assets from PNG and GIF files',
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description='Convert PNG and GIF icons to C source code for ZUI GUI library.',
            epilog='''
Examples:
  west zui icons assets/icons build/generated
      Convert all icons in assets/icons to build/generated/assets_icons.{c,h}

  west zui icons assets/icons build/generated --filename my_icons
      Output to my_icons.c and my_icons.h

  west zui icons assets/icons build/generated --no-compress
      Generate uncompressed icon data

Directory structure:
  For static icons, place PNG or single-frame GIF files directly in the input directory
  or subdirectories.
  For animated GIF icons, place the GIF file directly in the input directory or
  subdirectories. The GIF frame duration metadata is converted to the ZUI frame_rate.
  For animated icons, create a directory with:
    - Multiple frame_XX.png files (numbered sequentially)
    - A 'frame_rate' text file containing the FPS value

Naming conventions:
  - Static icons: I_<filename> (e.g., I_Button_7x7 from Button_7x7.png)
  - Animated GIF icons: A_<filename> (e.g., A_Loading_24 from Loading_24.gif)
  - Animated icons: A_<dirname> (e.g., A_Loading_24 from Loading_24/ directory)
''',
        )

        icons_parser.add_argument(
            'input_directory',
            help='Source directory containing PNG icons',
        )
        icons_parser.add_argument(
            'output_directory',
            help='Output directory for generated C files',
        )
        icons_parser.add_argument(
            '--filename', '-f',
            default='assets_icons',
            help='Base filename for output files (default: assets_icons)',
        )
        icons_parser.add_argument(
            '--no-compress',
            action='store_true',
            help='Disable heatshrink compression',
        )
        icons_parser.add_argument(
            '--include-path',
            default='zui/zui.h',
            help='Public ZUI include path for generated headers (default: zui/zui.h)',
        )
        icons_parser.add_argument(
            '--include-path-internal',
            default=None,
            help='Deprecated and ignored; generated icons only require the public ZUI header',
        )

        # XBM bitmaps subcommand
        xbms_parser = subparsers.add_parser(
            'xbms',
            help='Generate raw XBM bitmap arrays from PNG files',
            formatter_class=argparse.RawDescriptionHelpFormatter,
            description='Convert PNG bitmap assets to a header-only raw XBM array file.',
            epilog='''
Examples:
  west zui xbms assets/xbms assets --filename assets_xbms
      Convert all PNG files in assets/xbms to assets/assets_xbms.h

Naming conventions:
  - Bitmaps: B_<filename> (e.g., B_battery_46x28 from battery_46x28.png)
''',
        )

        xbms_parser.add_argument(
            'input_directory',
            help='Source directory containing PNG bitmap assets',
        )
        xbms_parser.add_argument(
            'output_directory',
            help='Output directory for the generated header file',
        )
        xbms_parser.add_argument(
            '--filename', '-f',
            default='assets_xbms',
            help='Base filename for output header (default: assets_xbms)',
        )
        xbms_parser.add_argument(
            '--include-path',
            default='zui/zui.h',
            help='Include path for generated headers (default: zui/zui.h)',
        )

        return parser

    def do_run(self, args, unknown_args):
        if args.subcommand == 'icons':
            return self._run_icons(args)
        if args.subcommand == 'xbms':
            return self._run_xbms(args)
        else:
            log.die(f'Unknown subcommand: {args.subcommand}')

    def _run_icons(self, args):
        """Run the icons subcommand."""
        input_dir = Path(args.input_directory)
        output_dir = Path(args.output_directory)

        if not input_dir.is_dir():
            log.die(f'Input directory does not exist: {input_dir}')

        # Create output directory if needed
        output_dir.mkdir(parents=True, exist_ok=True)

        log.inf(f'Converting icons from {input_dir} to {output_dir}')

        try:
            self._convert_icons(
                input_dir,
                output_dir,
                args.filename,
                compress=not args.no_compress,
                include_path=args.include_path,
                include_path_internal=args.include_path_internal,
            )
        except Exception as e:
            log.die(f'Icon conversion failed: {e}')

        log.inf('Icon conversion complete')
        return 0

    def _run_xbms(self, args):
        """Run the xbms subcommand."""
        input_dir = Path(args.input_directory)
        output_dir = Path(args.output_directory)

        if not input_dir.is_dir():
            log.die(f'Input directory does not exist: {input_dir}')

        output_dir.mkdir(parents=True, exist_ok=True)

        log.inf(f'Converting XBM bitmaps from {input_dir} to {output_dir}')

        try:
            self._convert_xbms(
                input_dir,
                output_dir,
                args.filename,
                include_path=args.include_path,
            )
        except Exception as e:
            log.die(f'XBM bitmap conversion failed: {e}')

        log.inf('XBM bitmap conversion complete')
        return 0

    def _icon2header(self, file: Path, compress: bool) -> Tuple[int, int, str, int]:
        """
        Convert a PNG file to C array data.

        Args:
            file: Path to PNG file
            compress: Whether to use compression

        Returns:
            Tuple of (width, height, c_array_string, data_size)
        """
        image = file2image(str(file), compress=compress)

        if image.width > MAX_IMAGE_WIDTH or image.height > MAX_IMAGE_HEIGHT:
            raise ValueError(
                f'Image {file} is too large ({image.width}x{image.height}), '
                f'maximum is {MAX_IMAGE_WIDTH}x{MAX_IMAGE_HEIGHT}'
            )

        data_size = len(image.data)
        if data_size > MAX_ICON_FRAME_SIZE:
            raise ValueError(
                f'Encoded image {file} is too large ({data_size} bytes), '
                f'maximum frame size is {MAX_ICON_FRAME_SIZE} bytes'
            )

        return image.width, image.height, image.data_as_carray(), data_size

    def _xbm2header(self, file: Path) -> Tuple[int, int, str, int]:
        """
        Convert a PNG file to raw XBM C array data.

        Args:
            file: Path to PNG file

        Returns:
            Tuple of (width, height, c_array_string, data_size)
        """
        image = file2image_raw(str(file))

        if image.width > MAX_IMAGE_WIDTH or image.height > MAX_IMAGE_HEIGHT:
            raise ValueError(
                f'Bitmap {file} is too large ({image.width}x{image.height}), '
                f'maximum is {MAX_IMAGE_WIDTH}x{MAX_IMAGE_HEIGHT}'
            )

        data_size = len(image.data)
        if data_size > MAX_ICON_FRAME_SIZE:
            raise ValueError(
                f'Encoded bitmap {file} is too large ({data_size} bytes), '
                f'maximum frame size is {MAX_ICON_FRAME_SIZE} bytes'
            )

        return image.width, image.height, image.data_as_carray(), data_size

    def _image2header(self, image, source: Path) -> Tuple[int, int, str, int]:
        """Convert an already decoded image frame to C array data."""
        if image.width > MAX_IMAGE_WIDTH or image.height > MAX_IMAGE_HEIGHT:
            raise ValueError(
                f'Image {source} is too large ({image.width}x{image.height}), '
                f'maximum is {MAX_IMAGE_WIDTH}x{MAX_IMAGE_HEIGHT}'
            )

        data_size = len(image.data)
        if data_size > MAX_ICON_FRAME_SIZE:
            raise ValueError(
                f'Encoded image {source} is too large ({data_size} bytes), '
                f'maximum frame size is {MAX_ICON_FRAME_SIZE} bytes'
            )

        return image.width, image.height, image.data_as_carray(), data_size

    def _sanitize_name(self, name: str) -> str:
        """Convert a filename to a basic identifier fragment.

        Note: This is not sufficient for final exported symbol naming.
        Use _make_icon_identifier() for exported names.
        """
        name = name.rsplit('.', 1)[0]
        return name.replace('-', '_').replace(' ', '_')

    def _rewrite_banned_tokens(self, s: str) -> str:
        """Rewrite banned legacy tokens into neutral names.

        This prevents Flipper/Furi/Dolphin traces from leaking into generated
        C identifiers.
        """
        rules = (
            (r'(?i)dolphin', 'Mascot'),
            (r'(?i)flipper', 'Device'),
            (r'(?i)furi', 'Core'),
        )

        out = s
        for pattern, repl in rules:
            out = re.sub(pattern, repl, out)
        return out

    def _sanitize_c_identifier_fragment(self, s: str) -> str:
        """Sanitize an arbitrary string into a C identifier fragment."""
        # Replace any non-alphanumeric characters with underscores.
        s = re.sub(r'[^0-9A-Za-z]+', '_', s)
        # Collapse multiple underscores.
        s = re.sub(r'_+', '_', s)
        # Strip leading/trailing underscores.
        s = s.strip('_')
        # Ensure it doesn't start with a digit.
        if s and s[0].isdigit():
            s = f'_{s}'
        return s

    def _assert_no_legacy_traces(self, identifier: str) -> None:
        if re.search(r'(?i)(dolphin|flipper|furi)', identifier):
            raise ValueError(f'Generated identifier contains banned token: {identifier}')

    def _make_unique_name(self, base_name: str, name_counts: dict) -> str:
        """Resolve collisions with a readable numeric suffix (_2, _3, ...)."""
        count = name_counts.get(base_name, 0) + 1
        name_counts[base_name] = count
        return base_name if count == 1 else f'{base_name}_{count}'

    def _make_icon_identifier(self, prefix: str, raw: str) -> str:
        """Create an exported icon identifier (I_* or A_*), trace-free."""
        # Pre-rewrite (catches DolphinFoo, FlipperFoo, etc.).
        rewritten = self._rewrite_banned_tokens(raw)
        # Basic sanitization.
        fragment = self._sanitize_c_identifier_fragment(rewritten)
        # Post-rewrite (catches odd split tokens introduced by sanitization).
        fragment = self._rewrite_banned_tokens(fragment)
        fragment = self._sanitize_c_identifier_fragment(fragment)

        ident = f'{prefix}{fragment}'
        self._assert_no_legacy_traces(ident)
        return ident

    def _sanitize_assets_basename(self, filename: str) -> str:
        """Sanitize output basename used for generated C/H filenames/includes."""
        # We keep this simple and strict: only allow alnum/underscore.
        rewritten = self._rewrite_banned_tokens(filename)
        base = self._sanitize_c_identifier_fragment(rewritten)
        if not base:
            raise ValueError('Output filename is empty after sanitization')
        self._assert_no_legacy_traces(base)
        return base

    def _convert_icons(
        self,
        input_dir: Path,
        output_dir: Path,
        filename: str,
        compress: bool,
        include_path: str,
        include_path_internal: Optional[str],
    ) -> None:
        """
        Convert all icons in input_dir to C source files.

        Args:
            input_dir: Source directory with PNG icons
            output_dir: Output directory for C files
            filename: Base filename for output files
            compress: Whether to compress icon data
            include_path: Include path for public header
            include_path_internal: Deprecated and ignored
        """
        # List to store icon metadata: (name, width, height, frame_rate, frame_count)
        icons: List[Tuple[str, int, int, int, int]] = []

        # Sanitize the output basename so generated includes don't leak legacy tokens.
        filename = self._sanitize_assets_basename(filename)

        # Name counts for collision resolution (readable numeric suffixes).
        name_counts: dict = {}

        # Open output files
        c_file = output_dir / f'{filename}.c'
        h_file = output_dir / f'{filename}.h'

        with open(c_file, 'w', newline='\n') as icons_c:
            # Write C file header
            header = ICONS_TEMPLATE_C_HEADER.format(
                assets_filename=filename,
            )
            icons_c.write(header)

            # Collect generation jobs first, then sort by relative path.
            # This keeps numeric suffix collision resolution reasonably deterministic.
            jobs = []

            for dirpath, dirnames, dir_filenames in os.walk(input_dir):
                dirpath = Path(dirpath)
                dirnames.sort()
                dir_filenames.sort()

                if not dir_filenames:
                    continue

                if 'frame_rate' in dir_filenames:
                    rel = dirpath.relative_to(input_dir).as_posix()
                    jobs.append((rel + '/', 'animation', dirpath, list(dir_filenames)))
                    continue

                for fn in dir_filenames:
                    if not is_file_an_icon(fn):
                        continue
                    filepath = dirpath / fn
                    rel = filepath.relative_to(input_dir).as_posix()
                    if filepath.suffix.lower() == '.gif' and gif_frame_count(str(filepath)) > 1:
                        jobs.append((rel, 'gif_animation', filepath, None))
                    else:
                        jobs.append((rel, 'static', filepath, None))

            jobs.sort(key=lambda j: j[0])

            for _, kind, a, b in jobs:
                if kind == 'animation':
                    dirpath = a
                    dir_filenames = b
                    base = self._make_icon_identifier('A_', dirpath.name)
                    icon_name = self._make_unique_name(base, name_counts)
                    self._process_animation(
                        dirpath, dir_filenames, icons_c, icons, compress, icon_name
                    )
                elif kind == 'gif_animation':
                    filepath = a
                    base = self._make_icon_identifier('A_', filepath.stem)
                    icon_name = self._make_unique_name(base, name_counts)
                    self._process_gif_animation(filepath, icons_c, icons, compress, icon_name)
                else:
                    filepath = a
                    base = self._make_icon_identifier('I_', filepath.stem)
                    icon_name = self._make_unique_name(base, name_counts)
                    self._process_static_icon(filepath, icons_c, icons, compress, icon_name)

            # Write Icon structures
            icons_c.write('\n')
            for name, width, height, frame_rate, frame_count in icons:
                icons_c.write(
                    ICONS_TEMPLATE_C_ICONS.format(
                        name=name,
                        width=width,
                        height=height,
                        frame_rate=frame_rate,
                        frame_count=frame_count,
                    )
                )

        # Write header file
        with open(h_file, 'w', newline='\n') as icons_h:
            header = ICONS_TEMPLATE_H_HEADER.format(include_path=include_path)
            icons_h.write(header)

            for name, _, _, _, _ in icons:
                icons_h.write(ICONS_TEMPLATE_H_ICON_NAME.format(name=name))

        log.inf(f'Generated {len(icons)} icons: {c_file.name}, {h_file.name}')

    def _convert_xbms(
        self,
        input_dir: Path,
        output_dir: Path,
        filename: str,
        include_path: str,
    ) -> None:
        """
        Convert all PNG bitmaps in input_dir to a header-only XBM asset file.

        Args:
            input_dir: Source directory with PNG bitmaps
            output_dir: Output directory for the header file
            filename: Base filename for the output header
            include_path: Include path for the generated header
        """
        filename = self._sanitize_assets_basename(filename)
        h_file = output_dir / f'{filename}.h'
        name_counts: dict = {}
        generated = 0

        jobs = []
        for dirpath, dirnames, filenames in os.walk(input_dir):
            dirpath = Path(dirpath)
            dirnames.sort()
            filenames.sort()

            for fn in filenames:
                filepath = dirpath / fn
                if filepath.suffix.lower() != '.png':
                    continue
                jobs.append((filepath.relative_to(input_dir).as_posix(), filepath))

        jobs.sort(key=lambda job: job[0])

        with open(h_file, 'w', newline='\n') as xbms_h:
            xbms_h.write(XBMS_TEMPLATE_H_HEADER.format(include_path=include_path))

            for _, filepath in jobs:
                base = self._make_icon_identifier('B_', filepath.stem)
                bitmap_name = self._make_unique_name(base, name_counts)
                width, height, data, _ = self._xbm2header(filepath)
                log.dbg(f'Processing bitmap: {bitmap_name} ({width}x{height})')
                xbms_h.write(XBMS_TEMPLATE_H_BITMAP.format(name=bitmap_name, data=data))
                generated += 1

        log.inf(f'Generated {generated} XBM bitmaps: {h_file.name}')

    def _process_animation(
        self,
        dirpath: Path,
        filenames: List[str],
        icons_c,
        icons: List[Tuple[str, int, int, int, int]],
        compress: bool,
        icon_name: str,
    ) -> None:
        """Process an animation directory."""
        log.dbg(f'Processing animation: {icon_name}')

        width = None
        height = None
        frame_count = 0
        frame_rate = 0
        frame_names = []
        frame_sizes = []

        for filename in sorted(filenames):
            filepath = dirpath / filename

            if filename == 'frame_rate':
                frame_rate = int(filepath.read_text().strip())
                continue

            if not is_file_an_icon(filename):
                continue

            log.dbg(f'  Frame {frame_count}: {filename}')
            temp_width, temp_height, data, data_size = self._icon2header(filepath, compress)

            if width is None:
                width = temp_width
            if height is None:
                height = temp_height

            # All frames must have same dimensions
            if width != temp_width or height != temp_height:
                raise ValueError(
                    f'Animation frame {filepath} has different dimensions '
                    f'({temp_width}x{temp_height}) than first frame ({width}x{height})'
                )

            frame_name = f'_{icon_name}_{frame_count}'
            frame_names.append(frame_name)
            frame_sizes.append(str(data_size))
            icons_c.write(ICONS_TEMPLATE_C_FRAME.format(name=frame_name, data=data))
            frame_count += 1

        if frame_count == 0:
            log.wrn(f'Animation {icon_name} has no frames, skipping')
            return
        if frame_count > MAX_ICON_FRAME_COUNT:
            raise ValueError(
                f'Animation {icon_name} has too many frames ({frame_count}), '
                f'maximum is {MAX_ICON_FRAME_COUNT}'
            )

        if frame_rate <= 0:
            log.wrn(f'Animation {icon_name} has invalid frame_rate, defaulting to 1')
            frame_rate = 1
        if frame_rate > MAX_ICON_FRAME_RATE:
            raise ValueError(
                f'Animation {icon_name} frame_rate {frame_rate} exceeds '
                f'maximum {MAX_ICON_FRAME_RATE}'
            )

        # Write frame pointer array
        icons_c.write(
            ICONS_TEMPLATE_C_DATA.format(
                name=f'_{icon_name}',
                data='{' + ','.join(frame_names) + '}'
            )
        )
        icons_c.write(
            ICONS_TEMPLATE_C_SIZES.format(
                name=f'_{icon_name}',
                data='{' + ','.join(frame_sizes) + '}'
            )
        )
        icons_c.write('\n')

        icons.append((icon_name, width, height, frame_rate, frame_count))

    def _process_gif_animation(
        self,
        filepath: Path,
        icons_c,
        icons: List[Tuple[str, int, int, int, int]],
        compress: bool,
        icon_name: str,
    ) -> None:
        """Process one animated GIF icon file."""
        log.dbg(f'Processing GIF animation: {icon_name}')

        frames = file2images(str(filepath), compress=compress)
        frame_rate = gif_frame_rate(str(filepath))
        frame_count = len(frames)

        if frame_count > MAX_ICON_FRAME_COUNT:
            raise ValueError(
                f'Animation {filepath} has too many frames ({frame_count}), '
                f'maximum is {MAX_ICON_FRAME_COUNT}'
            )
        if frame_rate <= 0:
            log.wrn(f'GIF animation {icon_name} has invalid frame_rate, defaulting to 1')
            frame_rate = 1
        if frame_rate > MAX_ICON_FRAME_RATE:
            raise ValueError(
                f'GIF animation {filepath} frame_rate {frame_rate} exceeds '
                f'maximum {MAX_ICON_FRAME_RATE}'
            )

        width = None
        height = None
        frame_names = []
        frame_sizes = []

        for frame_idx, image in enumerate(frames):
            temp_width, temp_height, data, data_size = self._image2header(image, filepath)

            if width is None:
                width = temp_width
            if height is None:
                height = temp_height

            if width != temp_width or height != temp_height:
                raise ValueError(
                    f'GIF frame {frame_idx} in {filepath} has different dimensions '
                    f'({temp_width}x{temp_height}) than first frame ({width}x{height})'
                )

            frame_name = f'_{icon_name}_{frame_idx}'
            frame_names.append(frame_name)
            frame_sizes.append(str(data_size))
            icons_c.write(ICONS_TEMPLATE_C_FRAME.format(name=frame_name, data=data))

        icons_c.write(
            ICONS_TEMPLATE_C_DATA.format(
                name=f'_{icon_name}',
                data='{' + ','.join(frame_names) + '}'
            )
        )
        icons_c.write(
            ICONS_TEMPLATE_C_SIZES.format(
                name=f'_{icon_name}',
                data='{' + ','.join(frame_sizes) + '}'
            )
        )
        icons_c.write('\n')

        icons.append((icon_name, width, height, frame_rate, frame_count))

    def _process_static_icon(
        self,
        filepath: Path,
        icons_c,
        icons: List[Tuple[str, int, int, int, int]],
        compress: bool,
        icon_name: str,
    ) -> None:
        """Process one static icon file."""
        log.dbg(f'Processing icon: {icon_name}')

        width, height, data, data_size = self._icon2header(filepath, compress)

        frame_name = f'_{icon_name}_0'
        icons_c.write(ICONS_TEMPLATE_C_FRAME.format(name=frame_name, data=data))
        icons_c.write(
            ICONS_TEMPLATE_C_DATA.format(
                name=f'_{icon_name}',
                data='{' + frame_name + '}'
            )
        )
        icons_c.write(
            ICONS_TEMPLATE_C_SIZES.format(
                name=f'_{icon_name}',
                data='{' + str(data_size) + '}'
            )
        )
        icons_c.write('\n')

        # Static icons: frame_rate=0, frame_count=1
        icons.append((icon_name, width, height, 0, 1))
