# Copyright (c) 2026 FoBE Studio
# SPDX-License-Identifier: Apache-2.0

"""Reproducible Meshbus FIRMWARE v1 sizing and LoRa airtime model."""

from __future__ import annotations

import argparse
import math
from dataclasses import dataclass

SMP_HEADER_SIZE = 8
SMP_PACKET_LIMIT = 608
PATCH_CAP = 24 * 1024
PRIMARY_CHUNK = 448
FALLBACK_CHUNK = 384
TRANSFER_ID_SIZE = 32


def _varint_size(value: int) -> int:
    if value < 0:
        raise ValueError("varints must be non-negative")
    size = 1
    while value >= 0x80:
        value >>= 7
        size += 1
    return size


def _protobuf_bytes_size(field_number: int, length: int) -> int:
    return _varint_size(field_number << 3 | 2) + _varint_size(length) + length


def _protobuf_uint_size(field_number: int, value: int) -> int:
    return _varint_size(field_number << 3) + _varint_size(value)


def _cbor_bstr_header_size(length: int) -> int:
    if length < 24:
        return 1
    if length <= 0xFF:
        return 2
    if length <= 0xFFFF:
        return 3
    return 5


def write_request_sizes(
    chunk_size: int, offset: int = PATCH_CAP - PRIMARY_CHUNK
) -> dict[str, int]:
    """Return fixed-v1 write sizes for transfer_id=1, offset=2, data=3."""
    protobuf = (
        _protobuf_bytes_size(1, TRANSFER_ID_SIZE)
        + _protobuf_uint_size(2, offset)
        + _protobuf_bytes_size(3, chunk_size)
    )
    # Canonical one-entry map: {"p": bstr(protobuf)}.
    cbor = 1 + 2 + _cbor_bstr_header_size(protobuf) + protobuf
    return {"protobuf": protobuf, "cbor": cbor, "smp": SMP_HEADER_SIZE + cbor}


@dataclass(frozen=True)
class LoraProfile:
    frequency_hz: int = 915_125_000
    bandwidth_hz: int = 250_000
    spreading_factor: int = 11
    coding_rate_denominator: int = 5
    preamble_symbols: int = 16
    crc: bool = True
    explicit_header: bool = True
    low_data_rate_optimization: bool = False


def lora_airtime_seconds(
    payload_bytes: int, profile: LoraProfile = LoraProfile()
) -> float:
    """Calculate Semtech LoRa packet airtime for one PHY payload."""
    sf = profile.spreading_factor
    symbol_time = (2**sf) / profile.bandwidth_hz
    de = 1 if profile.low_data_rate_optimization else 0
    ih = 0 if profile.explicit_header else 1
    crc = 1 if profile.crc else 0
    cr = profile.coding_rate_denominator - 4
    numerator = 8 * payload_bytes - 4 * sf + 28 + 16 * crc - 20 * ih
    denominator = 4 * (sf - 2 * de)
    payload_symbols = 8 + max(math.ceil(numerator / denominator) * (cr + 4), 0)
    return (profile.preamble_symbols + 4.25 + payload_symbols) * symbol_time


def transfer_model(
    patch_bytes: int,
    chunk_size: int,
    *,
    hops: int = 1,
    retry_rate: float = 0.0,
    full_frame_bytes: int = 184,
) -> dict[str, float | int]:
    """Model one serialized transfer using Phase-12 stop-and-wait frames."""
    if not 0 < patch_bytes <= PATCH_CAP:
        raise ValueError("patch_bytes outside the v1 cap")
    if chunk_size not in (PRIMARY_CHUNK, FALLBACK_CHUNK):
        raise ValueError("unsupported v1 chunk size")
    if hops < 1 or retry_rate < 0:
        raise ValueError("invalid topology or retry rate")
    writes = math.ceil(patch_bytes / chunk_size)
    request_fragments = math.ceil(write_request_sizes(chunk_size)["smp"] / 104)
    frames_per_write = request_fragments + (request_fragments - 1) + 1
    lifecycle_frames = 2 + 5 * 2
    endpoint_frames = writes * frames_per_write + lifecycle_frames
    transmissions = math.ceil(endpoint_frames * hops * (1.0 + retry_rate))
    airtime = transmissions * lora_airtime_seconds(full_frame_bytes)
    return {
        "patch_bytes": patch_bytes,
        "chunk_size": chunk_size,
        "writes": writes,
        "request_fragments": request_fragments,
        "frames_per_write": frames_per_write,
        "endpoint_frames": endpoint_frames,
        "radio_transmissions": transmissions,
        "airtime_seconds": airtime,
        "one_percent_elapsed_seconds": airtime / 0.01,
        "ten_percent_elapsed_seconds": airtime / 0.10,
    }


def _print_report() -> None:
    for chunk in (PRIMARY_CHUNK, FALLBACK_CHUNK):
        sizes = write_request_sizes(chunk)
        print(
            f"chunk={chunk}: protobuf={sizes['protobuf']} cbor={sizes['cbor']} "
            f"smp={sizes['smp']} limit={SMP_PACKET_LIMIT}"
        )
        if sizes["smp"] > SMP_PACKET_LIMIT:
            raise SystemExit(f"chunk {chunk} exceeds SMP limit")
    for patch in (9 * 1024, PATCH_CAP):
        for chunk in (PRIMARY_CHUNK, FALLBACK_CHUNK):
            for hops in (1, 2, 3):
                for retry in (0.0, 0.05, 0.10):
                    result = transfer_model(
                        patch, chunk, hops=hops, retry_rate=retry
                    )
                    print(
                        "patch={patch_bytes} chunk={chunk_size} hops={hops} "
                        "retry={retry:.0%} writes={writes} "
                        "frames={endpoint_frames} tx={radio_transmissions} "
                        "airtime={airtime_seconds:.1f}s "
                        "elapsed@10%={ten_percent_elapsed_seconds:.1f}s "
                        "elapsed@1%={one_percent_elapsed_seconds:.1f}s".format(
                            hops=hops, retry=retry, **result
                        )
                    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.check:
        assert write_request_sizes(PRIMARY_CHUNK)["smp"] <= SMP_PACKET_LIMIT
        assert transfer_model(9 * 1024, PRIMARY_CHUNK)["writes"] == 21
        assert transfer_model(9 * 1024, FALLBACK_CHUNK)["writes"] == 24
        print("FIRMWARE protocol model checks passed")
        return 0
    _print_report()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
