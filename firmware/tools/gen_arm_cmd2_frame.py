#!/usr/bin/env python3
"""
Generate UART frame for ArmPiUltra SYS cmd=2 (move then claw action).

Frame format:
  AA 55 | function(1) | length(1) | payload | crc8(1)

Payload for cmd=2 (little-endian):
  uint8   cmd
  int16   x_10
  int16   y_10
  int16   z_10
  int16   pitch_10
  int16   min_pitch_10
  int16   max_pitch_10
  uint16  move_time_ms
  int16   claw_open_angle_10
  uint16  claw_time_ms
"""

from __future__ import annotations

import argparse
import struct
import sys

START_BYTE_1 = 0xAA
START_BYTE_2 = 0x55
PACKET_FUNC_SYS = 0x00
CMD_MOVE_AND_CLAW = 0x02


def clamp(value: float, min_v: float, max_v: float) -> float:
    if value < min_v:
        return min_v
    if value > max_v:
        return max_v
    return value


def to_i16_x10(value: float) -> int:
    raw = int(round(value * 10.0))
    if raw < -32768 or raw > 32767:
        raise ValueError(f"value out of int16 range after x10 scaling: {value}")
    return raw


def to_u16(value: int, name: str) -> int:
    if value < 0 or value > 65535:
        raise ValueError(f"{name} must be in [0, 65535], got {value}")
    return value


def crc8_maxim(data: bytes) -> int:
    """CRC-8/MAXIM compatible with firmware (poly=0x8C, init=0x00, reflected)."""
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x01:
                crc = ((crc >> 1) ^ 0x8C) & 0xFF
            else:
                crc = (crc >> 1) & 0xFF
    return crc


def build_frame(
    x: float,
    y: float,
    z: float,
    pitch: float,
    min_pitch: float,
    max_pitch: float,
    move_time_ms: int,
    claw_open_angle: float,
    claw_time_ms: int,
    do_clamp: bool,
) -> bytes:
    if do_clamp:
        x = clamp(x, 10.0, 20.0)
        y = clamp(y, -10.0, 10.0)
        z = clamp(z, 0.0, 25.0)
        pitch = clamp(pitch, -90.0, 90.0)
        min_pitch = clamp(min_pitch, -90.0, 90.0)
        max_pitch = clamp(max_pitch, -90.0, 90.0)
        claw_open_angle = clamp(claw_open_angle, 0.0, 90.0)

    if min_pitch > max_pitch:
        min_pitch, max_pitch = max_pitch, min_pitch

    payload = struct.pack(
        "<BhhhhhhHhH",
        CMD_MOVE_AND_CLAW,
        to_i16_x10(x),
        to_i16_x10(y),
        to_i16_x10(z),
        to_i16_x10(pitch),
        to_i16_x10(min_pitch),
        to_i16_x10(max_pitch),
        to_u16(move_time_ms, "move_time_ms"),
        to_i16_x10(claw_open_angle),
        to_u16(claw_time_ms, "claw_time_ms"),
    )

    head = bytes([START_BYTE_1, START_BYTE_2, PACKET_FUNC_SYS, len(payload)])
    crc_input = bytes([PACKET_FUNC_SYS, len(payload)]) + payload
    crc = crc8_maxim(crc_input)
    return head + payload + bytes([crc])


def hex_bytes(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Generate ArmPiUltra cmd=2 frame (move then claw).",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--x", type=float, required=True, help="target x")
    p.add_argument("--y", type=float, required=True, help="target y")
    p.add_argument("--z", type=float, required=True, help="target z")
    p.add_argument("--pitch", type=float, default=10.0, help="target pitch")
    p.add_argument("--min-pitch", type=float, default=-90.0, help="min pitch")
    p.add_argument("--max-pitch", type=float, default=90.0, help="max pitch")
    p.add_argument("--move-ms", type=int, default=1500, help="move time in ms")
    p.add_argument("--claw-angle", type=float, default=0.0, help="claw open angle, 0=close 90=open")
    p.add_argument("--claw-ms", type=int, default=500, help="claw move time in ms")
    p.add_argument(
        "--no-clamp",
        action="store_true",
        help="disable firmware-like clamping before packing",
    )
    return p.parse_args()


def main() -> int:
    try:
        args = parse_args()
        frame = build_frame(
            x=args.x,
            y=args.y,
            z=args.z,
            pitch=args.pitch,
            min_pitch=args.min_pitch,
            max_pitch=args.max_pitch,
            move_time_ms=args.move_ms,
            claw_open_angle=args.claw_angle,
            claw_time_ms=args.claw_ms,
            do_clamp=not args.no_clamp,
        )
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    print("Frame hex:")
    print(hex_bytes(frame))
    print("\nNote: send in HEX mode via UART.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
