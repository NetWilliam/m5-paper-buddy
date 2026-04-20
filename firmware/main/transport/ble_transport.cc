#include "ble_transport.h"
#include <esp_log.h>
#include <cstring>

// Placeholder BLE transport. Full NimBLE NUS implementation will be
// filled in during integration testing. This stub allows the project
// to compile and run with USB-serial-jtag only.

static const char* TAG = "BleTransport";

BleTransport::BleTransport() = default;
BleTransport::~BleTransport() { Close(); }

bool BleTransport::Init() {
    if (initialized_) return true;
    // TODO: Initialize NimBLE stack, register NUS service
    // For now, mark as initialized but not functional
    initialized_ = true;
    ESP_LOGI(TAG, "BLE transport stub initialized (NUS not yet implemented)");
    return true;
}

bool BleTransport::Open() {
    if (!initialized_ && !Init()) return false;
    // TODO: Start BLE advertising
    return true;
}

void BleTransport::Close() {
    if (initialized_) {
        // TODO: Stop advertising, disconnect
        connected_ = false;
        initialized_ = false;
    }
}

int BleTransport::Send(const uint8_t* data, size_t len) {
    if (!connected_) return -1;
    // TODO: Write to NUS TX characteristic
    return -1;
}

void BleTransport::OnData(DataCallback callback) {
    data_callback_ = callback;
}

size_t BleTransport::Poll() {
    // BLE uses callbacks (NimBLE notify), not polling
    return 0;
}

bool BleTransport::IsConnected() const { return connected_; }
uint32_t BleTransport::GetPasskey() const { return passkey_; }
