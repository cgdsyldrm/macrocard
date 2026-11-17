// Macro configuration (loaded from JSON) and macro execution.
#pragma once
#include <Arduino.h>

#include <vector>

#include "keycodes.h"
#include "typing.h"

enum class StepType : uint8_t { Combo, Text, Delay, Media };

struct Step {
    StepType type = StepType::Combo;
    kc::Combo combo;
    uint32_t value = 0;  // delay ms or consumer usage
    String text;
};

struct Macro {
    String key;    // Cardputer key id: "1", "q", "Q" (shift layer), "tab", "enter", "del", "space"
    String label;  // shown on screen
    bool repeat = false;  // fire repeatedly while held
    std::vector<Step> steps;
};

struct Profile {
    String name;
    uint32_t color = 0x2299FF;
    // "type": "keyboard" - unassigned keys type the character printed on them,
    // so the Cardputer works as a normal (wireless) keyboard.
    bool keyboard = false;
    std::vector<Macro> macros;
    const Macro* find(const String& key) const;
};

struct Config {
    String deviceName      = "MacroCard";
    // How text is typed: the keyboard layout and OS of the computer.
    const layouts::LayoutDef* layout = typing::defaultLayout();
    typing::Os os          = typing::Os::Windows;
    bool unicode           = false;  // OS Unicode input for characters not on the layout
    uint16_t typingDelayMs = 8;
    uint16_t screenOffS    = 60;  // turn the screen off after this idle time; 0 = never
    std::vector<Profile> profiles;

    typing::Settings typingSettings() const
    {
        typing::Settings s;
        s.layout  = layout;
        s.os      = os;
        s.unicode = unicode;
        s.delayMs = typingDelayMs;
        return s;
    }
};

// Returns false only for fatal errors (bad JSON, no profiles). Problems with
// individual macros are skipped and described in `warnings`.
bool parseConfig(const String& json, Config& cfg, String& error, String& warnings);

// Returns false if no computer is connected.
bool runMacro(const Macro& macro, const Config& cfg);
