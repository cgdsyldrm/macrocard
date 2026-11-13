# Patched copy of the arduino-esp32 3.3.6 BLE library

PlatformIO uses this copy instead of the framework's BLE library.

The only change is in `src/BLEService.cpp` → `addCharacteristic()`. With the
NimBLE backend, upstream refused to register a second characteristic with an
existing UUID. HID over GATT needs one 0x2A4D characteristic per report
(keyboard input, LED output, consumer input). Because of that, the media-key
report was never created, and `notify()` on it hit
`assert(getService()->getServer() != nullptr)`, which rebooted the device.

When updating the framework, re-copy the library and re-apply that change.
