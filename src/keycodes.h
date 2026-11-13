// Key name / HID usage tables and text-to-keystroke conversion.
// No Cardputer or USB/BLE headers here: those libraries define clashing
// KEY_* macros, so each one lives in its own translation unit.
#pragma once
#include <Arduino.h>

namespace kc {

constexpr uint8_t MOD_LCTRL  = 0x01;
constexpr uint8_t MOD_LSHIFT = 0x02;
constexpr uint8_t MOD_LALT   = 0x04;
constexpr uint8_t MOD_LGUI   = 0x08;
constexpr uint8_t MOD_RCTRL  = 0x10;
constexpr uint8_t MOD_RSHIFT = 0x20;
constexpr uint8_t MOD_RALT   = 0x40;  // AltGr
constexpr uint8_t MOD_RGUI   = 0x80;

constexpr uint8_t HID_ENTER = 0x28;
constexpr uint8_t HID_TAB   = 0x2b;
constexpr uint8_t HID_SPACE = 0x2c;

struct Combo {
    uint8_t mod     = 0;
    uint8_t keys[6] = {0, 0, 0, 0, 0, 0};
    uint8_t count   = 0;
};

// Parses "ctrl+shift+p", "f13", "win+d", "kp1", "0x68" ... into a HID report.
// Single punctuation characters refer to the key's position on a US keyboard.
bool parseCombo(const String& spec, Combo& out, String& err);

// Parses a media/consumer key name ("volup", "mute", "play", "0xcd" ...).
bool parseConsumer(const String& name, uint16_t& usage);

}  // namespace kc
