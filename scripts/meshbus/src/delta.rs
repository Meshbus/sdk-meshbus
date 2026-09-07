// SPDX-License-Identifier: Apache-2.0
//! detools sequential CRLE wire format. The bsdiff crate's stream is NOT this format.
use anyhow::{Result, bail, ensure};

const MAX_IMAGE: usize = 16 * 1024 * 1024;

pub(crate) fn take<'a>(input: &mut &'a [u8], n: usize) -> Result<&'a [u8]> {
    ensure!(n <= input.len(), "truncated binary input");
    let (head, tail) = input.split_at(n);
    *input = tail;
    Ok(head)
}

fn size(out: &mut Vec<u8>, n: i64) {
    let mut value = n.unsigned_abs();
    out.push((value as u8 & 0x3f) | if n < 0 { 0x40 } else { 0 });
    value >>= 6;
    while value != 0 {
        *out.last_mut().unwrap() |= 0x80;
        out.push(value as u8 & 0x7f);
        value >>= 7;
    }
}

fn read_size(input: &mut &[u8]) -> Result<i64> {
    let first = take(input, 1)?[0];
    let negative = first & 0x40 != 0;
    let mut result = u64::from(first & 0x3f);
    let mut byte = first;
    let mut shift = 6;
    while byte & 0x80 != 0 {
        byte = take(input, 1)?[0];
        ensure!(
            shift < 63 && (u64::from(byte & 0x7f) <= (i64::MAX as u64 >> shift)),
            "delta integer overflow"
        );
        result |= u64::from(byte & 0x7f) << shift;
        shift += 7;
    }
    Ok(if negative {
        -(result as i64)
    } else {
        result as i64
    })
}

fn unsigned_size(out: &mut Vec<u8>, mut n: usize) {
    loop {
        let next = n >> 7;
        out.push((n as u8 & 0x7f) | if next != 0 { 0x80 } else { 0 });
        n = next;
        if n == 0 {
            break;
        }
    }
}

fn read_unsigned(input: &mut &[u8]) -> Result<usize> {
    let mut n = 0usize;
    for shift in (0..usize::BITS).step_by(7) {
        let b = take(input, 1)?[0];
        let value = usize::from(b & 0x7f);
        ensure!(value <= usize::MAX >> shift, "CRLE integer overflow");
        n |= value << shift;
        if b & 0x80 == 0 {
            return Ok(n);
        }
    }
    bail!("CRLE integer overflow")
}

fn compress(data: &[u8]) -> Vec<u8> {
    let mut out = Vec::new();
    let mut literal = 0;
    let mut p = 0;
    while p < data.len() {
        let mut end = p + 1;
        while end < data.len() && data[end] == data[p] {
            end += 1;
        }
        if end - p >= 6 {
            if p > literal {
                out.push(0);
                unsigned_size(&mut out, p - literal);
                out.extend_from_slice(&data[literal..p]);
            }
            out.push(1);
            unsigned_size(&mut out, end - p);
            out.push(data[p]);
            literal = end;
        }
        p = end;
    }
    if literal < data.len() || out.is_empty() {
        out.push(0);
        unsigned_size(&mut out, data.len() - literal);
        out.extend_from_slice(&data[literal..]);
    }
    out
}

fn decompress(mut input: &[u8], limit: usize) -> Result<Vec<u8>> {
    let mut out = Vec::new();
    while !input.is_empty() {
        let kind = take(&mut input, 1)?[0];
        let n = read_unsigned(&mut input)?;
        ensure!(
            n <= limit.saturating_sub(out.len()),
            "CRLE expansion exceeds image bound"
        );
        match kind {
            0 => out.extend_from_slice(take(&mut input, n)?),
            1 => {
                let b = take(&mut input, 1)?[0];
                out.resize(out.len() + n, b);
            }
            _ => bail!("unknown CRLE segment"),
        }
    }
    Ok(out)
}

fn bsdiff_integer(input: &mut &[u8]) -> Result<i64> {
    let n = u64::from_le_bytes(take(input, 8)?.try_into()?);
    let magnitude = (n & !(1 << 63)) as i64;
    Ok(if n >> 63 != 0 { -magnitude } else { magnitude })
}

