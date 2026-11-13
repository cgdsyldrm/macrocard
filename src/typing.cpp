#include "typing.h"

#include "hid_output.h"
#include "keycodes.h"

namespace typing {
namespace {

using layouts::LayoutCompose;
using layouts::LayoutDef;
using layouts::LayoutKey;

constexpr uint8_t HID_ENTER = 0x28, HID_TAB = 0x2B, HID_SPACE = 0x2C;

const LayoutKey* findKey(const LayoutDef* l, uint32_t cp)
{
    if (!l || cp > 0xFFFF) return nullptr;
    int lo = 0, hi = (int)l->mapLen - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (l->map[mid].cp == cp) return &l->map[mid];
        if (l->map[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

const LayoutCompose* findCompose(const LayoutDef* l, uint32_t cp)
{
    if (!l || cp > 0xFFFF) return nullptr;
    int lo = 0, hi = (int)l->composeLen - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (l->compose[mid].cp == cp) return &l->compose[mid];
        if (l->compose[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

const LayoutDef* effective(const Settings& s)
{
    const LayoutDef* l = s.layout ? s.layout : defaultLayout();
    if (s.os == Os::Mac && l->mac) return l->mac;
    return l;
}

uint8_t hidMods(uint8_t lmods, Os os)
{
    uint8_t m = 0;
    if (lmods & layouts::L_SHIFT) m |= kc::MOD_LSHIFT;
    // AltGr is Right Alt on Windows/Linux; macOS uses Option for the same level.
    if (lmods & layouts::L_ALTGR) m |= (os == Os::Mac) ? kc::MOD_LALT : kc::MOD_RALT;
    return m;
}

uint16_t keyDelay(const Settings& s)
{
    uint16_t d = s.delayMs;
    if (out::activeLink() == out::Link::Ble && d < 12) d = 12;
    return d;
}

bool tap(uint8_t key, uint8_t mods, const Settings& s)
{
    uint8_t keys[6] = {key, 0, 0, 0, 0, 0};
    return out::tap(mods, keys, keyDelay(s));
}

bool hold(uint8_t mods, uint8_t key, const Settings& s)
{
    uint8_t keys[6] = {key, 0, 0, 0, 0, 0};
    bool ok = out::report(mods, keys);
    delay(keyDelay(s));
    return ok;
}

// Key (no modifiers) that types hex digit `c` on the layout; falls back to
// the US position.
uint8_t hexKey(char c, const LayoutDef* l)
{
    const LayoutKey* k = findKey(l, (uint8_t)c);
    if (k && !(k->mods & ~layouts::L_SHIFT)) return k->key;
    return usKeyFor(c);
}

bool typeHexDigits(uint32_t v, int digits, const LayoutDef* l, const Settings& s, bool shiftOk)
{
    static const char* hex = "0123456789abcdef";
    for (int i = digits - 1; i >= 0; --i) {
        char c                = hex[(v >> (4 * i)) & 0xF];
        const LayoutKey* k    = findKey(l, (uint8_t)c);
        uint8_t mods          = (shiftOk && k) ? hidMods(k->mods & layouts::L_SHIFT, s.os) : 0;
        if (!tap(hexKey(c, l), mods, s)) return false;
    }
    return true;
}

// Unicode input built into each OS (see README for the one-time setup).
bool unicodeInput(uint32_t cp, const Settings& s)
{
    const LayoutDef* l = effective(s);
    switch (s.os) {
        case Os::Linux: {
            // Ctrl+Shift+U, hex code, Space (GTK/Qt with IBus).
            const LayoutKey* u = findKey(l, 'u');
            uint8_t uKey       = u ? u->key : 0x18;
            if (!tap(uKey, kc::MOD_LCTRL | kc::MOD_LSHIFT, s)) return false;
            int digits = cp > 0xFFFF ? 6 : 4;
            if (!typeHexDigits(cp, digits, l, s, true)) return false;
            return tap(HID_SPACE, 0, s);
        }
        case Os::Mac: {
            // "Unicode Hex Input" source: hold Option, type 4 hex digits per
            // UTF-16 unit. That input source has US key positions.
            uint16_t units[2];
            int n = 0;
            if (cp > 0xFFFF) {
                uint32_t v = cp - 0x10000;
                units[n++] = 0xD800 | (v >> 10);
                units[n++] = 0xDC00 | (v & 0x3FF);
            } else {
                units[n++] = cp;
            }
            for (int u = 0; u < n; ++u) {
                if (!hold(kc::MOD_LALT, 0, s)) return false;
                for (int i = 3; i >= 0; --i) {
                    char c = "0123456789abcdef"[(units[u] >> (4 * i)) & 0xF];
                    hold(kc::MOD_LALT, usKeyFor(c), s);
                    hold(kc::MOD_LALT, 0, s);
                }
                hold(0, 0, s);
            }
            return true;
        }
        default: {
            // Windows with EnableHexNumpad: hold Alt, numpad +, hex digits
            // (numpad for 0-9), release Alt.
            if (cp > 0xFFFF) return false;
            if (!hold(kc::MOD_LALT, 0, s)) return false;
            hold(kc::MOD_LALT, 0x57, s);  // keypad +
            hold(kc::MOD_LALT, 0, s);
            for (int i = 3; i >= 0; --i) {
                int d       = (cp >> (4 * i)) & 0xF;
                uint8_t key = d == 0 ? 0x62 : d < 10 ? 0x59 + (d - 1) : hexKey("abcdef"[d - 10], l);
                hold(kc::MOD_LALT, key, s);
                hold(kc::MOD_LALT, 0, s);
            }
            hold(0, 0, s);
            return true;
        }
    }
}

}  // namespace

const LayoutDef* findLayout(const String& idIn)
{
    String id = idIn;
    id.trim();
    id.toLowerCase();
    if (id == "en" || id == "en_us") id = "us";
    if (id == "tr_q" || id == "tr_tr") id = "tr";
    for (size_t i = 0; i < layouts::kLayoutCount; ++i) {
        if (id == layouts::kLayouts[i]->id) return layouts::kLayouts[i];
    }
    return nullptr;
}

const LayoutDef* defaultLayout() { return layouts::kLayouts[0]; }

bool parseOs(const String& nameIn, Os& out)
{
    String n = nameIn;
    n.toLowerCase();
    if (n == "windows" || n == "win") { out = Os::Windows; return true; }
    if (n == "mac" || n == "macos" || n == "osx") { out = Os::Mac; return true; }
    if (n == "linux") { out = Os::Linux; return true; }
    return false;
}

const char* osId(Os os) { return os == Os::Mac ? "mac" : os == Os::Linux ? "linux" : "windows"; }
const char* osName(Os os) { return os == Os::Mac ? "macOS" : os == Os::Linux ? "Linux" : "Windows"; }

uint32_t nextCodepoint(const String& s, size_t& i)
{
    if (i >= s.length()) return 0;
    uint8_t c = (uint8_t)s[i++];
    if (c < 0x80) return c;
    int extra;
    uint32_t cp;
    if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07; }
    else return 0xFFFD;
    while (extra-- > 0) {
        if (i >= s.length()) return 0xFFFD;
        uint8_t cc = (uint8_t)s[i];
        if ((cc & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (cc & 0x3F);
        ++i;
    }
    return cp;
}

bool typeChar(uint32_t cp, const Settings& s)
{
    if (cp == '\n' || cp == '\r') return tap(HID_ENTER, 0, s);
    if (cp == '\t') return tap(HID_TAB, 0, s);

    const LayoutDef* l = effective(s);
    if (const LayoutKey* k = findKey(l, cp)) {
        if (!tap(k->key, hidMods(k->mods, s.os), s)) return false;
        if (k->mods & layouts::L_DEAD) tap(HID_SPACE, 0, s);  // dead key: accent alone
        return true;
    }
    if (const LayoutCompose* c = findCompose(l, cp)) {
        if (!tap(c->deadKey, hidMods(c->deadMods, s.os), s)) return false;
        return tap(c->baseKey, hidMods(c->baseMods, s.os), s);
    }
    if (s.unicode) return unicodeInput(cp, s);
    return false;
}

void typeText(const String& text, const Settings& s)
{
    size_t i = 0;
    while (i < text.length()) {
        uint32_t cp = nextCodepoint(text, i);
        if (cp == '\r') continue;
        typeChar(cp, s);  // characters that cannot be typed are skipped
        if (out::activeLink() == out::Link::None) return;
    }
}

uint8_t usKeyFor(char c)
{
    const LayoutKey* k = findKey(defaultLayout(), (uint8_t)c);
    return k ? k->key : 0;
}

}  // namespace typing
