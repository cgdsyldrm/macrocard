// Cardputer v1.1 programmable macro pad ("MacroCard").
//
// Every key on the Cardputer can trigger a macro from macropad.json (SD card
// or internal flash). Shift gives a second layer ("Q", "!" ...). Fn + key is
// reserved for the pad itself:
//   Fn + [  /  Fn + ]    previous / next profile (also Fn + , and Fn + /)
//   Fn + ;  /  Fn + .    scroll the list up / down
//   Fn + 1..9            jump to profile N
//   Fn + w               Wi-Fi menu (on/off, choose network)
//   Fn + l               computer OS + keyboard layout (for typing text)
//   Fn + m               output mode: AUTO -> USB -> BT
//   Fn + r               reload macropad.json
//   Fn + b               screen brightness
//   Fn + h               help
//   Fn + u               reboot into download mode (to flash new firmware)
//
// Wi-Fi runs in the background next to USB and Bluetooth; its on/off state
// is remembered and it reconnects at boot.
//
// A "keyboard" profile turns the Cardputer into a normal keyboard: keys type
// what is printed on them, Ctrl/Alt/Opt(=Win) work as modifiers and
// Ctrl+Opt + ; , . / are the arrow keys and Fn + ` / del are Esc / Delete.
// All other Fn combinations are pad shortcuts in every profile.
#include <ArduinoJson.h>
#include <M5Cardputer.h>
#include <Preferences.h>
#include <WiFi.h>

#include <algorithm>

#include "default_config.generated.h"
#include "hid_output.h"
#include "macros.h"
#include "storage.h"
#include "ui.h"
#include "version.h"
#include "web.h"

#ifdef MACROPAD_DIAG
#define DLOG(...) Serial.printf(__VA_ARGS__)
#else
#define DLOG(...) do {} while (0)
#endif

