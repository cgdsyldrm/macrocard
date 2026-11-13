// USB HID keyboard + consumer control over the ESP32-S3 native USB (TinyUSB).
// Kept in its own translation unit: USBHIDKeyboard.h and the M5Cardputer
// keyboard header define conflicting KEY_* macros.
#include "transports.h"

#include <Arduino.h>

#if ARDUINO_USB_MODE == 0  // TinyUSB (OTG): real USB keyboard

#include <USB.h>
#include <esp32-hal-tinyusb.h>
#include <USBHIDConsumerControl.h>
#include <USBHIDKeyboard.h>

namespace {
// Deliberately no USB serial (CDC) port. Its auto-reset logic reboots the
// chip into the bootloader when a program on the PC toggles DTR/RTS or opens
// it at 1200 baud. Use Fn+U to flash instead.
USBHIDKeyboard g_keyboard;
USBHIDConsumerControl g_consumer;
volatile bool g_mounted   = false;
volatile bool g_suspended = false;

void onUsbEvent(void*, esp_event_base_t base, int32_t id, void*)
{
    if (base != ARDUINO_USB_EVENTS) return;
    switch (id) {
        case ARDUINO_USB_STARTED_EVENT: g_mounted = true; g_suspended = false; break;
        case ARDUINO_USB_STOPPED_EVENT: g_mounted = false; break;
        case ARDUINO_USB_SUSPEND_EVENT: g_suspended = true; break;
        case ARDUINO_USB_RESUME_EVENT: g_suspended = false; break;
        default: break;
    }
}
}  // namespace

namespace usbhid {

void begin(const char* productName)
{
    USB.onEvent(onUsbEvent);
    USB.manufacturerName("M5Stack");
    USB.productName(productName);
    g_keyboard.begin();
    g_consumer.begin();
    USB.begin();
}

bool ready()
{
    return g_mounted && !g_suspended;
}

void send(uint8_t modifiers, const uint8_t keys[6])
{
    KeyReport report{};
    report.modifiers = modifiers;
    report.reserved  = 0;
    memcpy(report.keys, keys, 6);
    g_keyboard.sendReport(&report);
}

void consumer(uint16_t usage)
{
    g_consumer.press(usage);
    delay(10);
    g_consumer.release();
}

void rebootToBootloader()
{
    usb_persist_restart(RESTART_BOOTLOADER);
}

}  // namespace usbhid

#else  // Hardware USB-Serial/JTAG mode (diagnostic build): no USB keyboard

namespace usbhid {
void begin(const char*) {}
bool ready() { return false; }
void send(uint8_t, const uint8_t[6]) {}
void consumer(uint16_t) {}
void rebootToBootloader() { ESP.restart(); }
}  // namespace usbhid

#endif
