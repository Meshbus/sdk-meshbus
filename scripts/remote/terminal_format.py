# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Terminal text formatting shared by the remote dashboards."""

from __future__ import annotations

import re
import unicodedata

ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")


def _format_age(value) -> str:
    if value is None:
        return "-"
    if value < 1:
        return f"{value * 1000:.0f}ms"
    if value < 60:
        return f"{value:.1f}s"
    minutes, seconds = divmod(int(value), 60)
    if minutes < 60:
        return f"{minutes}m{seconds:02d}s"
    hours, minutes = divmod(minutes, 60)
    return f"{hours}h{minutes:02d}m"


def _strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


def _visible_len(text: str) -> int:
    return _cell_len(_strip_ansi(text))


def _cell_len(text: str) -> int:
    return sum(_cell_width(ch) for ch in text)


def _cell_width(ch: str) -> int:
    if not ch:
        return 0
    code = ord(ch)
    if unicodedata.combining(ch):
        return 0
    if code < 32 or 0x7F <= code < 0xA0:
        return 0
    if 0xE000 <= code <= 0xF8FF:
        return 2
    if unicodedata.east_asian_width(ch) in {"F", "W"}:
        return 2
    return 1


def _fit_plain(text: str, width: int) -> str:
    if width <= 0:
        return ""
    if _cell_len(text) <= width:
        return text
    if width == 1:
        return "…"
    return _take_cells(text, width - 1) + "…"


def _fit_ansi(text: str, width: int) -> str:
    if width <= 0:
        return ""
    visible = _visible_len(text)
    if visible <= width:
        return text + (" " * (width - visible))
    plain = _strip_ansi(text)
    if width == 1:
        return "…"
    return _take_cells(plain, width - 1) + "…"


def _take_cells(text: str, width: int) -> str:
    if width <= 0:
        return ""
    cells = 0
    chars: list[str] = []
    for ch in text:
        ch_width = _cell_width(ch)
        if cells + ch_width > width:
            break
        chars.append(ch)
        cells += ch_width
    return "".join(chars)