namespace {

constexpr uint32_t REPEAT_DELAY_MS    = 400;
constexpr uint32_t REPEAT_INTERVAL_MS = 90;
constexpr uint32_t TYPE_REPEAT_MS     = 45;  // key repeat in keyboard profiles
constexpr uint32_t FLASH_MS           = 350;
constexpr uint32_t SPLASH_MIN_MS      = 1600;  // logo time at boot (any key skips)
constexpr uint8_t kBrightness[]       = {30, 80, 150, 255};
constexpr uint32_t DIM_BEFORE_OFF_MS  = 10000;  // dim this long before the screen goes off
constexpr uint8_t DIM_LEVEL           = 10;

// Screen power: On -> Dim (keys still work) -> Off (first key only wakes it).
enum class Screen : uint8_t { On, Dim, Off };

struct Held {
    Point2D_t pos;
    const Macro* macro;  // null for Fn shortcuts / unmapped keys
    uint32_t pressedAt;
    uint32_t lastFire;
    bool typing      = false;  // keyboard profile: repeat `action` while held
    struct {
        uint8_t key, mods;
        uint32_t cp;
    } action = {0, 0, 0};
};

Config g_cfg;
String g_configText;  // JSON text of g_cfg, served to the web page
String g_accessCode;  // 6 digits; the web page needs it (shown on screen)
uint32_t g_configRev = 0;  // bumps on every change; stops stale pages overwriting
Preferences g_prefs;
std::vector<Held> g_held;
size_t g_profile    = 0;
int g_scroll        = 0;
uint8_t g_brightIdx = 2;
bool g_helpOpen     = false;
web::State g_lastWeb = web::State::Off;

String g_flashKey;
uint32_t g_flashUntil = 0;
String g_notice;
bool g_noticeAlert     = false;
uint32_t g_noticeUntil = 0;
out::Link g_lastLink   = out::Link::None;
int g_battery          = -1;
uint32_t g_nextBattery = 0;
bool g_dirty           = true;
Screen g_screen        = Screen::On;
uint32_t g_lastActivity = 0;

const Profile& profile() { return g_cfg.profiles[g_profile]; }

// Counts as use: restores full brightness and restarts the idle timer.
void screenWake()
{
    g_lastActivity = millis();
    if (g_screen == Screen::On) return;
    if (g_screen == Screen::Off) M5Cardputer.Display.wakeup();
    ui::setBrightness(kBrightness[g_brightIdx]);
    g_screen = Screen::On;
    g_dirty  = true;
}

void updateScreen()
{
    const uint32_t offMs = g_cfg.screenOffS * 1000UL;
    if (!offMs || g_screen == Screen::Off) return;
    const uint32_t idle = millis() - g_lastActivity;
    if (idle >= offMs) {
        ui::setBrightness(0);
        M5Cardputer.Display.sleep();
        g_screen = Screen::Off;
    } else if (g_screen == Screen::On && idle + DIM_BEFORE_OFF_MS >= offMs) {
        ui::setBrightness(std::min(DIM_LEVEL, kBrightness[g_brightIdx]));
        g_screen = Screen::Dim;
    }
}

void notice(const String& text, bool alert = false, uint32_t ms = 2500)
{
    g_notice      = text;
    g_noticeAlert = alert;
    g_noticeUntil = millis() + ms;
    g_dirty       = true;
}

String footerText(bool& alert)
{
    if (g_noticeUntil && millis() < g_noticeUntil) {
        alert = g_noticeAlert;
        return g_notice;
    }
    if (out::activeLink() == out::Link::None) {
        alert = true;
        switch (out::mode()) {
            case out::Mode::Usb: return "Waiting for USB - plug in the cable";
            case out::Mode::Ble: return "Pair '" + g_cfg.deviceName + "' via Bluetooth";
            default: return "Not connected - USB or Bluetooth";
        }
    }
    alert = false;
    switch (web::state()) {
        case web::State::Online: return "code " + g_accessCode + "  " + web::HOSTNAME + ".local";
        case web::State::Connecting: return "Wi-Fi: connecting to " + web::ssid() + "...";
        case web::State::Failed:
            alert = true;
            return "Wi-Fi: " + web::lastError() + " - Fn+W menu";
        default: return "Fn+W Wi-Fi   Fn+[ ] profile   Fn+H help";
    }
}

void render()
{
    if (g_helpOpen) return;
    ui::View v;
    v.cfg       = &g_cfg;
    v.profile   = g_profile;
    v.scroll    = g_scroll;
    v.flashKey  = (millis() < g_flashUntil) ? g_flashKey : String();
    v.footer    = footerText(v.footerAlert);
    v.link      = out::linkName(out::activeLink());
    v.mode      = out::modeName(out::mode());
    v.battery   = g_battery;
    v.webOn     = web::state() == web::State::Online;
    ui::render(v);
    g_dirty = false;
}

void showHelp()
{
    g_helpOpen = true;
    ui::message("Shortcuts (Fn + key)  v" MACROCARD_VERSION,
                "[ ] profile    ; . scroll\n"
                "W Wi-Fi    L keyboard layout\n"
                "M mode   B light   R reload\n"
                "Keyboard profile: arrows are\n"
                "Ctrl+Opt + ; , . /",
                0x666666);
}

// ---------------------------------------------------------------- config ---

// Parses and activates a config. Does not save it.
bool applyConfig(const String& text, String& error, String& warnings)
{
    Config cfg;
    if (!parseConfig(text, cfg, error, warnings)) return false;
    g_cfg        = std::move(cfg);
    g_configText = text;
    ++g_configRev;
    for (auto& h : g_held) h.macro = nullptr;  // they pointed into the old config
    if (g_profile >= g_cfg.profiles.size()) g_profile = 0;
    g_scroll = 0;
    g_dirty  = true;
    return true;
}

// The first firmware wrote a Turkish example config. If it is still the
// untouched example, replace it with the current (English) default.
bool isOldTurkishDefault(const String& text)
{
    return text.indexOf("\"Medya\"") >= 0 && text.indexOf("\"Yapıştır\"") >= 0 &&
           text.indexOf("\"Seçime odaklan\"") >= 0 && text.indexOf("\"F13-F24\"") >= 0;
}

void loadConfig()
{
    String source;
    String text = storage::load(source);
    if (isOldTurkishDefault(text)) {
        String where, err;
        text = String(kDefaultConfig, kDefaultConfigLen);
        storage::save(text, where, err);
    }
    String error, warnings;
    if (!applyConfig(text, error, warnings)) {
        ui::message("macropad.json error", error + "\n\nUsing the default settings.", 0xCC3333);
        delay(4000);
        warnings = "";
        applyConfig(String(kDefaultConfig, kDefaultConfigLen), error, warnings);
        source = "defaults";
    } else if (warnings.length()) {
        ui::message("Skipped (warnings)", warnings, 0xCC8800);
        delay(4000);
    }
    notice(String(g_cfg.profiles.size()) + " profiles loaded (" + source + ")");
}

// ------------------------------------------------------------ web hooks ---

String infoJson()
{
    JsonDocument doc;
    doc["name"]    = g_cfg.deviceName;
    doc["profile"] = g_profile;
    doc["sd"]      = storage::hasSd();
    doc["link"]    = out::linkName(out::activeLink());
    doc["ip"]      = web::ip();
    doc["layout"]  = g_cfg.layout->id;
    doc["os"]      = typing::osId(g_cfg.os);
    doc["heap"]    = ESP.getFreeHeap();
    doc["minHeap"] = ESP.getMinFreeHeap();
    doc["uptime"]  = millis() / 1000;
    doc["rev"]     = g_configRev;
    doc["version"] = MACROCARD_VERSION;
    String s;
    serializeJson(doc, s);
    return s;
}

bool webSave(const String& text, const String& rev, String& msg)
{
    // A page opened before the settings changed (Fn+L, another tab) must not
    // overwrite them with its old copy.
    if (rev.length() && (uint32_t)rev.toInt() != g_configRev) {
        msg = "The settings were changed on the device since this page was loaded. Reload the page and try again.";
        return false;
    }
    String error, warnings;
    Config parsed;
    if (!parseConfig(text, parsed, error, warnings)) {
        msg = error;
        return false;
    }
    String where;
    if (!storage::save(text, where, error)) {
        msg = error;
        return false;
    }
    // Apply the already parsed config (avoids holding two copies in RAM).
    g_cfg        = std::move(parsed);
    g_configText = text;
    ++g_configRev;
    for (auto& h : g_held) h.macro = nullptr;
    if (g_profile >= g_cfg.profiles.size()) g_profile = 0;
    g_scroll = 0;
    msg      = warnings;
    screenWake();  // show what was saved (also applies a new screen timeout)
    notice("Saved from web (" + where + ")");
    return true;
}

bool webRun(const String& text, String& msg)
{
    Config tmp;
    String error, warnings;
    if (!parseConfig(text, tmp, error, warnings) || tmp.profiles.empty() || tmp.profiles[0].macros.empty()) {
        msg = error.length() ? error : (warnings.length() ? warnings : String("no macro"));
        return false;
    }
    tmp.layout        = g_cfg.layout;
    tmp.os            = g_cfg.os;
    tmp.unicode       = g_cfg.unicode;
    tmp.typingDelayMs = g_cfg.typingDelayMs;
    const Macro& m    = tmp.profiles[0].macros[0];
    screenWake();
    notice("> Test: " + m.label, false, 1500);
    render();
    if (!runMacro(m, tmp)) {
        msg = "Not connected to the computer (USB or Bluetooth)";
        return false;
    }
    return true;
}

// ---------------------------------------------------------- modal input ---

bool isModifier(uint8_t v)
{
    return v == KEY_FN || v == KEY_LEFT_SHIFT || v == KEY_LEFT_CTRL || v == KEY_LEFT_ALT || v == KEY_OPT;
}

// Treat every key that is down right now as already handled, so a key that
// closed a dialog does not also fire a macro.
void absorbHeldKeys()
{
    uint32_t now = millis();
    for (const auto& p : M5Cardputer.Keyboard.keyList()) {
        bool known = std::any_of(g_held.begin(), g_held.end(), [&](const Held& h) { return h.pos == p; });
        if (!known) g_held.push_back(Held{p, nullptr, now, now});
    }
}

struct KeyEvent {
    char first  = 0;
    char second = 0;
    bool fn     = false;
    bool shift  = false;
};

// Returns true when a new (non-modifier) key went down.
bool pollKey(KeyEvent& ev, std::vector<Point2D_t>& down)
{
    M5Cardputer.update();
    auto& kb        = M5Cardputer.Keyboard;
    const auto& now = kb.keyList();
    down.erase(std::remove_if(down.begin(), down.end(),
                              [&](const Point2D_t& p) { return std::find(now.begin(), now.end(), p) == now.end(); }),
               down.end());
    bool fn = false, shift = false;
    for (const auto& p : now) {
        uint8_t v = (uint8_t)kb.getKeyValue(p).value_first;
        if (v == KEY_FN) fn = true;
        else if (v == KEY_LEFT_SHIFT) shift = true;
    }
    for (const auto& p : now) {
        KeyValue_t kv = kb.getKeyValue(p);
        if (isModifier((uint8_t)kv.value_first)) continue;
        if (std::find(down.begin(), down.end(), p) != down.end()) continue;
        down.push_back(p);
        ev.first  = kv.value_first;
        ev.second = kv.value_second;
        ev.fn     = fn;
        ev.shift  = shift;
        return true;
    }
    return false;
}

std::vector<Point2D_t> initialDown()
{
    M5Cardputer.update();
    const auto& now = M5Cardputer.Keyboard.keyList();
    return std::vector<Point2D_t>(now.begin(), now.end());
}

bool isEsc(const KeyEvent& ev) { return ev.fn && ev.first == '`'; }

// Text entry with the Cardputer keyboard. Enter confirms, Fn+` cancels.
bool promptText(const String& title, const String& prompt, String& value)
{
    auto down = initialDown();
    const String foot = "Enter: OK   del: erase   Fn+`: cancel";
    ui::input(title, prompt, value, foot);
    while (true) {
        KeyEvent ev;
        if (!pollKey(ev, down)) {
            delay(10);
            continue;
        }
        uint8_t f = (uint8_t)ev.first;
        if (isEsc(ev)) return false;
        if (f == KEY_ENTER) return true;
        if (f == KEY_BACKSPACE) {
            if (value.length()) value.remove(value.length() - 1);
        } else if (f == KEY_TAB) {
            continue;
        } else if (ev.first >= ' ' && ev.first < 0x7f) {
            value += ev.shift ? ev.second : ev.first;
        }
        ui::input(title, prompt, value, foot);
    }
}

// Scans (with retries) and returns visible networks, strongest first.
// On failure `detail` explains what happened.
std::vector<String> scanNetworks(String& detail)
{
    bool modeOk = WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(200);
    int n = WIFI_SCAN_FAILED;
    for (int attempt = 0; attempt < 3 && n < 0; ++attempt) {
        n = WiFi.scanNetworks(false, false);
        DLOG("[diag] scan attempt %d -> %d (mode %d, heap %u)\n", attempt, n, modeOk, ESP.getFreeHeap());
        if (n < 0) delay(700);
    }
    std::vector<std::pair<int, String>> found;
    for (int i = 0; i < n; ++i) {
        String s = WiFi.SSID(i);
        if (s.isEmpty()) continue;
        auto it = std::find_if(found.begin(), found.end(), [&](const auto& e) { return e.second == s; });
        if (it == found.end()) found.push_back({WiFi.RSSI(i), s});
        else it->first = std::max(it->first, (int)WiFi.RSSI(i));
    }
    WiFi.scanDelete();
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<String> items;
    for (const auto& e : found) items.push_back(e.second);
    if (items.empty()) {
        detail = !modeOk ? String("Wi-Fi failed to start (free RAM ") + ESP.getFreeHeap() + ")"
                 : n < 0 ? String("Scan failed (code ") + n + ", free RAM " + ESP.getFreeHeap() + ")"
                         : String("No 2.4 GHz networks visible");
    }
    return items;
}

// Lets the user pick a network with ; and . (up/down). R rescans.
bool pickNetwork(String& ssid)
{
    while (true) {
        ui::message("Wi-Fi", "Scanning for networks...", 0x2299FF);
        String detail;
        std::vector<String> items = scanNetworks(detail);
        auto down = initialDown();

        if (items.empty()) {
            ui::message("No Wi-Fi found", detail + "\n\nR: scan again   Fn+`: cancel", 0xCC3333);
            while (true) {
                KeyEvent ev;
                if (!pollKey(ev, down)) {
                    delay(10);
                    continue;
                }
                if (isEsc(ev)) return false;
                if (ev.first == 'r') break;
            }
            continue;
        }

        int sel = 0;
        const String title = "Pick Wi-Fi (" + String(items.size()) + ")";
        const String foot  = "; . move   Enter: OK   R: rescan";
        ui::list(title, items, sel, foot);
        bool rescan = false;
        while (!rescan) {
            KeyEvent ev;
            if (!pollKey(ev, down)) {
                delay(10);
                continue;
            }
            if (isEsc(ev)) return false;
            if (ev.first == ';' && sel > 0) --sel;
            else if (ev.first == '.' && sel + 1 < (int)items.size()) ++sel;
            else if (ev.first == 'r') rescan = true;
            else if ((uint8_t)ev.first == KEY_ENTER) {
                ssid = items[sel];
                return true;
            }
            if (!rescan) ui::list(title, items, sel, foot);
        }
    }
}

// Asks for network + password and stores them.
bool wifiSetup()
{
    String ssid;
    if (!pickNetwork(ssid)) return false;
    String pass;
    if (!promptText(ssid, "Wi-Fi password:", pass)) return false;
    Preferences p;
    p.begin("wifi", false);
    p.putString("ssid", ssid);
    p.putString("pass", pass);
    p.end();
    return true;
}

String savedSsid()
{
    Preferences p;
    p.begin("wifi", true);
    String s = p.getString("ssid", "");
    p.end();
    return s;
}

// Starts Wi-Fi in the background with the saved network. With pickFirst (or
// no saved network) the user chooses a network and password first.
void webEnable(bool pickFirst)
{
    if (pickFirst || savedSsid().isEmpty()) {
        web::stop();  // scanning needs the radio
        bool ok = wifiSetup();
        absorbHeldKeys();
        g_dirty = true;
        if (!ok) return;
    }
    Preferences p;
    p.begin("wifi", true);
    String ssid = p.getString("ssid", "");
    String pass = p.getString("pass", "");
    p.end();
    if (ESP.getFreeHeap() < 50000) {
        notice(String("Not enough memory for Wi-Fi (") + ESP.getFreeHeap() / 1024 + " KB)", true, 5000);
        return;
    }
    g_prefs.putBool("web", true);
    web::start(ssid, pass);
}

void webDisable()
{
    web::stop();
    g_prefs.putBool("web", false);
}

// Fn+W: Wi-Fi menu. Shows the current network and lets the user turn Wi-Fi
// on/off or pick another network.
void wifiMenu()
{
    auto down = initialDown();
    int sel             = 0;
    String lastScreen;
    uint32_t nextRedraw = 0;
    while (true) {
        web::loop();  // keep connecting / serving while the menu is open
        web::State st = web::state();
        bool on       = st == web::State::Connecting || st == web::State::Online;
        String saved  = savedSsid();

        std::vector<String> items;
        items.push_back(on ? "Turn Wi-Fi off" : "Turn Wi-Fi on");
        items.push_back(saved.isEmpty() ? "Choose network" : "Change network");
        items.push_back("Access code: " + g_accessCode);  // for the web page
        items.push_back("Close");

        String title = "Wi-Fi: ";
        String foot;
        switch (st) {
            case web::State::Online:
                title += web::ssid();
                foot = String(web::HOSTNAME) + ".local  " + web::ip();
                break;
            case web::State::Connecting:
                title += "connecting";
                foot = "Connecting to " + web::ssid() + "...";
                break;
            case web::State::Failed:
                title += "failed";
                foot = web::ssid() + ": " + web::lastError();
                break;
            default:
                title += "off";
                foot = saved.isEmpty() ? String("No network saved") : "Saved network: " + saved;
                break;
        }
        String screen = title + "|" + foot + "|" + items[0] + "|" + sel;
        if (screen != lastScreen || millis() >= nextRedraw) {
            ui::list(title, items, sel, foot);
            lastScreen = screen;
            nextRedraw = millis() + 1000;
        }

        KeyEvent ev;
        if (!pollKey(ev, down)) {
            delay(10);
            continue;
        }
        if (isEsc(ev) || (ev.fn && ev.first == 'w')) break;
        if (ev.first == ';' && sel > 0) --sel;
        else if (ev.first == '.' && sel + 1 < (int)items.size()) ++sel;
        else if ((uint8_t)ev.first == KEY_ENTER) {
            if (sel == 0) {
                if (on) webDisable();
                else webEnable(false);
            } else if (sel == 1) {
                webEnable(true);
            } else if (sel == 2) {
                continue;  // the access code row is information only
            } else {
                break;
            }
            down       = initialDown();
            lastScreen = "";
        }
    }
    absorbHeldKeys();
    g_dirty = true;
}

// Stores layout / OS in the saved config (used by the Fn+L menu).
bool saveTypingSettings(const layouts::LayoutDef* layout, typing::Os os)
{
    JsonDocument doc;
    if (deserializeJson(doc, g_configText)) return false;
    doc["layout"] = layout->id;
    doc["os"]     = typing::osId(os);
    String text;
    serializeJson(doc, text);
    doc.clear();
    String where, error, warnings;
    if (!storage::save(text, where, error)) {
        notice(error, true, 4000);
        return false;
    }
    return applyConfig(text, error, warnings);
}

// Fn+L: computer OS and keyboard layout, for typing text and the keyboard
// profile. Handy when moving the pad between computers.
void layoutMenu()
{
    auto down = initialDown();
    int sel   = 0;
    for (size_t i = 0; i < layouts::kLayoutCount; ++i) {
        if (layouts::kLayouts[i] == g_cfg.layout) sel = i + 1;
    }
    bool redraw = true;
    while (true) {
        if (redraw) {
            std::vector<String> items;
            items.push_back(String("Computer: ") + typing::osName(g_cfg.os) + "  (Enter = change)");
            for (size_t i = 0; i < layouts::kLayoutCount; ++i) {
                const layouts::LayoutDef* l = layouts::kLayouts[i];
                items.push_back(String(l == g_cfg.layout ? "* " : "  ") + l->name);
            }
            ui::list("Keyboard layout of the computer", items, sel, "; . move   Enter: select   Fn+`: close");
            redraw = false;
        }
        KeyEvent ev;
        if (!pollKey(ev, down)) {
            delay(10);
            continue;
        }
        if (isEsc(ev) || (ev.fn && ev.first == 'l')) break;
        if (ev.first == ';' && sel > 0) {
            --sel;
            redraw = true;
        } else if (ev.first == '.' && sel < (int)layouts::kLayoutCount) {
            ++sel;
            redraw = true;
        } else if ((uint8_t)ev.first == KEY_ENTER) {
            if (sel == 0) {
                typing::Os next = g_cfg.os == typing::Os::Windows ? typing::Os::Mac
                                  : g_cfg.os == typing::Os::Mac   ? typing::Os::Linux
                                                                  : typing::Os::Windows;
                saveTypingSettings(g_cfg.layout, next);
                redraw = true;
            } else {
                saveTypingSettings(layouts::kLayouts[sel - 1], g_cfg.os);
                notice(String("Layout: ") + g_cfg.layout->name + " / " + typing::osName(g_cfg.os));
                break;
            }
        }
    }
    absorbHeldKeys();
    g_dirty = true;
}

// ------------------------------------------------------------------ keys ---

String keyId(const KeyValue_t& kv, bool shift)
{
    uint8_t first = (uint8_t)kv.value_first;
    if (first == KEY_BACKSPACE) return "del";
    if (first == KEY_TAB) return "tab";
    if (first == KEY_ENTER) return "enter";
    if (first == ' ') return "space";
    return String(shift ? kv.value_second : kv.value_first);
}

void selectProfile(size_t idx)
{
    if (idx >= g_cfg.profiles.size()) return;
    out::releaseAll();
    g_profile = idx;
    g_scroll  = 0;
    g_prefs.putUInt("profile", g_profile);
    notice(profile().name);
}

void systemKey(char c)
{
    const size_t n = g_cfg.profiles.size();
    switch (c) {
        case ',':
        case '[': selectProfile((g_profile + n - 1) % n); break;
        case '/':
        case ']': selectProfile((g_profile + 1) % n); break;
        case ';':
            if (g_scroll > 0) --g_scroll;
            break;
        case '.':
            if (g_scroll + ui::visibleRows() < ui::totalRows(profile())) ++g_scroll;
            break;
        case 'w': wifiMenu(); break;
        case 'l': layoutMenu(); break;
        case 'm': {
            out::Mode m = out::nextMode(out::mode());
            out::setMode(m);
            g_prefs.putUChar("mode", (uint8_t)m);
            notice(String("Output: ") + out::modeName(m));
            break;
        }
        case 'r':
            ui::message("Loading", "Reading macropad.json...", 0x2299FF);
            loadConfig();
            break;
        case 'b':
            g_brightIdx = (g_brightIdx + 1) % sizeof(kBrightness);
            ui::setBrightness(kBrightness[g_brightIdx]);
            g_prefs.putUChar("bright", g_brightIdx);
            break;
        case 'h': showHelp(); return;
        case 'u':
            ui::message("Update mode", "Ready for new firmware.\nPower-cycle to cancel.", 0x666666);
            delay(300);
            out::rebootToBootloader();
            return;
        default:
            if (c >= '1' && c <= '9') selectProfile(c - '1');
            break;
    }
    g_dirty = true;
}

void fire(const Macro& m)
{
    g_flashKey   = m.key;
    g_flashUntil = millis() + FLASH_MS;
    notice("> " + m.label, false, 1500);
    render();  // show feedback before a possibly long macro runs
    if (!runMacro(m, g_cfg)) notice("Not connected - macro not sent", true);
    g_flashUntil = millis() + FLASH_MS;
    g_dirty      = true;
}

// ---- keyboard profile -------------------------------------------------

struct Mods {
    bool fn = false, shift = false, ctrl = false, alt = false, opt = false;
};

// What a key does in a keyboard profile: a key press with modifiers, or a
// character to type on the computer's layout.
struct KeyAction {
    uint8_t key  = 0;
    uint8_t mods = 0;
    uint32_t cp  = 0;  // non-zero: type this character
};

void sendAction(const KeyAction& a)
{
    bool ok;
    if (a.cp) {
        ok = typing::typeChar(a.cp, g_cfg.typingSettings());
    } else {
        uint8_t keys[6] = {a.key, 0, 0, 0, 0, 0};
        ok = out::tap(a.mods, keys, out::activeLink() == out::Link::Ble ? 12 : 4);
    }
    if (!ok && out::activeLink() == out::Link::None) notice("Not connected - key not sent", true, 1500);
}

// Returns false if the key does nothing in a keyboard profile.
bool keyboardAction(const KeyValue_t& kv, const Mods& m, KeyAction& a)
{
    const uint8_t first = (uint8_t)kv.value_first;
    // Opt is the Windows key on Windows/Linux and Cmd on a Mac.
    uint8_t mods = (m.ctrl ? kc::MOD_LCTRL : 0) | (m.alt ? kc::MOD_LALT : 0) | (m.opt ? kc::MOD_LGUI : 0);
    if (m.shift) mods |= kc::MOD_LSHIFT;
    a = KeyAction{};

    // Ctrl + Opt + the keys with arrows printed on them = arrow keys
    // (Fn + those keys switches profiles everywhere, so it can't be used here).
    if (m.ctrl && m.opt) {
        switch (kv.value_first) {
            case ';': a.key = 0x52; break;  // up
            case '.': a.key = 0x51; break;  // down
            case ',': a.key = 0x50; break;  // left
            case '/': a.key = 0x4f; break;  // right
            default: break;
        }
        if (a.key) {
            a.mods = (m.shift ? kc::MOD_LSHIFT : 0) | (m.alt ? kc::MOD_LALT : 0);
            return true;
        }
    }
    if (m.fn) {
        // Esc / Delete printed on the Cardputer keycaps.
        if (kv.value_first == '`') a.key = 0x29;           // esc
        else if (first == KEY_BACKSPACE) a.key = 0x4c;     // delete
        else return false;
        a.mods = mods;
        return true;
    }
    switch (first) {
        case KEY_BACKSPACE: a.key = 0x2a; break;
        case KEY_TAB: a.key = 0x2b; break;
        case KEY_ENTER: a.key = 0x28; break;
        case ' ': a.key = 0x2c; break;
        default: break;
    }
    if (a.key) {
        a.mods = mods;
        return true;
    }
    if (m.ctrl || m.alt || m.opt) {
        // Shortcut (Ctrl+C ...): shortcuts follow the key position.
        a.key = typing::usKeyFor(kv.value_first);
        a.mods = mods;
        return a.key != 0;
    }
    // Plain typing: the character printed on the key.
    a.cp = (uint8_t)(m.shift ? kv.value_second : kv.value_first);
    return true;
}

bool isArrowKey(char c) { return c == ';' || c == '.' || c == ',' || c == '/'; }

void handleKeys()
{
    auto& kb        = M5Cardputer.Keyboard;
    const auto& now_pressed = kb.keyList();

    Mods mods;
    for (const auto& p : now_pressed) {
        uint8_t v = (uint8_t)kb.getKeyValue(p).value_first;
        if (v == KEY_FN) mods.fn = true;
        else if (v == KEY_LEFT_SHIFT) mods.shift = true;
        else if (v == KEY_LEFT_CTRL) mods.ctrl = true;
        else if (v == KEY_LEFT_ALT) mods.alt = true;
        else if (v == KEY_OPT) mods.opt = true;
    }

    // Forget released keys.
    g_held.erase(std::remove_if(g_held.begin(), g_held.end(),
                                [&](const Held& h) {
                                    return std::find(now_pressed.begin(), now_pressed.end(), h.pos) ==
                                           now_pressed.end();
                                }),
                 g_held.end());

    for (const auto& p : now_pressed) {
        KeyValue_t kv = kb.getKeyValue(p);
        if (isModifier((uint8_t)kv.value_first)) continue;

        auto it = std::find_if(g_held.begin(), g_held.end(), [&](const Held& h) { return h.pos == p; });
        uint32_t now = millis();

        if (it == g_held.end()) {
            Held h{p, nullptr, now, now};
            const bool kbProfile = profile().keyboard;
            // In the Keyboard profile only Fn+` (Esc) and Fn+del (Delete) are
            // typed; every other Fn combination is a pad shortcut, as everywhere.
            // Ctrl+Opt+arrow keys stay arrows even if Fn is also held.
            const bool kbFnKey = kbProfile && mods.fn &&
                                 (kv.value_first == '`' || (uint8_t)kv.value_first == KEY_BACKSPACE ||
                                  (mods.ctrl && mods.opt && isArrowKey(kv.value_first)));
            if (g_helpOpen) {
                g_helpOpen = false;
                g_dirty    = true;
            } else if (mods.fn && !kbFnKey) {
                g_held.push_back(h);
                systemKey(kv.value_first);
                g_lastActivity = millis();  // a menu may have been open for a while
                return;  // dialogs may have changed the key state; rescan next loop
            } else {
                String id      = keyId(kv, mods.shift);
                const Macro* m = mods.fn ? nullptr : profile().find(id);
                if (m) {
                    h.macro = m;
                    fire(*m);
                } else if (kbProfile) {
                    KeyAction a;
                    if (keyboardAction(kv, mods, a)) {
                        h.typing = true;
                        h.action = {a.key, a.mods, a.cp};
                        sendAction(a);
                    }
                } else {
                    notice("[" + id + "] not assigned", false, 1200);
                }
            }
            g_held.push_back(h);
        } else if (it->typing) {
            if (now - it->pressedAt >= REPEAT_DELAY_MS && now - it->lastFire >= TYPE_REPEAT_MS) {
                it->lastFire = now;
                KeyAction a;
                a.key  = it->action.key;
                a.mods = it->action.mods;
                a.cp   = it->action.cp;
                sendAction(a);
            }
        } else if (it->macro && it->macro->repeat) {
            if (now - it->pressedAt >= REPEAT_DELAY_MS && now - it->lastFire >= REPEAT_INTERVAL_MS) {
                const Macro* m = it->macro;
                it->lastFire   = now;
                fire(*m);
            }
        }
    }
}

// --------------------------------------------------------------- status ---

void updateStatus()
{
    uint32_t now = millis();
    out::Link link = out::activeLink();
    if (link != g_lastLink) {
        g_lastLink = link;
        g_dirty    = true;
    }
    if (now >= g_nextBattery) {
        g_nextBattery = now + 10000;
        int level     = M5Cardputer.Power.getBatteryLevel();
        int next      = (level >= 0 && level <= 100) ? level : -1;
        if (next != g_battery) {
            g_battery = next;
            if (next >= 0) out::setBattery(next);
            g_dirty = true;
        }
    }
    if (g_flashUntil && now >= g_flashUntil) {
        g_flashUntil = 0;
        g_dirty      = true;
    }
    if (g_noticeUntil && now >= g_noticeUntil) {
        g_noticeUntil = 0;
        g_dirty       = true;
    }
    web::State ws = web::state();
    if (ws != g_lastWeb) {
        g_lastWeb = ws;
        g_dirty   = true;
        DLOG("[diag] wifi %s ip=%s heap=%u\n", web::stateName(), web::ip().c_str(), ESP.getFreeHeap());
        if (ws == web::State::Online) notice(String("Web: http://") + web::HOSTNAME + ".local", false, 5000);
    }
}

}  // namespace

