# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

"""
Icon conversion module for ZUI assets.

Converts PNG images and GIF frames to XBM format with optional heatshrink compression,
matching the Flipper Zero icon format for compatibility with the ZUI GUI library.

Compression format:
- Header byte 0x01: compressed data follows
  - Next 2 bytes: compressed size (little-endian)
  - Remaining bytes: heatshrink-compressed XBM data
- Header byte 0x00: uncompressed XBM data follows directly

XBM format: 1-bit monochrome, row-major, LSB first, rows padded to byte boundary.
"""

import logging
import subprocess
from typing import List, Tuple

# Supported input formats
ICONS_SUPPORTED_FORMATS = ['png', 'gif']

# Heatshrink compression parameters (matching Flipper Zero)
HEATSHRINK_WINDOW_SZ2 = 8
HEATSHRINK_LOOKAHEAD_SZ2 = 4


class Image:
    """Represents an icon image with dimensions and raw data."""

    def __init__(self, width: int, height: int, data: bytes):
        """
        Initialize an Image.

        Args:
            width: Image width in pixels
            height: Image height in pixels
            data: Image data bytes (compressed or raw XBM)
        """
        self.width = width
        self.height = height
        self.data = data

    def write(self, filename: str) -> None:
        """Write raw image data to a file."""
        with open(filename, 'wb') as f:
            f.write(self.data)

    def data_as_carray(self) -> str:
        """
        Convert image data to C array initializer string.

        Returns:
            String like "{0x01,0x00,0x05,0x00,...}"
        """
        hex_bytes = ','.join(f'0x{b:02x}' for b in self.data)
        return '{' + hex_bytes + '}'


def is_file_an_icon(filename: str) -> bool:
    """
    Check if a filename has a supported icon format extension.

    Args:
        filename: Filename to check

    Returns:
        True if the file extension is in ICONS_SUPPORTED_FORMATS
    """
    extension = filename.lower().rsplit('.', 1)[-1]
    return extension in ICONS_SUPPORTED_FORMATS


