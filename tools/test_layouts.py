#!/usr/bin/env python3
"""Checks gen_layouts.py output against known Windows layout facts.

Usage: python tools/test_layouts.py <xkeyboard-config>/symbols <keysymdef.h>
"""
import sys

import gen_layouts as g

S, A = g.SHIFT, g.ALTGR

# character -> (HID usage, modifiers) as on Windows
FACTS = {
    ("us", None): {"@": (0x1F, S), "a": (0x04, 0), '"': (0x34, S)},
    ("tr", None): {
        "@": (0x14, A), "ı": (0x0C, 0), "i": (0x34, 0), "I": (0x0C, S), "İ": (0x34, S),
        '"': (0x35, 0), "#": (0x20, A), "<": (0x64, 0), "ş": (0x33, 0), "ğ": (0x2F, 0),
        "ü": (0x30, 0), "ö": (0x36, 0), "ç": (0x37, 0), ",": (0x31, 0), ".": (0x38, 0),
        "{": (0x24, A), "\\": (0x2D, A), "$": (0x21, A),
    },
    ("de", None): {"z": (0x1C, 0), "y": (0x1D, 0), "@": (0x14, A), "€": (0x08, A),
                   "ä": (0x34, 0), "ß": (0x2D, 0), "{": (0x24, A)},
    ("fr", None): {"a": (0x14, 0), "q": (0x04, 0), "1": (0x1E, S), "@": (0x27, A),
                   "é": (0x1F, 0), "m": (0x33, 0)},
    ("gb", None): {"@": (0x34, S), '"': (0x1F, S), "£": (0x20, S)},
    ("es", None): {"ñ": (0x33, 0), "@": (0x1F, A)},
    ("ru", None): {"й": (0x14, 0), "ф": (0x04, 0)},
    ("jp", None): {"@": (0x2F, 0), '"': (0x1F, S)},
}
COMPOSE = {("fr", None): {"ê": ((0x2F, 0), (0x08, 0))}}  # ê = ^ then e


def main():
    symdir, kd = sys.argv[1], sys.argv[2]
    ks = g.keysym_table(kd)
    bad = 0
    for (f, v), exp in FACTS.items():
        entries, compose = g.build(g.resolve(symdir, f, v), ks)
        for ch, want in exp.items():
            got = entries.get(ord(ch))
            if got != want:
                bad += 1
                print(f"MISMATCH {f} U+{ord(ch):04X}: expected {want} got {got}")
        for ch, want in COMPOSE.get((f, v), {}).items():
            got = compose.get(ord(ch))
            if got != want:
                bad += 1
                print(f"MISMATCH {f} compose U+{ord(ch):04X}: expected {want} got {got}")
    print("checks failed:", bad)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
