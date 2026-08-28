# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 FoBE Studio

"""Minimal ELF section parser used for LLEXT heap estimation."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import struct


ELF_SHT_NOBITS = 8
ELF_SHT_REL = 9
ELF_SHT_RELA = 4


class ElfError(ValueError):
    """Raised when an ELF input is malformed or unsupported."""


@dataclass(frozen=True)
class ElfSection:
    index: int
    name: str
    name_offset: int
    type: int
    flags: int
    addr: int
    offset: int
    size: int
    link: int
    info: int
    align: int
    entsize: int

    def as_mapping(self) -> dict[str, int | str]:
        return {
            "index": self.index,
            "name": self.name,
            "name_offset": self.name_offset,
            "type": self.type,
            "flags": self.flags,
            "addr": self.addr,
            "offset": self.offset,
            "size": self.size,
            "link": self.link,
            "info": self.info,
            "align": self.align,
            "entsize": self.entsize,
        }


class ElfImage:
    """Small ELF32/ELF64 parser for LLEXT section and relocation sizes."""

    def __init__(self, data: bytes, source: str = "<memory>"):
        self.data = data
        self.source = source
        self.elf_class = 0
        self.prefix = ""
        self.shstrndx = 0
        self.sections: tuple[ElfSection, ...] = ()
        self._parse()

    @classmethod
    def from_path(cls, path: Path) -> "ElfImage":
        return cls(path.read_bytes(), str(path))

    def _fail(self, message: str) -> None:
        raise ElfError(f"{message}: {self.source}")

    def _parse(self) -> None:
        if len(self.data) < 16 or self.data[:4] != b"\x7fELF":
            self._fail("not an ELF file")
        elf_class = self.data[4]
        endian = self.data[5]
        if endian == 1:
            prefix = "<"
        elif endian == 2:
            prefix = ">"
        else:
            self._fail(f"unsupported ELF endian {endian}")
            return
        if elf_class == 1:
            ehdr_fmt = prefix + "16sHHIIIIIHHHHHH"
            shdr_fmt = prefix + "IIIIIIIIII"
        elif elf_class == 2:
            ehdr_fmt = prefix + "16sHHIQQQIHHHHHH"
            shdr_fmt = prefix + "IIQQQQIIQQ"
        else:
            self._fail(f"unsupported ELF class {elf_class}")
            return
        ehdr_size = struct.calcsize(ehdr_fmt)
        if len(self.data) < ehdr_size:
            self._fail("truncated ELF header")
        ehdr = struct.unpack_from(ehdr_fmt, self.data, 0)
        e_shoff = int(ehdr[6])
        e_shentsize = int(ehdr[11])
        e_shnum = int(ehdr[12])
        e_shstrndx = int(ehdr[13])
        if e_shoff == 0 or e_shnum == 0:
            self._fail("ELF has no supported section table")
        shdr_size = struct.calcsize(shdr_fmt)
        if e_shentsize < shdr_size:
            self._fail(f"unsupported section header size {e_shentsize}")
        if e_shoff + e_shentsize * e_shnum > len(self.data):
            self._fail("truncated ELF section table")

        raw_sections: list[tuple[int, ...]] = []
        for index in range(e_shnum):
            offset = e_shoff + index * e_shentsize
            raw_sections.append(
                tuple(
                    int(value)
                    for value in struct.unpack_from(shdr_fmt, self.data, offset)
                )
            )
        if e_shstrndx >= len(raw_sections):
            self._fail(f"invalid shstrtab index {e_shstrndx}")
        shstr = raw_sections[e_shstrndx]
        shstr_data = self._slice(int(shstr[4]), int(shstr[5]), "section name table")
        self.elf_class = elf_class
        self.prefix = prefix
        self.shstrndx = e_shstrndx
        self.sections = tuple(
            ElfSection(
                index=index,
                name=_elf_string(shstr_data, raw[0]),
                name_offset=raw[0],
                type=raw[1],
                flags=raw[2],
                addr=raw[3],
                offset=raw[4],
                size=raw[5],
                link=raw[6],
                info=raw[7],
                align=raw[8],
                entsize=raw[9],
            )
            for index, raw in enumerate(raw_sections)
        )

    def _slice(self, offset: int, size: int, label: str) -> bytes:
        if offset < 0 or size < 0 or offset + size > len(self.data):
            self._fail(f"truncated {label}")
        return self.data[offset : offset + size]

    def relocation_count(self) -> int:
        total = 0
        for section in self.sections:
            if section.type not in (ELF_SHT_REL, ELF_SHT_RELA):
                continue
            if section.type == ELF_SHT_REL:
                fmt = self.prefix + ("II" if self.elf_class == 1 else "QQ")
            else:
                fmt = self.prefix + ("IIi" if self.elf_class == 1 else "QQq")
            size = struct.calcsize(fmt)
            entry_size = section.entsize or size
            if entry_size < size or section.size % entry_size != 0:
                self._fail(f"invalid relocation entry size in {section.name}")
            total += section.size // entry_size
        return total


def _elf_string(table: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(table):
        return ""
    end = table.find(b"\0", offset)
    if end < 0:
        end = len(table)
    return table[offset:end].decode("utf-8", errors="replace")
