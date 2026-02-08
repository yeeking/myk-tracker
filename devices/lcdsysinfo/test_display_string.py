#!/usr/bin/env python3
# -*- coding: UTF-8 -*-

import sys

from pylcdsysinfo import LCDSysInfo, BackgroundColours, TextLines

# Icon configuration mirrors load_icons.py
ICON_NAMES = [
    "horizontal_bar_left",
    "horizontal_bar_right",
    "horizontal_bar_short",
    "vertical_bar_long",
    "diagonal_top_left_to_bottom_right",
    "diagonal_bottom_left_to_top_right",
]

SEGMENT_TO_ICON = {
    "A1": "horizontal_bar_left",
    "A2": "horizontal_bar_right",
    "B": "vertical_bar_long",
    "C": "vertical_bar_long",
    "D1": "horizontal_bar_left",
    "D2": "horizontal_bar_right",
    "E": "vertical_bar_long",
    "F": "vertical_bar_long",
    "G1": "horizontal_bar_short",
    "G2": "horizontal_bar_short",
    "H": "diagonal_top_left_to_bottom_right",
    "I": "diagonal_bottom_left_to_top_right",
    "J": "diagonal_bottom_left_to_top_right",
    "K": "diagonal_top_left_to_bottom_right",
    "L": "vertical_bar_long",
    "M": "vertical_bar_long",
}

# The grid spacing for segment placement.
GRID = 20
SEGMENT_XYS = {
    "A1": (1, 0),
    "A2": (3, 0),
    "B": (4, 1),
    "C": (4, 3),
    "D1": (1, 4),
    "D2": (3, 4),
    "E": (0, 3),
    "F": (0, 1),
    "G1": (1, 2),
    "G2": (3, 2),
    "H": (1, 1),
    "I": (3, 1),
    "J": (1, 3),
    "K": (3, 3),
    "L": (2, 1),
    "M": (2, 3),
}

# Layout: 5x5 grid per character, 3 columns x 2 rows on the display.
CHAR_GRID = 5
COLS = 3
ROWS = 2
ORIGIN_X = 0
ORIGIN_Y = 0

GREY_FIRST_SLOT = 1
CYAN_FIRST_SLOT = GREY_FIRST_SLOT + len(ICON_NAMES)

# Segment bit positions (from info/Segment14Geometry.h)
SEG = {
    "A": 0,
    "B": 1,
    "C": 2,
    "D": 3,
    "E": 4,
    "F": 5,
    "G1": 6,
    "G2": 7,
    "H": 8,
    "I": 9,
    "J": 10,
    "K": 11,
    "L": 12,
    "M": 13,
    "DP": 14,
}


def bits(*segs):
    mask = 0
    for s in segs:
        mask |= 1 << SEG[s]
    return mask


