// Routes key reports to USB or Bluetooth depending on the selected mode.
#pragma once
#include <stdint.h>

namespace out {

enum class Mode : uint8_t { Auto = 0, Usb = 1, Ble = 2 };
enum class Link : uint8_t { None, Usb, Ble };

// enableBle=false keeps ~80 KB of RAM free (needed for Wi-Fi setup mode).
void begin(const char* deviceName, bool enableBle = true);

void setMode(Mode m);
Mode mode();
Mode nextMode(Mode m);
const char* modeName(Mode m);

// Auto: USB when the computer has enumerated the pad, otherwise Bluetooth.
Link activeLink();
const char* linkName(Link l);
bool usbReady();
bool bleConnected();

// Press the given keys, wait, release, wait. Returns false if nothing is connected.
bool tap(uint8_t modifiers, const uint8_t keys[6], uint16_t delayMs);
bool consumer(uint16_t usage);
// Sends one raw report (keys stay down until the next report).
bool report(uint8_t modifiers, const uint8_t keys[6]);
void releaseAll();
void setBattery(uint8_t percent);
void rebootToBootloader();

}  // namespace out
