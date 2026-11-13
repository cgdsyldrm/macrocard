#include "keycodes.h"

namespace kc {
namespace {

struct Named {
    const char* name;
    uint8_t code;
};

const Named kKeys[] = {
    {"enter", 0x28},      {"return", 0x28},     {"esc", 0x29},         {"escape", 0x29},
    {"backspace", 0x2a},  {"bksp", 0x2a},       {"tab", 0x2b},         {"space", 0x2c},
    {"minus", 0x2d},      {"equal", 0x2e},      {"lbracket", 0x2f},    {"rbracket", 0x30},
    {"backslash", 0x31},  {"semicolon", 0x33},  {"quote", 0x34},       {"grave", 0x35},
    {"comma", 0x36},      {"period", 0x37},     {"dot", 0x37},         {"slash", 0x38},
    {"capslock", 0x39},   {"printscreen", 0x46},{"prtsc", 0x46},       {"scrolllock", 0x47},
    {"pause", 0x48},      {"insert", 0x49},     {"ins", 0x49},         {"home", 0x4a},
    {"pageup", 0x4b},     {"pgup", 0x4b},       {"delete", 0x4c},      {"del", 0x4c},
    {"end", 0x4d},        {"pagedown", 0x4e},   {"pgdn", 0x4e},        {"right", 0x4f},
    {"left", 0x50},       {"down", 0x51},       {"up", 0x52},          {"numlock", 0x53},
    {"kp_slash", 0x54},   {"kp_star", 0x55},    {"kp_minus", 0x56},    {"kp_plus", 0x57},
    {"kp_enter", 0x58},   {"kp_dot", 0x63},     {"iso_backslash", 0x64},
    {"menu", 0x65},       {"app", 0x65},
};

const Named kMods[] = {
    {"ctrl", MOD_LCTRL},   {"control", MOD_LCTRL}, {"lctrl", MOD_LCTRL},
    {"shift", MOD_LSHIFT}, {"lshift", MOD_LSHIFT},
    {"alt", MOD_LALT},     {"lalt", MOD_LALT},     {"option", MOD_LALT},
    {"win", MOD_LGUI},     {"gui", MOD_LGUI},      {"cmd", MOD_LGUI},
    {"meta", MOD_LGUI},    {"super", MOD_LGUI},    {"lgui", MOD_LGUI},
    {"rctrl", MOD_RCTRL},  {"rshift", MOD_RSHIFT},
    {"ralt", MOD_RALT},    {"altgr", MOD_RALT},
    {"rgui", MOD_RGUI},    {"rwin", MOD_RGUI},
};

struct NamedUsage {
    const char* name;
    uint16_t usage;
};

const NamedUsage kConsumer[] = {
    {"volup", 0x00E9},       {"volume_up", 0x00E9},   {"voldown", 0x00EA},
    {"volume_down", 0x00EA}, {"mute", 0x00E2},        {"play", 0x00CD},
    {"playpause", 0x00CD},   {"next", 0x00B5},        {"prev", 0x00B6},
    {"previous", 0x00B6},    {"stop", 0x00B7},        {"bright_up", 0x006F},
    {"bright_down", 0x0070}, {"calculator", 0x0192},  {"browser", 0x0196},
    {"mail", 0x018A},        {"mycomputer", 0x0194},
};

// Unshifted punctuation keys by their US-layout character (physical position).
struct PosChar {
    char c;
    uint8_t code;
};
const PosChar kUsPositions[] = {
    {'-', 0x2d}, {'=', 0x2e}, {'[', 0x2f}, {']', 0x30}, {'\\', 0x31}, {';', 0x33},
    {'\'', 0x34}, {'`', 0x35}, {',', 0x36}, {'.', 0x37}, {'/', 0x38},
};

String lower(String s)
{
    s.trim();
    s.toLowerCase();
    return s;
}

bool parseHex(const String& s, uint32_t& v)
{
    if (!s.startsWith("0x") || s.length() < 3) return false;
    char* end   = nullptr;
    v           = strtoul(s.c_str() + 2, &end, 16);
    return end && *end == '\0';
}

bool keyCode(const String& t, uint8_t& code)
{
    for (const auto& k : kKeys) {
        if (t == k.name) {
            code = k.code;
            return true;
        }
    }
    if (t.length() == 1) {
        char c = t[0];
        if (c >= 'a' && c <= 'z') { code = 0x04 + (c - 'a'); return true; }
        if (c >= '1' && c <= '9') { code = 0x1e + (c - '1'); return true; }
        if (c == '0') { code = 0x27; return true; }
        for (const auto& p : kUsPositions) {
            if (p.c == c) { code = p.code; return true; }
        }
        return false;
    }
    if (t[0] == 'f' && t.length() <= 3) {
        int n = t.substring(1).toInt();
        if (n >= 1 && n <= 12) { code = 0x3a + (n - 1); return true; }
        if (n >= 13 && n <= 24) { code = 0x68 + (n - 13); return true; }
    }
    if (t.length() == 3 && t.startsWith("kp") && isDigit(t[2])) {
        int n = t[2] - '0';
        code  = (n == 0) ? 0x62 : 0x59 + (n - 1);
        return true;
    }
    uint32_t v;
    if (parseHex(t, v) && v > 0 && v <= 0xff) {
        code = (uint8_t)v;
        return true;
    }
    return false;
}

}  // namespace

bool parseCombo(const String& specIn, Combo& out, String& err)
{
    out          = Combo{};
    String spec  = lower(specIn);
    if (spec.isEmpty()) {
        err = "empty key";
        return false;
    }
    int start = 0;
    while (start <= (int)spec.length()) {
        int plus = spec.indexOf('+', start);
        // A trailing "+" (e.g. "ctrl++") means the '+' character itself is not
        // supported; use "shift+equal" instead.
        String tok = spec.substring(start, plus < 0 ? spec.length() : plus);
        tok.trim();
        if (tok.isEmpty()) {
            err = "'" + specIn + "': empty part";
            return false;
        }
        bool isMod = false;
        for (const auto& m : kMods) {
            if (tok == m.name) {
                out.mod |= m.code;
                isMod = true;
                break;
            }
        }
        if (!isMod) {
            uint8_t code;
            if (!keyCode(tok, code)) {
                err = "'" + specIn + "': unknown key '" + tok + "'";
                return false;
            }
            if (out.count >= 6) {
                err = "'" + specIn + "': at most 6 keys";
                return false;
            }
            out.keys[out.count++] = code;
        }
        if (plus < 0) break;
        start = plus + 1;
    }
    return true;
}

bool parseConsumer(const String& nameIn, uint16_t& usage)
{
    String name = lower(nameIn);
    for (const auto& c : kConsumer) {
        if (name == c.name) {
            usage = c.usage;
            return true;
        }
    }
    uint32_t v;
    if (parseHex(name, v) && v > 0 && v <= 0x3ff) {
        usage = (uint16_t)v;
        return true;
    }
    return false;
}

}  // namespace kc
