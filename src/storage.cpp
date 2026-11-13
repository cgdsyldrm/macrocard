#include "storage.h"

#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

#include "default_config.generated.h"

namespace storage {
namespace {

constexpr int SD_SCK  = 40;
constexpr int SD_MISO = 39;
constexpr int SD_MOSI = 14;
constexpr int SD_CS   = 12;
constexpr const char* CONFIG_PATH = "/macropad.json";
constexpr const char* BACKUP_PATH = "/macropad.bak";

bool g_sd    = false;
bool g_flash = false;

bool sdMount()
{
    static bool spiStarted = false;
    if (!spiStarted) {
        SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
        spiStarted = true;
    }
    SD.end();
    g_sd = SD.begin(SD_CS, SPI, 25000000) && SD.cardType() != CARD_NONE;
    return g_sd;
}

String readFile(fs::FS& fs, const char* path, bool& ok)
{
    ok     = false;
    File f = fs.open(path, FILE_READ);
    if (!f) return String();
    String text = f.readString();
    f.close();
    ok = text.length() > 0;
    return text;
}

bool writeFile(fs::FS& fs, const char* path, const String& text)
{
    File f = fs.open(path, FILE_WRITE);
    if (!f) return false;
    size_t n = f.print(text);
    f.close();
    return n == text.length();
}

}  // namespace

void begin()
{
    // The 8MB partition table has a "spiffs" data partition; use it as LittleFS.
    g_flash = LittleFS.begin(true, "/littlefs", 5, "spiffs");
    sdMount();
}

bool hasSd() { return g_sd; }

String load(String& source)
{
    bool ok = false;
    if (sdMount()) {
        if (SD.exists(CONFIG_PATH)) {
            String t = readFile(SD, CONFIG_PATH, ok);
            if (ok) {
                source = "SD card";
                return t;
            }
        } else {
            // First run with a card: put the example there so it can be edited.
            writeFile(SD, CONFIG_PATH, String(kDefaultConfig, kDefaultConfigLen));
        }
    }
    if (g_flash && LittleFS.exists(CONFIG_PATH)) {
        String t = readFile(LittleFS, CONFIG_PATH, ok);
        if (ok) {
            source = "internal flash";
            return t;
        }
    }
    source = "defaults";
    return String(kDefaultConfig, kDefaultConfigLen);
}

bool save(const String& text, String& where, String& error)
{
    if (sdMount()) {
        if (SD.exists(CONFIG_PATH)) {
            SD.remove(BACKUP_PATH);
            SD.rename(CONFIG_PATH, BACKUP_PATH);
        }
        if (writeFile(SD, CONFIG_PATH, text)) {
            where = "SD card";
            return true;
        }
        error = "Could not write to the SD card";
        return false;
    }
    if (g_flash && writeFile(LittleFS, CONFIG_PATH, text)) {
        where = "internal flash";
        return true;
    }
    error = "Nowhere to save (no SD card and internal flash failed)";
    return false;
}

}  // namespace storage
