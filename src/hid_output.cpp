#include "hid_output.h"

#include <Arduino.h>

#include "transports.h"

namespace out {
namespace {
Mode g_mode = Mode::Auto;

void send(Link link, uint8_t modifiers, const uint8_t keys[6])
{
    if (link == Link::Usb) usbhid::send(modifiers, keys);
    else if (link == Link::Ble) blehid::send(modifiers, keys);
}
}  // namespace

void begin(const char* deviceName, bool enableBle)
{
    usbhid::begin(deviceName);
    if (enableBle) blehid::begin(deviceName);
}

void setMode(Mode m) { g_mode = m; }
Mode mode() { return g_mode; }

Mode nextMode(Mode m)
{
    switch (m) {
        case Mode::Auto: return Mode::Usb;
        case Mode::Usb: return Mode::Ble;
        default: return Mode::Auto;
    }
}

const char* modeName(Mode m)
{
    switch (m) {
        case Mode::Usb: return "USB";
        case Mode::Ble: return "BT";
        default: return "AUTO";
    }
}

bool usbReady() { return usbhid::ready(); }
bool bleConnected() { return blehid::connected(); }

Link activeLink()
{
    switch (g_mode) {
        case Mode::Usb: return usbReady() ? Link::Usb : Link::None;
        case Mode::Ble: return bleConnected() ? Link::Ble : Link::None;
        default:
            if (usbReady()) return Link::Usb;
            if (bleConnected()) return Link::Ble;
            return Link::None;
    }
}

const char* linkName(Link l)
{
    switch (l) {
        case Link::Usb: return "USB";
        case Link::Ble: return "BT";
        default: return "--";
    }
}

bool tap(uint8_t modifiers, const uint8_t keys[6], uint16_t delayMs)
{
    Link link = activeLink();
    if (link == Link::None) return false;
    static const uint8_t none[6] = {0, 0, 0, 0, 0, 0};
    // Send modifiers alone first: some apps miss a modifier that arrives in
    // the same report as the key.
    if (modifiers) {
        send(link, modifiers, none);
        delay(delayMs);
    }
    send(link, modifiers, keys);
    delay(delayMs);
    send(link, 0, none);
    delay(delayMs);
    return true;
}

bool report(uint8_t modifiers, const uint8_t keys[6])
{
    Link link = activeLink();
    if (link == Link::None) return false;
    send(link, modifiers, keys);
    return true;
}

bool consumer(uint16_t usage)
{
    Link link = activeLink();
    if (link == Link::Usb) usbhid::consumer(usage);
    else if (link == Link::Ble) blehid::consumer(usage);
    else return false;
    return true;
}

void releaseAll()
{
    static const uint8_t none[6] = {0, 0, 0, 0, 0, 0};
    if (usbReady()) usbhid::send(0, none);
    if (bleConnected()) blehid::send(0, none);
}

void setBattery(uint8_t percent) { blehid::battery(percent); }

void rebootToBootloader()
{
    releaseAll();
    usbhid::rebootToBootloader();
}

}  // namespace out
