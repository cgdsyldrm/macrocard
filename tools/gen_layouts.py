#!/usr/bin/env python3
"""Generate keyboard layout tables for MacroCard from xkeyboard-config.

A USB/Bluetooth keyboard can only send key *positions*; the computer turns
them into characters with its own layout. To type a character the pad must
know which key (+Shift/AltGr) produces it on the computer's layout. These
tables come from the same database Linux uses (xkeyboard-config); Windows
layouts of the same name match it for practically all characters.

Usage (run once, output is committed):
  python tools/gen_layouts.py <xkeyboard-config>/symbols <keysymdef.h>

Sources:
  https://www.x.org/releases/individual/data/xkeyboard-config/
  https://gitlab.freedesktop.org/xorg/proto/xorgproto (include/X11/keysymdef.h)
"""
import re
import sys
import unicodedata
from pathlib import Path

# (id, xkb file, xkb variant or None for default, display name, mac variant)
LAYOUTS = [
    ("us", "us", None, "English (US)", "mac"),
    ("us-intl", "us", "intl", "English (US, international)", None),
    ("gb", "gb", None, "English (UK)", "mac"),
    ("tr", "tr", None, "Turkish Q", None),
    ("tr-f", "tr", "f", "Turkish F", None),
    ("de", "de", None, "German", "mac"),
    ("ch-de", "ch", None, "German (Switzerland)", "de_mac"),
    ("ch-fr", "ch", "fr", "French (Switzerland)", "fr_mac"),
    ("fr", "fr", None, "French (AZERTY)", "mac"),
    ("be", "be", None, "Belgian", None),
    ("ca-fr", "ca", None, "French (Canada)", None),
    ("es", "es", None, "Spanish", None),
    ("latam", "latam", None, "Spanish (Latin America)", None),
    ("it", "it", None, "Italian", "mac"),
    ("pt", "pt", None, "Portuguese", "mac"),
    ("br", "br", None, "Portuguese (Brazil)", None),
    ("nl", "nl", None, "Dutch", "mac"),
    ("se", "se", None, "Swedish", "mac"),
    ("fi", "fi", "classic", "Finnish", "mac"),
    ("dk", "dk", None, "Danish", "mac"),
    ("no", "no", None, "Norwegian", "mac"),
    ("is", "is", None, "Icelandic", None),
    ("pl", "pl", None, "Polish (programmers)", None),
    ("cz", "cz", None, "Czech", None),
    ("sk", "sk", None, "Slovak", None),
    ("hu", "hu", None, "Hungarian", None),
    ("hr", "hr", None, "Croatian", None),
    ("si", "si", None, "Slovenian", None),
    ("ro", "ro", None, "Romanian", None),
    ("ee", "ee", None, "Estonian", None),
    ("lv", "lv", None, "Latvian", None),
    ("lt", "lt", None, "Lithuanian", None),
    ("gr", "gr", None, "Greek", None),
    ("ru", "ru", None, "Russian", None),
    ("ua", "ua", None, "Ukrainian", None),
    ("bg", "bg", None, "Bulgarian", None),
    ("il", "il", None, "Hebrew", None),
    ("ara", "ara", None, "Arabic", None),
    ("jp", "jp", None, "Japanese (JIS)", None),
]

# xkb key name -> USB HID usage (keyboard page)
HID = {
    "TLDE": 0x35, "AE01": 0x1E, "AE02": 0x1F, "AE03": 0x20, "AE04": 0x21, "AE05": 0x22,
    "AE06": 0x23, "AE07": 0x24, "AE08": 0x25, "AE09": 0x26, "AE10": 0x27, "AE11": 0x2D,
    "AE12": 0x2E, "AE13": 0x89,
    "AD01": 0x14, "AD02": 0x1A, "AD03": 0x08, "AD04": 0x15, "AD05": 0x17, "AD06": 0x1C,
    "AD07": 0x18, "AD08": 0x0C, "AD09": 0x12, "AD10": 0x13, "AD11": 0x2F, "AD12": 0x30,
    "AC01": 0x04, "AC02": 0x16, "AC03": 0x07, "AC04": 0x09, "AC05": 0x0A, "AC06": 0x0B,
    "AC07": 0x0D, "AC08": 0x0E, "AC09": 0x0F, "AC10": 0x33, "AC11": 0x34, "AC12": 0x32,
    "BKSL": 0x31, "LSGT": 0x64,
    "AB01": 0x1D, "AB02": 0x1B, "AB03": 0x06, "AB04": 0x19, "AB05": 0x05, "AB06": 0x11,
    "AB07": 0x10, "AB08": 0x36, "AB09": 0x37, "AB10": 0x38, "AB11": 0x87,
    "SPCE": 0x2C,
}
KEY_ORDER = list(HID)  # preference order when a character is on several keys

