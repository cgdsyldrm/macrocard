// Web configurator: joins the home Wi-Fi in the background, serves the editor
// page at http://macrocard.local and exposes a small JSON API plus firmware
// update (/update). Runs alongside USB and Bluetooth.
#pragma once
#include <Arduino.h>

#include <functional>

namespace web {

constexpr const char* HOSTNAME = "macrocard";

struct Hooks {
    // Current macropad.json text.
    std::function<String()> getConfig;
    // Small JSON object with device state for the page header.
    std::function<String()> getInfo;
    // Validate + save + apply. On success `msg` holds warnings (may be empty),
    // on failure the error.
    // `rev` is the settings revision the page loaded (empty = don't check).
    std::function<bool(const String& json, const String& rev, String& msg)> saveConfig;
    // Run a one-off macro given as a full config document with one key.
    std::function<bool(const String& json, String& msg)> runMacro;
};

enum class State { Off, Connecting, Online, Failed };

void setHooks(const Hooks& hooks);

// Access code the page must send (X-Code header) to read or change macros,
// run them or install firmware. Shown on the pad's screen.
void setCode(const String& code);

// Starts connecting in the background; returns immediately.
void start(const String& ssid, const String& password);
void stop();
void loop();

State state();
const char* stateName();
String ip();
String lastError();
String ssid();

}  // namespace web