class ImageTools:
    """
    Tools for image conversion and compression.

    Handles image to XBM conversion using PIL (preferred) or ImageMagick convert
    for static PNG fallback, and XBM compression using heatshrink2 module
    (preferred) or CLI tool.
    """

    _pil_unavailable = False
    _heatshrink_unavailable = False

    def __init__(self):
        self.logger = logging.getLogger(__name__)

    def png2xbm(self, file: str) -> bytes:
        """
        Convert a PNG file to XBM format.

        Args:
            file: Path to PNG file

        Returns:
            XBM data as bytes

        Raises:
            RuntimeError: If conversion fails
        """
        if self._pil_unavailable:
            return self._png2xbm_imagemagick(file)

        try:
            from PIL import Image
        except ImportError:
            ImageTools._pil_unavailable = True
            self.logger.info('pillow module not available, using ImageMagick convert')
            return self._png2xbm_imagemagick(file)

        try:
            with Image.open(file) as im:
                return self._pil2xbm(im)
        except Exception as e:
            raise RuntimeError(f'Failed to convert {file} to XBM: {e}') from e

    def _pil2xbm(self, image) -> bytes:
        """Convert a PIL image object to an XBM document."""
        rgba = image.convert('RGBA')
        raw_bitmap = self._rgba2xbm_bitmap(rgba)
        return self._xbm_payload(rgba.width, rgba.height, raw_bitmap)

    def _rgba2xbm_bitmap(self, image) -> bytes:
        """Convert an RGBA image to raw XBM bitmap bytes.

        XBM stores foreground pixels as 1 bits, LSB first, with each row padded
        to a full byte. Transparent icons use alpha as the foreground mask.
        Fully opaque icons use luminance so existing black-on-white PNGs keep
        their old semantics.
        """
        pixels = list(image.getdata())
        has_transparency = any(a < 128 for _, _, _, a in pixels)
        bytes_per_row = (image.width + 7) // 8
        bitmap = bytearray(bytes_per_row * image.height)

        for y in range(image.height):
            for x in range(image.width):
                r, g, b, a = pixels[y * image.width + x]
                if has_transparency:
                    foreground = a >= 128
                else:
                    luminance = (r * 299 + g * 587 + b * 114) // 1000
                    foreground = luminance < 128

                if foreground:
                    bitmap[y * bytes_per_row + (x // 8)] |= 1 << (x % 8)

        return bytes(bitmap)

    def _xbm_payload(self, width: int, height: int, bitmap: bytes) -> bytes:
        """Build a small XBM document for the existing parser."""
        data = ','.join(f'0x{byte:02x}' for byte in bitmap)
        return (
            f'#define image_width {width}\n'
            f'#define image_height {height}\n'
            f'static unsigned char image_bits[] = {{{data}}};\n'
        ).encode('ascii')

    def _png2xbm_imagemagick(self, file: str) -> bytes:
        """Fallback PNG to XBM conversion using ImageMagick."""
        try:
            return subprocess.check_output(['convert', file, 'xbm:-'])
        except subprocess.CalledProcessError as e:
            raise RuntimeError(f'ImageMagick convert failed for {file}: {e}') from e
        except FileNotFoundError:
            raise RuntimeError(
                'Neither pillow nor ImageMagick convert is available. '
                'Install pillow: pip install pillow'
            )

    def compress_heatshrink(self, data: bytes) -> bytes:
        """
        Compress data using heatshrink algorithm.

        Args:
            data: Raw data to compress

        Returns:
            Compressed data

        Raises:
            RuntimeError: If compression fails
        """
        if self._heatshrink_unavailable:
            return self._compress_heatshrink_cli(data)

        try:
            import heatshrink2
        except ImportError:
            ImageTools._heatshrink_unavailable = True
            self.logger.info('heatshrink2 module not available, using CLI tool')
            return self._compress_heatshrink_cli(data)

        try:
            return heatshrink2.compress(
                data,
                window_sz2=HEATSHRINK_WINDOW_SZ2,
                lookahead_sz2=HEATSHRINK_LOOKAHEAD_SZ2
            )
        except Exception as e:
            raise RuntimeError(f'Heatshrink compression failed: {e}') from e

    def _compress_heatshrink_cli(self, data: bytes) -> bytes:
        """Fallback heatshrink compression using CLI tool."""
        try:
            return subprocess.check_output(
                ['heatshrink', '-e', f'-w{HEATSHRINK_WINDOW_SZ2}', f'-l{HEATSHRINK_LOOKAHEAD_SZ2}'],
                input=data
            )
        except subprocess.CalledProcessError as e:
            raise RuntimeError(f'heatshrink CLI compression failed: {e}') from e
        except FileNotFoundError:
            raise RuntimeError(
                'Neither heatshrink2 module nor heatshrink CLI is available. '
                'Install heatshrink2: pip install heatshrink2'
            )


# Global tools instance
_tools = ImageTools()


def _encode_image(width: int, height: int, raw_data: bytes, compress: bool) -> Image:
    """Encode raw XBM bitmap data into the ZUI icon frame payload."""
    if not compress:
        return Image(width, height, b'\x00' + raw_data)

    try:
        compressed = _tools.compress_heatshrink(raw_data)
    except RuntimeError:
        return Image(width, height, b'\x00' + raw_data)

    compressed_total = 4 + len(compressed)
    uncompressed_total = 1 + len(raw_data)

    if compressed_total < uncompressed_total:
        size_bytes = len(compressed).to_bytes(2, 'little')
        return Image(width, height, b'\x01\x00' + size_bytes + compressed)

    return Image(width, height, b'\x00' + raw_data)


def _parse_xbm(xbm_data: bytes) -> Tuple[int, int, bytes]:
    """
    Parse XBM format data to extract dimensions and raw bitmap.

    Args:
        xbm_data: XBM file contents as bytes

    Returns:
        Tuple of (width, height, raw_bitmap_bytes)

    Raises:
        ValueError: If XBM format is invalid
    """
    text = xbm_data.decode('ascii', errors='ignore').strip()
    lines = text.split('\n')

    width = None
    height = None

    # Parse header lines for width and height
    for line in lines:
        line = line.strip()
        if '_width' in line:
            parts = line.split()
            if len(parts) >= 3:
                width = int(parts[2])
        elif '_height' in line:
            parts = line.split()
            if len(parts) >= 3:
                height = int(parts[2])
        if width is not None and height is not None:
            break

    if width is None or height is None:
        raise ValueError('Could not parse XBM dimensions')

    # Find the data array - look for content between { and }
    full_text = '\n'.join(lines)
    start = full_text.find('{')
    end = full_text.rfind('}')

    if start == -1 or end == -1:
        raise ValueError('Could not find XBM data array')

    # Extract hex values
    data_str = full_text[start + 1:end]
    data_str = data_str.replace('\n', ' ').replace(',', ' ')
    hex_values = [s for s in data_str.split() if s.startswith('0x') or s.startswith('0X')]

    # Convert to bytes
    data_bytes = bytes(int(v, 16) for v in hex_values)

    return width, height, data_bytes


def file2image(file: str, compress: bool = True) -> Image:
    """
    Convert a PNG file to Image with optional compression.

    The output format matches Flipper Zero's icon format:
    - If compressed: 0x01 0x00 + 2-byte size (LE) + compressed data
    - If uncompressed: 0x00 + raw XBM data

    Args:
        file: Path to PNG file
        compress: Whether to attempt heatshrink compression (default True)

    Returns:
        Image object with width, height, and data

    Raises:
        RuntimeError: If conversion fails
        ValueError: If image format is invalid
    """
    # Convert image to XBM
    xbm_output = _tools.png2xbm(file)

    # Parse XBM to get dimensions and raw data
    width, height, raw_data = _parse_xbm(xbm_output)

    return _encode_image(width, height, raw_data, compress)


def file2images(file: str, compress: bool = True) -> List[Image]:
    """Convert a static image or animated GIF to one or more icon frames."""
    extension = file.lower().rsplit('.', 1)[-1]
    if extension != 'gif':
        return [file2image(file, compress=compress)]

    try:
        from PIL import Image as PILImage
        from PIL import ImageSequence
    except ImportError as e:
        raise RuntimeError('Pillow is required for animated GIF icon conversion') from e

    frames: List[Image] = []
    try:
        with PILImage.open(file) as im:
            for frame in ImageSequence.Iterator(im):
                xbm_output = _tools._pil2xbm(frame)
                width, height, raw_data = _parse_xbm(xbm_output)
                frames.append(_encode_image(width, height, raw_data, compress))
    except Exception as e:
        raise RuntimeError(f'Failed to convert GIF frames from {file}: {e}') from e

    if not frames:
        raise ValueError(f'GIF has no frames: {file}')

    return frames


def gif_frame_count(file: str) -> int:
    """Return the number of frames in a GIF file, or 1 for non-GIF files."""
    if file.lower().rsplit('.', 1)[-1] != 'gif':
        return 1

    try:
        from PIL import Image as PILImage
    except ImportError as e:
        raise RuntimeError('Pillow is required for animated GIF icon conversion') from e

    with PILImage.open(file) as im:
        return int(getattr(im, 'n_frames', 1))


def gif_frame_rate(file: str) -> int:
    """Derive a ZUI frame_rate value from GIF frame duration metadata."""
    try:
        from PIL import Image as PILImage
    except ImportError as e:
        raise RuntimeError('Pillow is required for animated GIF icon conversion') from e

    durations = []
    with PILImage.open(file) as im:
        for frame in range(int(getattr(im, 'n_frames', 1))):
            im.seek(frame)
            duration = int(im.info.get('duration') or 0)
            if duration > 0:
                durations.append(duration)

    if not durations:
        return 1

    avg_duration_ms = sum(durations) / len(durations)
    if avg_duration_ms <= 0:
        return 1

    # zui_icon stores integer FPS. Use nearest FPS, rounded half up.
    frame_rate = int((1000.0 / avg_duration_ms) + 0.5)
    return max(1, min(255, frame_rate))


def file2image_raw(file: str) -> Image:
    """
    Convert a PNG file to Image without compression header.

    Returns raw XBM bitmap data without the compression header byte.
    Useful for debugging or when compression is handled separately.

    Args:
        file: Path to PNG file

    Returns:
        Image object with raw XBM data
    """
    xbm_output = _tools.png2xbm(file)
    width, height, raw_data = _parse_xbm(xbm_output)
    return Image(width, height, raw_data)