SHIFT, ALTGR, DEAD = 0x01, 0x02, 0x04
LEVEL_MODS = [0, SHIFT, ALTGR, ALTGR | SHIFT]

# dead keysym -> (combining mark, spacing character typed by dead key + space)
DEAD_KEYS = {
    "dead_grave": (0x0300, 0x0060), "dead_acute": (0x0301, 0x00B4),
    "dead_circumflex": (0x0302, 0x005E), "dead_tilde": (0x0303, 0x007E),
    "dead_macron": (0x0304, 0x00AF), "dead_breve": (0x0306, 0x02D8),
    "dead_abovedot": (0x0307, 0x02D9), "dead_diaeresis": (0x0308, 0x00A8),
    "dead_abovering": (0x030A, 0x02DA), "dead_doubleacute": (0x030B, 0x02DD),
    "dead_caron": (0x030C, 0x02C7), "dead_cedilla": (0x0327, 0x00B8),
    "dead_ogonek": (0x0328, 0x02DB),
}

# ------------------------------------------------------------------ parsing


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


_file_cache = {}


def load_file(symdir, name):
    if name in _file_cache:
        return _file_cache[name]
    path = Path(symdir) / name
    text = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
    blocks, default = {}, None
    for m in re.finditer(r'((?:[\w]+\s+)*)xkb_symbols\s+"([^"]+)"\s*\{', text):
        flags, vname = m.group(1).split(), m.group(2)
        depth, i = 1, m.end()
        while depth and i < len(text):
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        blocks[vname] = text[m.end():i - 1]
        if "default" in flags and default is None:
            default = vname
        if default is None and len(blocks) == 1:
            first = vname
    result = (blocks, default or next(iter(blocks)))
    _file_cache[name] = result
    return result


def keysym_table(keysymdef):
    table = {}
    for line in Path(keysymdef).read_text(encoding="utf-8", errors="replace").splitlines():
        m = re.match(r"#define XK_(\w+)\s+0x([0-9a-fA-F]+)\s*/\*[ (]*U\+([0-9A-Fa-f]{4,6})", line)
        if m:
            table.setdefault(m.group(1), int(m.group(3), 16))
    return table


def sym_to_cp(sym, ks):
    if sym in ks:
        return ks[sym]
    m = re.fullmatch(r"U([0-9A-Fa-f]{4,6})", sym)
    if m:
        return int(m.group(1), 16)
    m = re.fullmatch(r"0x0*1([0-9A-Fa-f]{6})", sym)
    if m:
        return int(m.group(1), 16)
    return None


def parse_key_syms(body):
    """First group's symbol list from a key { ... } body."""
    m = re.search(r"symbols\s*\[\s*group1\s*\]\s*=\s*\[([^\]]*)\]", body, re.I)
    if not m:
        m = re.search(r"(?<![\w\]])\[([^\]]*)\]", re.sub(r"\w+\s*\[\s*\w+\s*\]\s*=\s*\"[^\"]*\"", "", body))
    if not m:
        return None
    return [s.strip() for s in m.group(1).split(",")]


KEEP = ("any", "NoSymbol")  # xkb: keep what an earlier definition had


def merge(old, new):
    """New key definition over an old one; 'any'/'NoSymbol' keep old levels.
    The new list's length decides how many levels the key has."""
    # Keep the 'any' markers when there is nothing older: an including file
    # may still have something underneath. build() treats leftovers as empty.
    if not old:
        return list(new)
    return [(old[i] if i < len(old) else s) if s in KEEP else s for i, s in enumerate(new)]


def resolve(symdir, fname, variant, depth=0):
    """Returns {key name: [level syms]} after applying includes."""
    if depth > 12:
        return {}
    blocks, default = load_file(symdir, fname)
    body = blocks[variant or default]
    keys = {}
    pos = 0
    stmt_re = re.compile(
        r'(include|augment|override|replace)\s+"([^"]+)"'
        r'|((?:replace|override|augment)\s+)?key\s*<(\w+)>\s*\{(.*?)\}\s*;', re.S)
    for m in stmt_re.finditer(body):
        if m.group(1):
            mode = m.group(1)
            for part in re.split(r"[+|]", m.group(2)):
                part = part.strip()
                if not part:
                    continue
                pm = re.fullmatch(r"([\w./-]+)(?:\(([\w-]+)\))?(?::\d+)?", part)
                if not pm or not (Path(symdir) / pm.group(1)).exists():
                    continue
                sub = resolve(symdir, pm.group(1), pm.group(2), depth + 1)
                for k, v in sub.items():
                    if mode == "augment" and k in keys:
                        continue
                    keys[k] = merge(keys.get(k), v)
        else:
            kmode = (m.group(3) or "").strip()
            name, kbody = m.group(4), m.group(5)
            syms = parse_key_syms(kbody)
            if syms is None:
                continue
            if kmode == "augment" and name in keys:
                continue
            keys[name] = merge(keys.get(name), syms)
    return keys