# Character map copied from info/Segment14Geometry.h
CHAR_MAP = {
    # Digits
    "0": bits("A", "B", "C", "D", "E", "F"),
    "1": bits("B", "C"),
    "2": bits("A", "B", "D", "E", "G1", "G2"),
    "3": bits("A", "B", "C", "D", "G1", "G2"),
    "4": bits("B", "C", "F", "G1", "G2"),
    "5": bits("A", "C", "D", "F", "G1", "G2"),
    "6": bits("A", "C", "D", "E", "F", "G1", "G2"),
    "7": bits("A", "B", "C"),
    "8": bits("A", "B", "C", "D", "E", "F", "G1", "G2"),
    "9": bits("A", "B", "C", "D", "F", "G1", "G2"),

    # Punctuation
    "-": bits("G1", "G2"),
    "_": bits("D"),
    ".": bits("DP"),
    ":": bits("DP"),
    "/": bits("I", "K"),
    "\\": bits("H", "J"),
    "@": bits("A", "B", "C", "D", "E", "F", "G1", "G2", "H", "I", "J", "K", "L", "M", "DP"),
    " ": 0,

    # Uppercase A-Z
    "A": bits("A", "B", "C", "E", "F", "G1", "G2"),
    "B": bits("A", "F", "B", "G1", "G2", "E", "C", "D"),
    "C": bits("A", "D", "E", "F"),
    "D": bits("A", "B", "C", "D", "E", "F"),
    "d": bits("E", "C", "D", "G1", "G2", "B"),
    "E": bits("A", "D", "E", "F", "G1", "G2"),
    "F": bits("A", "E", "F", "G1", "G2"),
    "G": bits("A", "C", "D", "E", "F", "G2"),
    "H": bits("B", "C", "E", "F", "G1", "G2"),
    "I": bits("A", "D", "L", "M"),
    "J": bits("B", "C", "D", "E"),
    "K": bits("F", "E", "G1", "I", "K"),
    "L": bits("D", "E", "F"),
    "M": bits("B", "C", "E", "F", "H", "I"),
    "N": bits("B", "C", "E", "F", "H", "K"),
    "O": bits("A", "B", "C", "D", "E", "F"),
    "P": bits("A", "B", "E", "F", "G1", "G2"),
    "Q": bits("A", "B", "C", "D", "E", "F", "K"),
    "R": bits("A", "B", "E", "F", "G1", "G2", "K"),
    "S": bits("A", "C", "D", "F", "G1", "G2"),
    "T": bits("A", "L", "M"),
    "U": bits("B", "C", "D", "E", "F"),
    "V": bits("H", "K", "C", "B"),
    "W": bits("B", "C", "E", "F", "J", "K"),
    "X": bits("H", "I", "J", "K"),
    "Y": bits("H", "I", "M"),
    "Z": bits("A", "D", "I", "J"),
}

# Lowercase maps to uppercase by default
for c in range(ord("a"), ord("z") + 1):
    ch = chr(c)
    if ch not in CHAR_MAP:
        CHAR_MAP[ch] = CHAR_MAP.get(chr(c - 32), 0)


def _name_to_slot(first_slot):
    slots = {}
    slot = first_slot
    for name in ICON_NAMES:
        slots[name] = slot
        slot += 1
    return slots


def _segment_slots(first_slot):
    name_to_slot = _name_to_slot(first_slot)
    return {seg: name_to_slot[name] for seg, name in SEGMENT_TO_ICON.items()}


def _segments_for_bit(seg_name):
    if seg_name == "A":
        return ("A1", "A2")
    if seg_name == "D":
        return ("D1", "D2")
    return (seg_name,)


def _draw_char(d, mask, x0, y0, seg_slots):
    for seg_name, seg_bit in SEG.items():
        if seg_name == "DP":
            continue
        if not (mask & (1 << seg_bit)):
            continue
        for part in _segments_for_bit(seg_name):
            xy = SEGMENT_XYS.get(part)
            if not xy:
                continue
            icon_no = seg_slots.get(part)
            if icon_no is None:
                continue
            d.display_icon_anywhere(x0 + xy[0] * GRID, y0 + xy[1] * GRID, icon_no)


def _layout_positions(text):
    positions = []
    row = 0
    col = 0
    for ch in text:
        if ch == "\n" or ch == "-":
            row += 1
            col = 0
            if row >= ROWS:
                break
            continue
        positions.append((ch, row, col))
        col += 1
        if col >= COLS:
            row += 1
            col = 0
            if row >= ROWS:
                break
    return positions


def display_text_segments(text, use_cyan=False):
    d = LCDSysInfo()
    d.clear_lines(TextLines.ALL, BackgroundColours.BLACK)
    d.set_brightness(255)

    first_slot = CYAN_FIRST_SLOT if use_cyan else GREY_FIRST_SLOT
    seg_slots = _segment_slots(first_slot)

    char_step = GRID * CHAR_GRID
    for ch, row, col in _layout_positions(text):
        mask = CHAR_MAP.get(ch, CHAR_MAP.get(ch.upper(), 0))
        xoff = ORIGIN_X + col * char_step
        yoff = ORIGIN_Y + row * char_step
        _draw_char(d, mask, xoff, yoff, seg_slots)


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
