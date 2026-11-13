// Keyboard layout tables (generated from xkeyboard-config by
// tools/gen_layouts.py): which key + modifiers produce a character on the
// computer's layout.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace layouts {

constexpr uint8_t L_SHIFT = 0x01;
constexpr uint8_t L_ALTGR = 0x02;  // AltGr on Windows/Linux, Option on macOS
constexpr uint8_t L_DEAD  = 0x04;  // dead key: press space afterwards

struct LayoutKey {
    uint16_t cp;  // Unicode code point (sorted ascending)
    uint8_t key;  // USB HID usage
    uint8_t mods; // L_* flags
};

// Character typed as dead key followed by a base key (e.g. ^ then e = ê).
struct LayoutCompose {
    uint16_t cp;
    uint8_t deadKey, deadMods;
    uint8_t baseKey, baseMods;
};

struct LayoutDef {
    const char* id;
    const char* name;
    const LayoutKey* map;
    uint16_t mapLen;
    const LayoutCompose* compose;
    uint16_t composeLen;
    const LayoutDef* mac;  // macOS variant, if it differs
};

extern const LayoutDef* const kLayouts[];
extern const size_t kLayoutCount;

}  // namespace layouts
