// Typing text: turns characters into key presses for the computer's keyboard
// layout and operating system (see layouts.h for why this is needed).
#pragma once
#include <Arduino.h>

#include "layouts.h"

namespace typing {

enum class Os : uint8_t { Windows, Mac, Linux };

struct Settings {
    const layouts::LayoutDef* layout = nullptr;  // nullptr = English (US)
    Os os            = Os::Windows;
    bool unicode     = false;  // type characters missing from the layout via OS Unicode input
    uint16_t delayMs = 8;
};

const layouts::LayoutDef* findLayout(const String& id);  // nullptr if unknown
const layouts::LayoutDef* defaultLayout();               // English (US)
bool parseOs(const String& name, Os& out);
const char* osId(Os os);    // "windows" / "mac" / "linux"
const char* osName(Os os);  // "Windows" / "macOS" / "Linux"

// Decodes the UTF-8 code point at s[i] and advances i. Returns 0 at the end.
uint32_t nextCodepoint(const String& s, size_t& i);

// Types one character. Returns false if it cannot be typed or nothing is connected.
bool typeChar(uint32_t cp, const Settings& s);
void typeText(const String& text, const Settings& s);

// HID usage of the key that is `c` on a US keyboard (letters/digits/punctuation),
// used for shortcuts (Ctrl+C etc. work by key position). 0 if none.
uint8_t usKeyFor(char c);

}  // namespace typing
