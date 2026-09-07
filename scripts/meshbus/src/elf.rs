// SPDX-License-Identifier: Apache-2.0
use anyhow::{Result, ensure};
use std::collections::{BTreeMap, BTreeSet};
#[derive(Clone, Debug)]
pub struct Section {
    pub name: String,
    pub kind: u64,
    pub flags: u64,
    pub offset: u64,
    pub size: u64,
    pub link: usize,
    pub align: u64,
    pub entsize: u64,
}
pub struct Elf {
    pub sections: Vec<Section>,
    strings: usize,
    class: u8,
}
fn number(data: &[u8], offset: usize, n: usize, le: bool) -> Result<u64> {
    let bytes = data
        .get(
            offset
                ..offset
                    .checked_add(n)
                    .ok_or_else(|| anyhow::anyhow!("ELF offset overflow"))?,
        )
        .ok_or_else(|| anyhow::anyhow!("truncated ELF"))?;
    let mut out = 0;
    if le {
        for (i, b) in bytes.iter().enumerate() {
            out |= u64::from(*b) << (i * 8);
        }
    } else {
        for b in bytes {
            out = (out << 8) | u64::from(*b);
        }
    }
    Ok(out)
}
impl Elf {
    pub fn parse(d: &[u8]) -> Result<Self> {
        ensure!(
            d.len() >= 52
                && &d[..4] == b"\x7fELF"
                && [1, 2].contains(&d[4])
                && [1, 2].contains(&d[5]),
            "invalid ELF header"
        );
        let class = d[4];
        let le = d[5] == 1;
        let wide = class == 2;
        let width = if wide { 8 } else { 4 };
        let off = usize::try_from(number(d, if wide { 40 } else { 32 }, width, le)?)?;
        let h = if wide { 58 } else { 46 };
        let stride = number(d, h, 2, le)? as usize;
        let count = number(d, h + 2, 2, le)? as usize;
        let strings = number(d, h + 4, 2, le)? as usize;
        ensure!(
            off > 0 && count > 0 && strings < count && stride >= if wide { 64 } else { 40 },
            "invalid ELF section table"
        );
        ensure!(
            off.checked_add(stride * count)
                .is_some_and(|end| end <= d.len()),
            "truncated ELF section table"
        );
        let mut sections = vec![];
        let mut names = vec![];
        for i in 0..count {
            let p = off + i * stride;
            names.push(number(d, p, 4, le)? as usize);
            let s = Section {
                name: String::new(),
                kind: number(d, p + 4, 4, le)?,
                flags: number(d, p + 8, width, le)?,
                offset: number(d, p + 8 + 2 * width, width, le)?,
                size: number(d, p + 8 + 3 * width, width, le)?,
                link: number(d, p + 8 + 4 * width, 4, le)? as usize,
                align: number(d, p + 16 + 4 * width, width, le)?,
                entsize: number(d, p + 16 + 5 * width, width, le)?,
            };
            ensure!(
                s.align == 0 || s.align.is_power_of_two(),
                "invalid ELF alignment"
            );
            ensure!(
                s.offset <= u32::MAX.into()
                    && s.size <= u32::MAX.into()
                    && s.align <= u32::MAX.into(),
                "ELF section exceeds 32-bit target bounds"
            );
            ensure!(
                s.kind == 8
                    || s.offset
                        .checked_add(s.size)
                        .is_some_and(|end| end <= d.len() as u64),
                "truncated ELF section"
            );
            sections.push(s);
        }
        let table = &sections[strings];
        ensure!(table.kind == 3, "ELF section names must use a string table");
        let bytes = d
            .get(table.offset as usize..(table.offset + table.size) as usize)
            .ok_or_else(|| anyhow::anyhow!("invalid ELF string table"))?;
        for (s, p) in sections.iter_mut().zip(names) {
            let tail = bytes
                .get(p..)
                .ok_or_else(|| anyhow::anyhow!("ELF name offset outside string table"))?;
            let end = tail
                .iter()
                .position(|b| *b == 0)
                .ok_or_else(|| anyhow::anyhow!("unterminated ELF section name"))?;
            s.name = String::from_utf8_lossy(&tail[..end]).into_owned();
        }
        Ok(Self {
            sections,
            strings,
            class,
        })
    }
    pub fn heap(&self, config: &BTreeMap<String, String>) -> Result<u32> {
        let mut regions: BTreeMap<u8, Section> = BTreeMap::new();
        let mut mapped = BTreeSet::new();
        let mut symbols = 0;
        let mut relocs = 0;
        let mut symtab = false;
        fn merge(regions: &mut BTreeMap<u8, Section>, key: u8, s: &Section) {
            regions
                .entry(key)
                .and_modify(|r| {
                    let bottom = r.offset.min(s.offset);
                    r.size = (r.offset + r.size).max(s.offset + s.size) - bottom;
                    r.offset = bottom;
                    r.align = r.align.max(s.align);
                    r.flags |= s.flags;
                })
                .or_insert_with(|| s.clone());
        }
        for (i, s) in self.sections.iter().enumerate() {
            if s.kind == 2 {
                merge(&mut regions, 5, s);
                mapped.insert(i);
                symtab = s.entsize != 0;
                ensure!(
                    s.link < self.sections.len(),
                    "invalid ELF symbol string table"
                );
                merge(&mut regions, 6, &self.sections[s.link]);
                mapped.insert(s.link);
            }
            if [2, 11].contains(&s.kind) && s.entsize > 0 {
                symbols += s.size / s.entsize;
            }
            if [4, 9].contains(&s.kind) {
                let min = if self.class == 1 {
                    if s.kind == 9 { 8 } else { 12 }
                } else if s.kind == 9 {
                    16
                } else {
                    24
                };
                let stride = if s.entsize == 0 { min } else { s.entsize };
                ensure!(
                    stride >= min && s.size % stride == 0,
                    "invalid ELF relocations"
                );
                relocs += s.size / stride;
            }
        }
        merge(&mut regions, 7, &self.sections[self.strings]);
        mapped.insert(self.strings);
        for (i, s) in self.sections.iter().enumerate() {
            if mapped.contains(&i) || s.size == 0 || s.flags & 2 == 0 {
                continue;
            }
            let key = if s.name == ".exported_sym" {
                4
            } else if s.kind == 8 {
                3
            } else if s.kind == 1 {
                if s.flags & 4 != 0 {
                    0
                } else if s.flags & 1 != 0 {
                    1
                } else {
                    2
                }
            } else {
                continue;
            };
            merge(&mut regions, key, s);
        }
        let enabled = |key: &str| config.get(key).is_some_and(|v| v == "y" || v == "1");
        let align = |size: u64, n: u64| size.div_ceil(n) * n;
        let mut total = 0;
        let mut count = 0;
        for r in regions.values() {
            let a = r.align.max(1);
            let mut size = r.size + (r.offset & (a - 1));
            if r.flags & 2 != 0 {
                if enabled("CONFIG_MMU") {
                    let page = config
                        .get("CONFIG_MMU_PAGE_SIZE")
                        .and_then(|s| s.parse().ok())
                        .filter(|v| *v > 0)
                        .unwrap_or(4096);
                    size = align(size, page);
                } else if enabled("CONFIG_USERSPACE")
                    && enabled("CONFIG_MPU_REQUIRES_POWER_OF_TWO_ALIGNMENT")
                {
                    size = size
                        .max(a)
                        .max(32)
                        .checked_next_power_of_two()
                        .ok_or_else(|| anyhow::anyhow!("ELF allocation overflow"))?;
                } else if enabled("CONFIG_USERSPACE")
                    && (enabled("CONFIG_ARM_MPU") || enabled("CONFIG_ARC_MPU"))
                {
                    size = align(size, 32);
                }
            }
            if size > 0 {
                total += size;
                count += 1;
            }
        }
        total += self.sections.len() as u64 * 64 + symbols * 16 + 1024 + count * 64 + relocs * 8;
        if symtab {
            let exports = regions.get(&4).map_or(0, |s| s.size);
            if exports > 0 {
                total += (exports / 8).max(1) * 16;
            }
        }
        Ok(u32::try_from(align(total, 8))?)
    }
}
