#include "serial_jtag_transport.h"
#include <esp_log.h>
#include <cstring>

SerialJtagTransport::~SerialJtagTransport() {
    Close();
}

bool SerialJtagTransport::Init() {
    if (initialized_) return true;

    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.tx_buffer_size = 1024;
    cfg.rx_buffer_size = 8192;

    esp_err_t ret = usb_serial_jtag_driver_install(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install failed: %s", esp_err_to_name(ret));
        return false;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "USB-serial-jtag transport ready (TX=1024 RX=8192)");
    return true;
}

bool SerialJtagTransport::Open() {
    if (!initialized_ && !Init()) return false;
    return true;
}

void SerialJtagTransport::Close() {
    if (initialized_) {
        usb_serial_jtag_driver_uninstall();
        initialized_ = false;
        ESP_LOGI(TAG, "USB-serial-jtag transport closed");
    }
}

int SerialJtagTransport::Send(const uint8_t* data, size_t len) {
    if (!initialized_) return -1;
    int written = usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(10));
    return written;
}

void SerialJtagTransport::OnData(DataCallback callback) {
    data_callback_ = callback;
}

static uint8_t s_poll_buf[4096];

size_t SerialJtagTransport::Poll() {
    if (!initialized_ || !data_callback_) return 0;

    int n = usb_serial_jtag_read_bytes(s_poll_buf, sizeof(s_poll_buf), 0);
    if (n > 0 && data_callback_) {
        data_callback_(s_poll_buf, n);
    }
    return n > 0 ? static_cast<size_t>(n) : 0;
}
