// Low-level HID transports. Use the `out` routing layer (hid_output.h) instead.
#pragma once
#include <stdint.h>

namespace usbhid {
void begin(const char* productName);
bool ready();
void send(uint8_t modifiers, const uint8_t keys[6]);
void consumer(uint16_t usage);
void rebootToBootloader();  // ROM download mode, for flashing new firmware
}  // namespace usbhid

namespace blehid {
void begin(const char* deviceName);
bool connected();
void send(uint8_t modifiers, const uint8_t keys[6]);
void consumer(uint16_t usage);
void battery(uint8_t percent);
}  // namespace blehid
