#!/usr/bin/env python3
"""Probe Slicky HID reports for per-LED addressing."""

from __future__ import annotations

import argparse
import sys
import time

import hid

VID = 0x04D8
PID = 0xEC24
REPORT_LEN = 65  # report ID + 64 payload bytes


def hexdump(data: bytes | list[int], n: int | None = None) -> str:
    b = bytes(data[:n] if n is not None else data)
    return " ".join(f"{x:02x}" for x in b)


def drain(dev: hid.device, ms: int = 50) -> list[bytes]:
    out: list[bytes] = []
    while True:
        data = dev.read(64, timeout_ms=ms)
        if not data:
            break
        out.append(bytes(data))
    return out


def send(dev: hid.device, payload: list[int] | bytes, label: str) -> int:
    buf = [0] * REPORT_LEN
    buf[0] = 0x00
    for i, v in enumerate(payload):
        if i + 1 >= REPORT_LEN:
            break
        buf[i + 1] = v & 0xFF
    n = dev.write(bytes(buf))
    replies = drain(dev, ms=30)
    extra = ""
    if replies:
        extra = "  replies=" + " | ".join(hexdump(r, 16) for r in replies)
    print(f"  write {n:3d}  {label:40s}  {hexdump(buf[1:17])}{extra}")
    return n


def set_global(dev: hid.device, r: int, g: int, b: int, w: int = 0, label: str = "") -> None:
    send(dev, [0x0A, 0x04, 0x00, 0x00, w, b, g, r], label or f"global RGBW {r:02x}{g:02x}{b:02x}{w:02x}")


def phase_readonly(dev: hid.device) -> None:
    print("\n=== Feature / input drain ===")
    try:
        feat = bytes(dev.get_feature_report(0, 8))
        print(f"  get_feature: n={len(feat)} data={hexdump(feat)}")
    except (OSError, ValueError, TypeError) as e:
        print(f"  get_feature failed: {e}")

    pending = drain(dev, ms=200)
    if pending:
        for i, r in enumerate(pending):
            print(f"  pending IN[{i}]: {hexdump(r)}")
    else:
        print("  no pending input reports")


def phase_index(dev: hid.device) -> None:
    print("\n=== Hypothesis: bytes 3/4 are LED index (watch the light) ===")
    print("  Setting all LEDs dim white, then trying per-index red/green/blue")
    set_global(dev, 8, 8, 8, 0, "dim white baseline")
    time.sleep(0.4)

    colors = [
        (0xFF, 0x00, 0x00, "RED"),
        (0x00, 0xFF, 0x00, "GREEN"),
        (0x00, 0x00, 0xFF, "BLUE"),
    ]
    for idx in range(16):
        r, g, b, name = colors[idx % 3]
        # variant A: byte3 = index, byte4 = 0
        send(dev, [0x0A, 0x04, idx, 0x00, 0, b, g, r], f"idxA[{idx}] {name}")
        time.sleep(0.25)
    time.sleep(0.3)
    set_global(dev, 8, 8, 8, 0, "reset dim white")
    time.sleep(0.3)
    for idx in range(16):
        r, g, b, name = colors[idx % 3]
        # variant B: byte3 = 0, byte4 = index
        send(dev, [0x0A, 0x04, 0x00, idx, 0, b, g, r], f"idxB[{idx}] {name}")
        time.sleep(0.25)


def phase_count(dev: hid.device) -> None:
    print("\n=== Hypothesis: byte 2 is pixel count / payload length ===")
    # If 0x04 is "4 channels", try other counts with packed RGB / RGBW.
    packed_rgbw = []
    for i in range(12):
        # distinct colors per slot
        packed_rgbw += [0x00, (i * 20) & 0xFF, 0x00, 0xFF if i % 2 == 0 else 0x00]  # W B G R

    for count in (1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20):
        payload = [0x0A, count, 0x00, 0x00] + packed_rgbw
        send(dev, payload, f"0x0A count={count} packed RGBW")
        time.sleep(0.2)