void setup()
{
#ifdef MACROPAD_DIAG
    Serial.begin(115200);
    delay(1500);
    DLOG("\n[diag] boot, reset reason=%d heap=%u\n", (int)esp_reset_reason(), ESP.getFreeHeap());
#endif
    auto cfg = M5.config();
    // Speaker and microphone are unused; not starting them saves RAM
    // (their tasks and DMA buffers) for Bluetooth + Wi-Fi.
    cfg.internal_spk = false;
    cfg.internal_mic = false;
    M5Cardputer.begin(cfg, true);
    ui::begin();
    DLOG("[diag] ui ok heap=%u rot=%d %dx%d\n", ESP.getFreeHeap(), M5Cardputer.Display.getRotation(),
         M5Cardputer.Display.width(), M5Cardputer.Display.height());

    g_prefs.begin("macropad", false);
    g_brightIdx = std::min<uint8_t>(g_prefs.getUChar("bright", 2), sizeof(kBrightness) - 1);
    ui::setBrightness(kBrightness[g_brightIdx]);
    out::setMode((out::Mode)std::min<uint8_t>(g_prefs.getUChar("mode", 0), 2));
    g_profile = g_prefs.getUInt("profile", 0);

    // Boot splash: one orange key per step.
    const uint32_t splashStart = millis();
    ui::splash(1, "Loading profiles...");
    storage::begin();
    loadConfig();
    DLOG("[diag] config ok, %u profiles heap=%u\n", (unsigned)g_cfg.profiles.size(), ESP.getFreeHeap());

    // The device name comes from the config, so HID starts afterwards.
    ui::splash(2, "Starting USB and Bluetooth...");
    out::begin(g_cfg.deviceName.c_str());
    DLOG("[diag] hid begin ok heap=%u\n", ESP.getFreeHeap());
    ui::splash(3, "Preparing web setup...");

    web::Hooks hooks;
    hooks.getConfig  = [] { return g_configText; };
    hooks.getInfo    = infoJson;
    hooks.saveConfig = webSave;
    hooks.runMacro   = webRun;
    web::setHooks(hooks);
    // Access code for the web page: random, created once, kept across updates.
    g_accessCode = g_prefs.getString("code", "");
    if (g_accessCode.length() != 6) {
        char buf[7];
        snprintf(buf, sizeof(buf), "%06lu", (unsigned long)(esp_random() % 1000000UL));
        g_accessCode = buf;
        g_prefs.putString("code", g_accessCode);
    }
    web::setCode(g_accessCode);

    // Reconnect Wi-Fi if it was on last time and a network is saved.
    bool webWanted = g_prefs.getBool("web", false);
#ifdef MACROPAD_DIAG
    webWanted = true;
#endif
    if (webWanted) {
        Preferences p;
        p.begin("wifi", true);
        bool haveNet = p.getString("ssid", "").length() > 0;
        p.end();
        if (haveNet) {
            ui::splash(4, "Connecting to Wi-Fi...");
            webEnable(false);
        }
    }
    ui::splash(ui::SPLASH_STEPS, "Ready");

    // Keep the logo up for a moment, but any key press skips straight on
    // (and does not also run a macro).
    auto down = initialDown();  // keys already held at power-on don't count
    while ((int32_t)(millis() - (splashStart + SPLASH_MIN_MS)) < 0) {
        KeyEvent ev;
        if (pollKey(ev, down)) break;
        delay(10);
    }
    absorbHeldKeys();
    render();
    g_lastActivity = millis();
}

void loop()
{
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.keyList().empty()) {
        // Held keys keep the screen on, so while it is off every key is a new
        // press: that one only wakes the screen and does not run a macro.
        const bool wasOff = g_screen == Screen::Off;
        screenWake();
        if (wasOff) absorbHeldKeys();
    }
    handleKeys();
    web::loop();
    updateStatus();
    updateScreen();
    if (g_dirty && g_screen != Screen::Off) render();
#ifdef MACROPAD_DIAG
    static uint32_t nextLog = 0;
    if (millis() >= nextLog) {
        nextLog = millis() + 5000;
        DLOG("[diag] t=%lus heap=%u min=%u\n", millis() / 1000, ESP.getFreeHeap(), ESP.getMinFreeHeap());
    }
#endif
    delay(web::state() == web::State::Online ? 2 : 5);
}
