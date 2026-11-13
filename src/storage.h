// Where macropad.json lives: the SD card when one is inserted, otherwise the
// internal flash (LittleFS), so the web configurator works without a card.
#pragma once
#include <Arduino.h>

namespace storage {

void begin();

// Returns the saved config text, or the built-in default when nothing is
// saved yet. `source` describes where it came from (for the screen).
String load(String& source);

// Saves the config text. On the SD card the previous file is kept as
// /macropad.bak. Returns false and sets `error` on failure.
bool save(const String& text, String& where, String& error);

bool hasSd();

}  // namespace storage