pub fn create(source: &[u8], target: &[u8]) -> Result<Vec<u8>> {
    ensure!(
        source.len() <= MAX_IMAGE && target.len() <= MAX_IMAGE,
        "image exceeds supported size"
    );
    let mut patch = vec![2];
    size(&mut patch, target.len() as i64);
    if target.is_empty() {
        return Ok(patch);
    }
    let mut bsdiff = Vec::new();
    bsdiff::diff(source, target, &mut bsdiff)?;
    let mut records = bsdiff.as_slice();
    let mut sequential = vec![0]; // no data-format transform
    while !records.is_empty() {
        let diff = bsdiff_integer(&mut records)?;
        let extra = bsdiff_integer(&mut records)?;
        let adjust = bsdiff_integer(&mut records)?;
        ensure!(diff >= 0 && extra >= 0, "invalid bsdiff record");
        size(&mut sequential, diff);
        sequential.extend_from_slice(take(&mut records, diff as usize)?);
        size(&mut sequential, extra);
        sequential.extend_from_slice(take(&mut records, extra as usize)?);
        size(&mut sequential, adjust);
    }
    patch.extend(compress(&sequential));
    ensure!(apply(source, &patch)? == target, "delta self-check failed");
    Ok(patch)
}

pub fn apply(source: &[u8], mut patch: &[u8]) -> Result<Vec<u8>> {
    ensure!(
        take(&mut patch, 1)? == [2],
        "detools patch must use sequential CRLE encoding"
    );
    let target_size = usize::try_from(read_size(&mut patch)?)?;
    ensure!(
        target_size <= MAX_IMAGE && source.len() <= MAX_IMAGE,
        "image exceeds supported size"
    );
    if target_size == 0 {
        ensure!(patch.is_empty(), "trailing empty patch data");
        return Ok(vec![]);
    }
    // Every emitted byte can require two lengths and an adjustment. Bound bombs.
    let raw = decompress(patch, target_size * 32 + 32)?;
    let mut input = raw.as_slice();
    ensure!(
        read_size(&mut input)? == 0,
        "unsupported detools data transformation"
    );
    let mut output = Vec::with_capacity(target_size);
    let mut source_pos = 0i64;
    while output.len() < target_size {
        let diff = usize::try_from(read_size(&mut input)?)?;
        ensure!(
            diff <= target_size - output.len(),
            "delta diff exceeds target"
        );
        let pos = usize::try_from(source_pos)?;
        ensure!(
            pos <= source.len() && diff <= source.len() - pos,
            "delta diff exceeds source"
        );
        output.extend(
            take(&mut input, diff)?
                .iter()
                .zip(&source[pos..pos + diff])
                .map(|(a, b)| a.wrapping_add(*b)),
        );
        source_pos += diff as i64;
        let extra = usize::try_from(read_size(&mut input)?)?;
        ensure!(
            extra <= target_size - output.len(),
            "delta extra exceeds target"
        );
        output.extend_from_slice(take(&mut input, extra)?);
        source_pos = source_pos
            .checked_add(read_size(&mut input)?)
            .ok_or_else(|| anyhow::anyhow!("source offset overflow"))?;
    }
    ensure!(input.is_empty(), "trailing delta records");
    Ok(output)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn varied_images_and_corruption() {
        for (old, new) in [
            (vec![], vec![]),
            (vec![], vec![7; 100]),
            (vec![4; 100], vec![9; 88]),
            ((0..=255).collect(), (0..=255).rev().collect()),
        ] {
            let p = create(&old, &new).unwrap();
            assert_eq!(apply(&old, &p).unwrap(), new);
            for n in 0..p.len() {
                assert!(apply(&old, &p[..n]).is_err());
            }
            let mut trailing = p.clone();
            trailing.extend([0, 1, 0]);
            assert!(apply(&old, &trailing).is_err());
        }
    }
    #[test]
    fn integer_and_bomb_rejection() {
        assert!(
            apply(
                &[],
                &[
                    2, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
                ]
            )
            .is_err()
        );
        assert!(apply(&[], &[2, 1, 1, 0xff, 0xff, 0xff, 0x7f, 0]).is_err());
        for n in [0, 1, -1, 63, 64, -64, 123456, i64::MAX, -i64::MAX] {
            let mut b = vec![];
            size(&mut b, n);
            assert_eq!(read_size(&mut b.as_slice()).unwrap(), n);
        }
    }
}
