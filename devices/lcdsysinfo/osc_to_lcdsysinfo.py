#!/usr/bin/env python3
# -*- coding: UTF-8 -*-

import select
import socket
import struct
import sys
import time

from pylcdsysinfo import LCDSysInfo, BackgroundColours, TextLines
from led_display_lib import (
    CHAR_GRID,
    CHAR_MAP,
    CYAN_FIRST_SLOT,
    GREY_FIRST_SLOT,
    GRID,
    ORIGIN_X,
    ORIGIN_Y,
    draw_char,
    layout_positions,
    segment_slots,
)


_DEF_HOST = "localhost"
_DEF_PORT = 9000
_DEF_GAP_SECONDS = 0.2


def _align4(n):
    return (n + 3) & ~3


def _read_osc_string(buf, idx):
    end = buf.find(b"\x00", idx)
    if end == -1:
        return None, idx
    s = buf[idx:end].decode("utf-8", errors="replace")
    idx = _align4(end + 1)
    return s, idx


def _parse_osc_message(data):
    print(f"parse_osc_message")
    if not data:
        return None
    if data.startswith(b"#bundle\x00"):
        return _parse_osc_bundle(data)

    addr, idx = _read_osc_string(data, 0)
    if addr is None:
        return None

    types, idx = _read_osc_string(data, idx)
    if not types or not types.startswith(","):
        return None

    print(f"Message types: {types}")
    for t in types[1:]:
        if t == "s":
            s, idx = _read_osc_string(data, idx)
            if s is None:
                return None
            return s
        if t == "i":
            if idx + 4 > len(data):
                return None
            return struct.unpack_from(">i", data, idx)[0]
        if t == "f":
            if idx + 4 > len(data):
                return None
            return struct.unpack_from(">f", data, idx)[0]
        if t == "d":
            if idx + 8 > len(data):
                return None
            return struct.unpack_from(">d", data, idx)[0]
        if t == "T":
            return True
        if t == "F":
            return False
        if t == "N" or t == "I":
            return None
    return None


def _parse_osc_bundle(data):
    if len(data) < 16:
        return None
    idx = 16
    length = len(data)
    while idx + 4 <= length:
        elem_len = struct.unpack_from(">i", data, idx)[0]
        idx += 4
        if elem_len <= 0 or idx + elem_len > length:
            return None
        elem = data[idx:idx + elem_len]
        idx += elem_len
        value = _parse_osc_message(elem)
        if value is not None:
            return value
    return None


def _display_text(d, seg_slots, char_step, text):
    d.clear_lines(TextLines.ALL, BackgroundColours.BLACK)
    for ch, row, col in layout_positions(text):
        mask = CHAR_MAP.get(ch, CHAR_MAP.get(ch.upper(), 0))
        if not mask:
            continue
        xoff = ORIGIN_X + col * char_step
        yoff = ORIGIN_Y + row * char_step
        draw_char(d, mask, xoff, yoff, seg_slots)


def _format_value(value):
    if isinstance(value, str):
        return value
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return f"{value:g}"
    return str(value)


def _recv_latest_text(recvfrom, parse_message, format_value):
    latest = None
    while True:
        try:
            data, _ = recvfrom(4096)
        except BlockingIOError:
            break
        value = parse_message(data)
        if value is None:
            continue
        text = format_value(value)
        if text:
            latest = text
    return latest


def main(argv):
    host = _DEF_HOST
    port = _DEF_PORT
    gap_seconds = _DEF_GAP_SECONDS
    print(f"Starting LCDsysinfo display OSC server on {host}:{port}")
    use_cyan = False

    i = 1
    while i < len(argv):
        arg = argv[i]
        if arg == "--port" and i + 1 < len(argv):
            port = int(argv[i + 1])
            i += 2
            continue
        if arg == "--host" and i + 1 < len(argv):
            host = argv[i + 1]
            i += 2
            continue
        if arg == "--cyan":
            use_cyan = True
            i += 1
            continue
        if arg == "--gap" and i + 1 < len(argv):
            gap_seconds = float(argv[i + 1])
            i += 2
            continue
        if arg in {"-h", "--help"}:
            print(
                "Usage: osc_to_lcdsysinfo.py [--host HOST] [--port PORT] [--cyan]"
                " [--gap SECONDS]"
            )
            return 0
        i += 1

    d = LCDSysInfo()
    d.clear_lines(TextLines.ALL, BackgroundColours.BLACK)
    d.set_brightness(255)

    first_slot = CYAN_FIRST_SLOT if use_cyan else GREY_FIRST_SLOT
    seg_slots = segment_slots(first_slot)
    char_step = GRID * CHAR_GRID

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((host, port))
    sock.setblocking(False)

    display_text = _display_text
    format_value = _format_value
    recvfrom = sock.recvfrom
    parse_message = _parse_osc_message
    pending_text = None
    last_recv_time = None

    while True:
        timeout = None
        if pending_text is not None and last_recv_time is not None:
            elapsed = time.monotonic() - last_recv_time
            timeout = max(0.0, gap_seconds - elapsed)

        readable, _, _ = select.select([sock], [], [], timeout)
        if readable:
            latest_text = _recv_latest_text(recvfrom, parse_message, format_value)
            if latest_text:
                pending_text = latest_text
                last_recv_time = time.monotonic()
            continue

        if not pending_text:
            continue

        display_text(d, seg_slots, char_step, pending_text)
        pending_text = None
        last_recv_time = None

        latest_text = _recv_latest_text(recvfrom, parse_message, format_value)
        if latest_text:
            pending_text = latest_text
            last_recv_time = time.monotonic()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
