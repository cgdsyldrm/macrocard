// Screen rendering (240x135).
#pragma once
#include <Arduino.h>

#include "macros.h"

namespace ui {

struct View {
    const Config* cfg  = nullptr;
    size_t profile     = 0;
    int scroll         = 0;     // first visible row
    String flashKey;            // macro key to highlight
    String footer;              // status / last action text
    bool footerAlert   = false; // draw footer in warning colour
    const char* link   = "---";
    const char* mode   = "AUTO";
    int battery        = -1;    // percent, -1 = unknown
    bool webOn         = false; // Wi-Fi web server online
};

void begin();
void setBrightness(uint8_t level);
void render(const View& v);
void message(const String& title, const String& body, uint32_t color);

// Boot splash: logo, a row of key-shaped progress steps (`lit` of
// SPLASH_STEPS turned orange), what is happening, and the version.
constexpr int SPLASH_STEPS = 5;
void splash(int lit, const String& status);

// Scrolling list with one highlighted row (Wi-Fi network picker).
void list(const String& title, const std::vector<String>& items, int selected, const String& footer);

// Single-line text entry screen.
void input(const String& title, const String& prompt, const String& value, const String& footer);

// Number of macro rows that fit on screen and the total for a profile,
// used to clamp scrolling.
int visibleRows();
int totalRows(const Profile& p);

}  // namespace ui
