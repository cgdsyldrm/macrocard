#include "ui.h"

#include <M5Cardputer.h>

#include "version.h"

namespace ui {
namespace {

constexpr int W        = 240;
constexpr int H        = 135;
constexpr int HEADER_H = 24;
constexpr int FOOTER_H = 18;
constexpr int ROW_H    = 22;  // 4 rows x 2 columns visible
constexpr int COLS     = 2;
constexpr int COL_W    = W / COLS;
constexpr int CHIP_W   = 30;
constexpr int BODY_Y   = HEADER_H + 2;
constexpr int BODY_H   = H - HEADER_H - FOOTER_H - 3;

// The main screen is drawn in horizontal strips through a small sprite
// (flicker-free, ~11 KB instead of a 32 KB full-screen buffer). RAM is tight
// with USB, Bluetooth and Wi-Fi all running.
constexpr int STRIP_H = 45;
M5Canvas g_strip;  // no parent: the display is passed at push time
bool g_hasStrip = false;

const lgfx::IFont* kLabelFont = &fonts::efontCN_16;    // macro labels, list rows
const lgfx::IFont* kTitleFont = &fonts::efontCN_16_b;  // headers
const lgfx::IFont* kBodyFont  = &fonts::efontCN_14;    // dialog text
const lgfx::IFont* kSmallFont = &fonts::efontCN_12;    // footer, status
const lgfx::IFont* kChipFont  = &fonts::Font2;         // key caps (ASCII)

uint16_t rgb(uint32_t c)
{
    return lgfx::color565((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff);
}

bool isLight(uint32_t c)
{
    uint32_t r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
    return (r * 299 + g * 587 + b * 114) / 1000 > 140;
}

// Trims a UTF-8 string with ".." so it fits in maxW pixels.
String fit(LovyanGFX& gfx, String s, int maxW)
{
    if (gfx.textWidth(s) <= maxW) return s;
    while (s.length() > 0) {
        int i = s.length() - 1;
        while (i > 0 && ((uint8_t)s[i] & 0xC0) == 0x80) --i;
        s.remove(i);
        if (gfx.textWidth(s + "..") <= maxW) return s + "..";
    }
    return s;
}

String chipText(const String& key)
{
    if (key == "tab") return "TAB";
    if (key == "enter") return "ENT";
    if (key == "del") return "DEL";
    if (key == "space") return "SPC";
    return key;
}

// oy = vertical offset of the surface being drawn (strip rendering).
void header(LovyanGFX& g, int oy, const String& title, uint32_t color, const String& right = String())
{
    g.fillRect(0, oy, W, HEADER_H, rgb(color));
    g.setTextColor(isLight(color) ? TFT_BLACK : TFT_WHITE);
    int rightW = 0;
    if (right.length()) {
        g.setFont(kSmallFont);
        g.setTextDatum(middle_right);
        g.drawString(right, W - 5, oy + HEADER_H / 2 + 1);
        rightW = g.textWidth(right) + 8;
    }
    g.setFont(kTitleFont);
    g.setTextDatum(middle_left);
    g.drawString(fit(g, title, W - rightW - 10), 5, oy + HEADER_H / 2 + 1);
}

void footer(LovyanGFX& g, int oy, const String& text, bool alert)
{
    int fy = oy + H - FOOTER_H;
    g.drawFastHLine(0, fy - 1, W, 0x31A6);
    g.setFont(kSmallFont);
    g.setTextDatum(middle_left);
    g.setTextColor(alert ? TFT_ORANGE : TFT_LIGHTGREY);
    g.drawString(fit(g, text, W - 8), 4, fy + FOOTER_H / 2);
}

void paintMain(LovyanGFX& g, int oy, const View& v)
{
    const Profile& p         = v.cfg->profiles[v.profile];
    const uint32_t accent    = p.color;
    const uint16_t accent565 = rgb(accent);
    const uint16_t onAccent  = isLight(accent) ? TFT_BLACK : TFT_WHITE;

    String right = v.webOn ? String("WiFi ") : String();
    right += v.link;
    if (v.mode[0] != 'A') right = String(v.mode) + ":" + right;  // forced output mode
    if (v.battery >= 0) right += " " + String(v.battery) + "%";
    header(g, oy, String(v.profile + 1) + "/" + String(v.cfg->profiles.size()) + " " + p.name, accent, right);

    const int rows  = visibleRows();
    const int first = v.scroll * COLS;
    for (int slot = 0; slot < rows * COLS; ++slot) {
        size_t idx = first + slot;
        if (idx >= p.macros.size()) break;
        const Macro& m = p.macros[idx];
        int x    = (slot % COLS) * COL_W + 2;
        int y    = oy + BODY_Y + (slot / COLS) * ROW_H;
        int h    = ROW_H - 3;
        bool hot = (m.key == v.flashKey);

        if (hot) g.fillRoundRect(x, y, COL_W - 4, h, 4, accent565);
        else g.drawRoundRect(x, y, COL_W - 4, h, 4, 0x2945);
        g.fillRoundRect(x + 2, y + 2, CHIP_W, h - 4, 3, hot ? onAccent : accent565);
        g.setFont(kChipFont);
        g.setTextDatum(middle_center);
        g.setTextColor(hot ? accent565 : onAccent);
        g.drawString(chipText(m.key), x + 2 + CHIP_W / 2, y + h / 2);

        g.setFont(kLabelFont);
        g.setTextDatum(middle_left);
        g.setTextColor(hot ? onAccent : TFT_WHITE);
        g.drawString(fit(g, m.label, COL_W - CHIP_W - 14), x + CHIP_W + 6, y + h / 2 + 1);
    }
    if (p.keyboard && p.macros.empty()) {
        g.setFont(kBodyFont);
        g.setTextDatum(top_left);
        g.setTextColor(TFT_WHITE);
        const char* lines[] = {"Keyboard mode - just type.", "Ctrl / Alt / Opt(Win) work.",
                               "Ctrl+Opt + ; , . /  = arrows", "Fn + `  = Esc   Fn + del = Delete"};
        for (int i = 0; i < 4; ++i) g.drawString(lines[i], 6, oy + BODY_Y + 2 + i * 22);
    } else if (p.macros.empty()) {
        g.setFont(kLabelFont);
        g.setTextDatum(middle_center);
        g.setTextColor(TFT_DARKGREY);
        g.drawString("No macros in this profile", W / 2, oy + BODY_Y + BODY_H / 2);
    }

    int total = totalRows(p);
    if (v.scroll > 0) {
        int yt = oy + BODY_Y;
        g.fillTriangle(W / 2, yt - 1, W / 2 - 6, yt + 4, W / 2 + 6, yt + 4, TFT_LIGHTGREY);
    }
    if (v.scroll + rows < total) {
        int yb = oy + BODY_Y + rows * ROW_H;
        g.fillTriangle(W / 2, yb, W / 2 - 6, yb - 5, W / 2 + 6, yb - 5, TFT_LIGHTGREY);
    }

    footer(g, oy, v.footer, v.footerAlert);
}

}  // namespace

void begin()
{
    // M5GFX >= 0.3 already maps rotation 0 to the Cardputer's landscape
    // orientation (older versions needed setRotation(1)). Only rotate if the
    // library hands us a portrait screen.
    auto& d = M5Cardputer.Display;
    for (int i = 0; i < 4 && d.width() < d.height(); ++i) d.setRotation((d.getRotation() + 1) & 3);
    g_strip.setColorDepth(8);
    g_hasStrip = g_strip.createSprite(W, STRIP_H) != nullptr;
}

void setBrightness(uint8_t level)
{
    M5Cardputer.Display.setBrightness(level);
}

int visibleRows()
{
    return BODY_H / ROW_H;
}

int totalRows(const Profile& p)
{
    return (p.macros.size() + COLS - 1) / COLS;
}

void render(const View& v)
{
    if (!g_hasStrip) {
        M5Cardputer.Display.fillScreen(TFT_BLACK);
        paintMain(M5Cardputer.Display, 0, v);
        return;
    }
    for (int top = 0; top < H; top += STRIP_H) {
        g_strip.fillScreen(TFT_BLACK);
        paintMain(g_strip, -top, v);  // the sprite clips everything outside the strip
        g_strip.pushSprite(&M5Cardputer.Display, 0, top);
    }
}

void splash(int lit, const String& status)
{
    auto& g = M5Cardputer.Display;
    const uint16_t bg     = lgfx::color565(0x14, 0x15, 0x17);  // the web page's near-black
    const uint16_t orange = lgfx::color565(0xF2, 0x6B, 0x21);
    const uint16_t keyOff = lgfx::color565(0x3A, 0x3B, 0x3E);
    const uint16_t grey   = lgfx::color565(0x8B, 0x8D, 0x90);

    g.startWrite();
    g.fillScreen(bg);

    // "Macro" white + "Card" orange, like the web page logo.
    g.setFont(&fonts::FreeSansBold18pt7b);
    g.setTextDatum(middle_left);
    const int wMacro = g.textWidth("Macro");
    const int wCard  = g.textWidth("Card");
    int x            = (W - wMacro - wCard) / 2;
    g.setTextColor(TFT_WHITE, bg);
    g.drawString("Macro", x, 48);
    g.setTextColor(orange, bg);
    g.drawString("Card", x + wMacro, 48);

    // Progress as Cardputer keys: each boot step "presses" one orange.
    const int pw = 30, ph = 12, gap = 8;
    const int total = SPLASH_STEPS * pw + (SPLASH_STEPS - 1) * gap;
    int px          = (W - total) / 2;
    for (int i = 0; i < SPLASH_STEPS; ++i) {
        bool on = i < lit;
        g.fillRoundRect(px + i * (pw + gap), 82 + (on ? 1 : 0), pw, ph, ph / 2, on ? orange : keyOff);
    }

    g.setFont(kSmallFont);
    g.setTextColor(grey, bg);
    g.setTextDatum(bottom_left);
    g.drawString(status, 6, H - 4);
    g.setTextDatum(bottom_right);
    g.drawString("v" MACROCARD_VERSION, W - 6, H - 4);
    g.endWrite();
}

// Dialog screens draw straight to the display (they change rarely).

void message(const String& title, const String& body, uint32_t color)
{
    auto& g = M5Cardputer.Display;
    g.startWrite();
    g.fillScreen(TFT_BLACK);
    header(g, 0, title, color);
    g.setFont(kBodyFont);
    g.setTextDatum(top_left);
    g.setTextColor(TFT_WHITE);
    int y     = HEADER_H + 5;
    int start = 0;
    while (start < (int)body.length() && y < H - 12) {
        int nl      = body.indexOf('\n', start);
        String line = body.substring(start, nl < 0 ? body.length() : nl);
        g.drawString(fit(g, line, W - 10), 5, y);
        y += line.length() ? 17 : 8;  // blank lines are half height
        if (nl < 0) break;
        start = nl + 1;
    }
    g.endWrite();
}

void list(const String& title, const std::vector<String>& items, int selected, const String& foot)
{
    auto& g = M5Cardputer.Display;
    g.startWrite();
    g.fillScreen(TFT_BLACK);
    header(g, 0, title, 0x2299FF);
    const int rowH = 21;
    const int rows = (H - HEADER_H - FOOTER_H - 4) / rowH;
    int first      = std::max(0, std::min(selected - rows / 2, (int)items.size() - rows));
    g.setFont(kLabelFont);
    g.setTextDatum(middle_left);
    for (int r = 0; r < rows && first + r < (int)items.size(); ++r) {
        int i = first + r;
        int y = HEADER_H + 3 + r * rowH;
        if (i == selected) {
            g.fillRoundRect(2, y, W - 4, rowH - 2, 4, rgb(0x2299FF));
            g.setTextColor(TFT_BLACK);
        } else {
            g.setTextColor(TFT_WHITE);
        }
        g.drawString(fit(g, items[i], W - 16), 8, y + rowH / 2);
    }
    footer(g, 0, foot, false);
    g.endWrite();
}

void input(const String& title, const String& prompt, const String& value, const String& foot)
{
    auto& g = M5Cardputer.Display;
    g.startWrite();
    g.fillScreen(TFT_BLACK);
    header(g, 0, title, 0x2299FF);
    g.setFont(kBodyFont);
    g.setTextDatum(top_left);
    g.setTextColor(TFT_LIGHTGREY);
    g.drawString(fit(g, prompt, W - 10), 6, HEADER_H + 8);

    int bx = 4, by = HEADER_H + 32, bw = W - 8, bh = 32;
    g.drawRoundRect(bx, by, bw, bh, 5, rgb(0x2299FF));
    g.setFont(kLabelFont);
    g.setTextColor(TFT_WHITE);
    g.setTextDatum(middle_left);
    String shown = value;
    while (shown.length() && g.textWidth(shown + "_") > bw - 12) shown.remove(0, 1);
    g.drawString(shown + "_", bx + 6, by + bh / 2 + 1);
    footer(g, 0, foot, false);
    g.endWrite();
}

}  // namespace ui
