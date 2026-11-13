// Bluetooth LE HID keyboard (report 1) + consumer control (report 2),
// built directly on the ESP32 Arduino core BLE library.
#include "transports.h"

#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>

namespace {

constexpr uint8_t REPORT_KEYBOARD = 1;
constexpr uint8_t REPORT_CONSUMER = 2;

// clang-format off
const uint8_t kReportMap[] = {
    // Keyboard
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_KEYBOARD,
    0x05, 0x07,        //   Usage Page (Keyboard)
    0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,        // 8 modifier bits
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,        // reserved byte
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08,        // 5 LEDs
    0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,        // LED padding
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00,        // 6 key slots
    0x26, 0xE7, 0x00, 0x05, 0x07,              // logical max 231 (covers F13-F24)
    0x19, 0x00, 0x29, 0xE7, 0x81, 0x00,
    0xC0,
    // Consumer control: one 16-bit usage
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,
    0x85, REPORT_CONSUMER,
    0x15, 0x00, 0x26, 0xFF, 0x03,
    0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xC0,
};
// clang-format on

BLEHIDDevice* g_hid           = nullptr;
BLECharacteristic* g_keyboard = nullptr;
BLECharacteristic* g_consumer = nullptr;
volatile bool g_connected     = false;

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*) override
    {
        g_connected = true;
    }
    void onDisconnect(BLEServer*) override
    {
        g_connected = false;
        BLEDevice::startAdvertising();
    }
};

}  // namespace

namespace blehid {

void begin(const char* deviceName)
{
    BLEDevice::init(deviceName);
    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    g_hid      = new BLEHIDDevice(server);
    g_keyboard = g_hid->inputReport(REPORT_KEYBOARD);
    g_hid->outputReport(REPORT_KEYBOARD);  // LED report, required by some hosts
    g_consumer = g_hid->inputReport(REPORT_CONSUMER);

    // manufacturer() creates the characteristic; the manufacturer(String)
    // overload writes to it without creating it and crashes on a null pointer.
    g_hid->manufacturer()->setValue(String("M5Stack"));
    g_hid->pnp(0x02, 0x303A, 0x8001, 0x0100);
    g_hid->hidInfo(0x00, 0x01);

    // "Just works" pairing with bonding, so the computer remembers the pad.
    BLESecurity::setAuthenticationMode(true, false, true);

    g_hid->reportMap((uint8_t*)kReportMap, sizeof(kReportMap));
    g_hid->startServices();
    g_hid->setBatteryLevel(100);

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(g_hid->hidService()->getUUID());
    adv->start();
}

bool connected()
{
    return g_connected;
}

void send(uint8_t modifiers, const uint8_t keys[6])
{
    if (!g_connected || !g_keyboard) return;
    uint8_t report[8] = {modifiers, 0, keys[0], keys[1], keys[2], keys[3], keys[4], keys[5]};
    g_keyboard->setValue(report, sizeof(report));
    g_keyboard->notify();
}

void consumer(uint16_t usage)
{
    if (!g_connected || !g_consumer) return;
    uint8_t press[2] = {(uint8_t)(usage & 0xff), (uint8_t)(usage >> 8)};
    g_consumer->setValue(press, sizeof(press));
    g_consumer->notify();
    delay(15);
    uint8_t release[2] = {0, 0};
    g_consumer->setValue(release, sizeof(release));
    g_consumer->notify();
}

void battery(uint8_t percent)
{
    if (g_hid) g_hid->setBatteryLevel(percent);
}

}  // namespace blehid
