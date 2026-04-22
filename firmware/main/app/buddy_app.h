#ifndef _BUDDY_APP_H_
#define _BUDDY_APP_H_

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include "config.h"
#include "hal/lcd_driver.h"
#include "ui/buddy_display.h"
#include "hal/spiffs_image.h"
#include "hal/button_handler.h"
#include "net/serial_jtag_transport.h"
#include "net/ble_transport.h"
#include "net/line_protocol.h"
#include "model/tama_state.h"
#include "model/xfer_commands.h"

class BuddyApp {
public:
    bool Init();
    void Run();  // never returns

private:
    // Subsystem instances
    LcdDriver lcd_;
    BuddyDisplay display_;
    SpiffsImage spiffs_;
    ButtonHandler buttons_;
    SerialJtagTransport usb_;
    BleTransport ble_;
    LineProtocol protocol_;
    XferCommands xfer_;

    // State
    TamaState state_{};
    SemaphoreHandle_t mutex_ = nullptr;
    EventGroupHandle_t events_ = nullptr;

    char bt_name_[16] = "Claude";
    uint32_t prompt_arrived_ms_ = 0;
    bool response_sent_ = false;
    char last_prompt_id_[40] = "";

    // Display mode
    enum DisplayMode { MODE_DASHBOARD, MODE_APPROVAL, MODE_IDLE_IMAGE, MODE_PASSKEY };
    DisplayMode display_mode_ = MODE_DASHBOARD;
    uint32_t pending_passkey_ = 0;
    uint8_t* image_buf_ = nullptr;
    int image_slot_ = 0;
    uint32_t last_image_cycle_ms_ = 0;

    // Callbacks
    void OnMessage(cJSON* root);
    void OnUsbData(const uint8_t* data, size_t len);

    // Helpers
    void SendLine(const char* json);
    void SendRaw(const char* line, size_t len);
    void SyncNames(TamaState& state);
};

#endif // _BUDDY_APP_H_
