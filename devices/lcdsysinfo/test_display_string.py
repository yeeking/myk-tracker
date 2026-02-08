#!/usr/bin/env python3
# -*- coding: UTF-8 -*-

import sys

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


def display_text_segments(text, use_cyan=False):
    d = LCDSysInfo()
    d.clear_lines(TextLines.ALL, BackgroundColours.BLACK)
    d.set_brightness(255)

    first_slot = CYAN_FIRST_SLOT if use_cyan else GREY_FIRST_SLOT
    seg_slots = segment_slots(first_slot)

    char_step = GRID * CHAR_GRID
    for ch, row, col in layout_positions(text):
        mask = CHAR_MAP.get(ch, CHAR_MAP.get(ch.upper(), 0))
        xoff = ORIGIN_X + col * char_step
        yoff = ORIGIN_Y + row * char_step
        draw_char(d, mask, xoff, yoff, seg_slots)


def main(argv):
    if len(argv) < 2:
        print("Usage: python segment14-display.py [--cyan] 'TEXT'")
        print("Tip: use '-' or newline to force a line break.")
        return 2
    use_cyan = False
    args = argv[1:]
    if args and args[0] == "--cyan":
        use_cyan = True
        args = args[1:]
    text = " ".join(args)
    display_text_segments(text, use_cyan=use_cyan)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