def phase_framebuffer(dev: hid.device) -> None:
    print("\n=== Hypothesis: remaining 60 bytes are a WS2812 framebuffer ===")
    print("  Alternating RED/BLUE pixels in RGB, GRB, and RGBW")

    def fill(header: list[int], pixel: list[int], n: int) -> list[int]:
        body: list[int] = []
        for i in range(n):
            body += pixel if i % 2 == 0 else list(reversed(pixel)) if len(pixel) == 3 else pixel[::-1]
        return header + body

    layouts = [
        ("hdr 0A 04 + RGB x20 alt", [0x0A, 0x04, 0x00, 0x00], [0xFF, 0x00, 0x00], 20),
        ("hdr 0A 04 + GRB x20 alt", [0x0A, 0x04, 0x00, 0x00], [0x00, 0xFF, 0x00], 20),
        ("no hdr RGB x21 alt", [], [0xFF, 0x00, 0x00], 21),
        ("cmd 0A RGB x20 alt", [0x0A], [0xFF, 0x00, 0x00], 20),
        ("cmd 01 start0 RGB x16", [0x01, 0x00], [0xFF, 0x00, 0x00], 16),
        ("cmd 02 index0 RGB", [0x02, 0x00, 0xFF, 0x00, 0x00], [], 0),
    ]
    for label, header, pixel, n in layouts:
        if n:
            send(dev, fill(header, pixel, n), label)
        else:
            send(dev, header, label)
        time.sleep(0.45)
        set_global(dev, 0x10, 0x00, 0x20, 0, "reset purple")
        time.sleep(0.15)


def phase_opcodes(dev: hid.device) -> None:
    print("\n=== Opcode sweep 0x00-0x2F (look for IN replies, not just STALL) ===")
    hits = []
    for op in range(0x30):
        # distinctive payload so replies are easy to spot
        n = send(dev, [op, 0x04, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00], f"op {op:#04x}")
        replies = drain(dev, ms=20)
        if replies:
            hits.append((op, replies))
        time.sleep(0.02)
    if hits:
        print("  opcodes with extra IN data:")
        for op, replies in hits:
            print(f"    {op:#04x}: {[hexdump(r, 16) for r in replies]}")
    else:
        print("  no opcodes produced IN reports (device may be write-only)")
    set_global(dev, 0x00, 0x20, 0x00, 0, "reset green")


def phase_single_pixel_cmds(dev: hid.device) -> None:
    print("\n=== Try set-pixel style commands (index + RGB/GRB) ===")
    set_global(dev, 0, 0, 0, 0, "all off")
    time.sleep(0.2)
    for op in (0x01, 0x02, 0x03, 0x05, 0x08, 0x09, 0x0B, 0x0C, 0x10, 0x80, 0x81):
        for idx in range(8):
            # op, index, R, G, B
            send(dev, [op, idx, 0xFF, 0x00, 0x00], f"pixel-cmd {op:#04x} idx={idx} RGB red")
            time.sleep(0.12)
        set_global(dev, 0, 0, 0, 0, "all off")
        time.sleep(0.1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--phase",
        choices=["all", "read", "index", "count", "fb", "opcodes", "pixel"],
        default="all",
    )
    args = parser.parse_args()

    dev = hid.device()
    try:
        dev.open(VID, PID)
    except OSError as e:
        print(f"Failed to open Slicky: {e}", file=sys.stderr)
        return 1

    print("Opened", dev.get_manufacturer_string(), dev.get_product_string(), dev.get_serial_number_string())
    dev.set_nonblocking(False)

    try:
        if args.phase in ("all", "read"):
            phase_readonly(dev)
        if args.phase in ("all", "index"):
            phase_index(dev)
        if args.phase in ("all", "count"):
            phase_count(dev)
        if args.phase in ("all", "fb"):
            phase_framebuffer(dev)
        if args.phase in ("all", "opcodes"):
            phase_opcodes(dev)
        if args.phase in ("all", "pixel"):
            phase_single_pixel_cmds(dev)
        set_global(dev, 0x20, 0x20, 0x20, 0, "done: dim white")
    finally:
        dev.close()
    print("\nDone. Watch for: only whole-unit color changes vs individual dots.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