# --------------------------------------------------------------- building


def build(keys, ks):
    direct, dead = {}, {}
    for level in range(4):
        for kname in KEY_ORDER:
            syms = keys.get(kname)
            if not syms or level >= len(syms):
                continue
            sym = syms[level]
            if sym in ("", "none", "any", "NoSymbol", "VoidSymbol"):
                continue
            hid, mods = HID[kname], LEVEL_MODS[level]
            if sym in DEAD_KEYS:
                comb, spacing = DEAD_KEYS[sym]
                dead.setdefault(comb, (hid, mods))
                continue
            cp = sym_to_cp(sym, ks)
            if cp is None or cp < 0x20 or cp > 0xFFFF:
                continue
            direct.setdefault(cp, (hid, mods))
    direct.setdefault(0x20, (0x2C, 0))
    entries = {cp: (hid, mods) for cp, (hid, mods) in direct.items()}
    # Spacing accents reachable only as dead key + space.
    for sym, (comb, spacing) in DEAD_KEYS.items():
        if comb in dead and spacing not in entries:
            hid, mods = dead[comb]
            entries[spacing] = (hid, mods | DEAD)
    # Accented letters reachable as dead key + base letter.
    compose = {}
    for cp in list(range(0x00C0, 0x0250)) + list(range(0x1E00, 0x1F00)):
        if cp in entries:
            continue
        nfd = unicodedata.normalize("NFD", chr(cp))
        if len(nfd) != 2:
            continue
        base, comb = ord(nfd[0]), ord(nfd[1])
        if comb in dead and base in entries and not entries[base][1] & DEAD:
            compose[cp] = (dead[comb], entries[base])
    return entries, compose


def c_name(lid):
    return "k_" + re.sub(r"\W", "_", lid)


def main():
    symdir, keysymdef = sys.argv[1], sys.argv[2]
    ks = keysym_table(keysymdef)
    out_dir = Path(__file__).resolve().parent.parent / "src"
    defs, tables, report = [], [], []

    def emit(lid, fname, variant):
        keys = resolve(symdir, fname, variant)
        entries, compose = build(keys, ks)
        name = c_name(lid)
        rows = ", ".join(f"{{0x{cp:04X},0x{h:02X},{m}}}" for cp, (h, m) in sorted(entries.items()))
        crow = ", ".join(
            f"{{0x{cp:04X},0x{d[0]:02X},{d[1]},0x{b[0]:02X},{b[1]}}}" for cp, (d, b) in sorted(compose.items()))
        tables.append(f"static const LayoutKey {name}_map[] = {{{rows}}};")
        tables.append(f"static const LayoutCompose {name}_compose[] = {{{crow or '{0,0,0,0,0}'}}};")
        ascii_missing = "".join(chr(c) for c in range(0x21, 0x7F) if c not in entries)
        report.append(f"{lid:8s} {len(entries):4d} chars, {len(compose):3d} composed, ASCII missing: {ascii_missing!r}")
        return name, len(entries), len(compose)

    for lid, fname, variant, title, mac in LAYOUTS:
        n, ne, nc = emit(lid, fname, variant)
        mac_ref = "nullptr"
        if mac:
            mn, mne, mnc = emit(lid + "-mac", fname, mac)
            mac_ref = f"&{mn}_def"
            defs.append(f"static const LayoutDef {mn}_def = {{\"{lid}-mac\", \"{title} (Mac)\", "
                        f"{mn}_map, {mne}, {mn}_compose, {mnc}, nullptr}};")
        defs.append(f"static const LayoutDef {n}_def = {{\"{lid}\", \"{title}\", {n}_map, {ne}, "
                    f"{n}_compose, {nc}, {mac_ref}}};")

    order = ", ".join(f"&{c_name(l[0])}_def" for l in LAYOUTS)
    cpp = [
        "// Generated by tools/gen_layouts.py from xkeyboard-config - do not edit.",
        '#include "layouts.h"',
        "",
        "namespace layouts {",
        "namespace {",
        *tables,
        *defs,
        "}  // namespace",
        f"const LayoutDef* const kLayouts[] = {{{order}}};",
        f"const size_t kLayoutCount = {len(LAYOUTS)};",
        "}  // namespace layouts",
        "",
    ]
    (out_dir / "layouts.generated.cpp").write_text("\n".join(cpp), encoding="utf-8")
    print("\n".join(report))


if __name__ == "__main__":
    main()
