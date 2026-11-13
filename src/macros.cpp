#include "macros.h"

#include <ArduinoJson.h>

#include "hid_output.h"

const Macro* Profile::find(const String& key) const
{
    for (const auto& m : macros) {
        if (m.key == key) return &m;
    }
    return nullptr;
}

namespace {

constexpr uint32_t MAX_DELAY_MS = 10000;

bool validKeyId(const String& k)
{
    if (k == "tab" || k == "enter" || k == "del" || k == "space") return true;
    return k.length() == 1 && k[0] > ' ' && k[0] < 0x7f;
}

uint32_t parseColor(const char* s, uint32_t fallback)
{
    if (!s || s[0] != '#' || strlen(s) != 7) return fallback;
    char* end  = nullptr;
    uint32_t v = strtoul(s + 1, &end, 16);
    return (end && *end == '\0') ? v : fallback;
}

bool parseStep(JsonVariantConst v, Step& step, String& err)
{
    if (v.is<const char*>()) {
        step.type = StepType::Combo;
        return kc::parseCombo(v.as<String>(), step.combo, err);
    }
    if (!v.is<JsonObjectConst>()) {
        err = "a step must be a string or an object";
        return false;
    }
    JsonObjectConst o = v.as<JsonObjectConst>();
    if (o["text"].is<const char*>()) {
        step.type = StepType::Text;
        step.text = o["text"].as<String>();
        return true;
    }
    if (o["delay"].is<uint32_t>()) {
        step.type  = StepType::Delay;
        step.value = min<uint32_t>(o["delay"].as<uint32_t>(), MAX_DELAY_MS);
        return true;
    }
    if (o["media"].is<const char*>()) {
        uint16_t usage;
        if (!kc::parseConsumer(o["media"].as<String>(), usage)) {
            err = "unknown media key '" + o["media"].as<String>() + "'";
            return false;
        }
        step.type  = StepType::Media;
        step.value = usage;
        return true;
    }
    if (o["keys"].is<const char*>()) {
        step.type = StepType::Combo;
        return kc::parseCombo(o["keys"].as<String>(), step.combo, err);
    }
    err = "step has no text/delay/media/keys";
    return false;
}

bool parseSteps(JsonVariantConst v, std::vector<Step>& steps, String& err)
{
    if (v.is<JsonArrayConst>()) {
        for (JsonVariantConst item : v.as<JsonArrayConst>()) {
            Step s;
            if (!parseStep(item, s, err)) return false;
            steps.push_back(std::move(s));
        }
    } else {
        Step s;
        if (!parseStep(v, s, err)) return false;
        steps.push_back(std::move(s));
    }
    if (steps.empty()) {
        err = "empty macro";
        return false;
    }
    return true;
}

void addWarning(String& warnings, const String& w)
{
    if (warnings.length() < 400) {
        if (warnings.length()) warnings += "\n";
        warnings += w;
    }
}

}  // namespace

bool parseConfig(const String& json, Config& cfg, String& error, String& warnings)
{
    JsonDocument doc;
    DeserializationError de = deserializeJson(doc, json);
    if (de) {
        error = String("JSON error: ") + de.c_str();
        return false;
    }

    Config parsed;
    if (doc["device_name"].is<const char*>()) parsed.deviceName = doc["device_name"].as<String>();
    if (doc["layout"].is<const char*>()) {
        const layouts::LayoutDef* l = typing::findLayout(doc["layout"].as<String>());
        if (l) parsed.layout = l;
        else addWarning(warnings, "unknown layout '" + doc["layout"].as<String>() + "', using English (US)");
    }
    if (doc["os"].is<const char*>() && !typing::parseOs(doc["os"].as<String>(), parsed.os)) {
        addWarning(warnings, "os must be windows, mac or linux");
    }
    parsed.unicode = doc["unicode"] | false;
    if (doc["typing_delay_ms"].is<uint16_t>()) {
        parsed.typingDelayMs = min<uint16_t>(doc["typing_delay_ms"].as<uint16_t>(), 200);
    }

    for (JsonObjectConst p : doc["profiles"].as<JsonArrayConst>()) {
        Profile profile;
        profile.name  = p["name"] | "Profile";
        profile.color = parseColor(p["color"] | "", profile.color);
        profile.keyboard = strcmp(p["type"] | "", "keyboard") == 0;
        if (profile.keyboard) {
            // Fixed: every key types its own character; no macros here.
            parsed.profiles.push_back(std::move(profile));
            continue;
        }
        for (JsonPairConst kv : p["keys"].as<JsonObjectConst>()) {
            Macro m;
            m.key = kv.key().c_str();
            if (!validKeyId(m.key)) {
                addWarning(warnings, profile.name + ": invalid key '" + m.key + "'");
                continue;
            }
            JsonVariantConst body = kv.value();
            JsonVariantConst action;
            if (body.is<JsonObjectConst>() && !body["do"].isNull()) {
                m.label  = body["label"] | "";
                m.repeat = body["repeat"] | false;
                action   = body["do"];
            } else {
                action = body;  // shorthand: "1": "ctrl+c"
            }
            String err;
            if (!parseSteps(action, m.steps, err)) {
                addWarning(warnings, profile.name + " [" + m.key + "]: " + err);
                continue;
            }
            if (m.label.isEmpty()) {
                m.label = action.is<const char*>() ? action.as<String>() : String("macro");
            }
            profile.macros.push_back(std::move(m));
        }
        parsed.profiles.push_back(std::move(profile));
    }

    if (parsed.profiles.empty()) {
        error = "'profiles' list is empty";
        return false;
    }
    // The Keyboard profile is built in: put it back if it is missing.
    bool hasKeyboard = false;
    for (const auto& p : parsed.profiles) hasKeyboard |= p.keyboard;
    if (!hasKeyboard) {
        Profile kb;
        kb.name     = "Keyboard";
        kb.color    = 0x8899AA;
        kb.keyboard = true;
        parsed.profiles.insert(parsed.profiles.begin() + std::min<size_t>(1, parsed.profiles.size()), std::move(kb));
    }
    cfg = std::move(parsed);
    return true;
}

bool runMacro(const Macro& macro, const Config& cfg)
{
    if (out::activeLink() == out::Link::None) return false;
    // Bluetooth needs a little more time between reports than USB.
    uint16_t d = cfg.typingDelayMs;
    if (out::activeLink() == out::Link::Ble && d < 12) d = 12;

    for (const auto& s : macro.steps) {
        switch (s.type) {
            case StepType::Combo:
                if (!out::tap(s.combo.mod, s.combo.keys, d)) return false;
                break;
            case StepType::Text:
                typing::typeText(s.text, cfg.typingSettings());
                break;
            case StepType::Delay:
                delay(s.value);
                break;
            case StepType::Media:
                if (!out::consumer((uint16_t)s.value)) return false;
                delay(d);
                break;
        }
    }
    return true;
}
