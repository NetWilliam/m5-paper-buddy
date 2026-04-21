#include <cstdio>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <nvs_flash.h>

#include "config.h"
#include "display/lcd_driver.h"
#include "display/buddy_display.h"
#include "display/spiffs_image.h"
#include "button/button_handler.h"
#include "transport/serial_jtag_transport.h"
#include "transport/ble_transport.h"
#include "protocol/line_protocol.h"
#include "state/tama_state.h"
#include "state/state_parser.h"
#include "state/stats.h"
#include "state/xfer_commands.h"

static const char* TAG = "main";

// ── Constants ──
static constexpr uint32_t CONNECTION_TIMEOUT_MS = 15000;  // 15s no data = disconnected
static constexpr uint32_t PROMPT_TIMEOUT_MS     = 30000;  // 30s mirror bridge timeout
static constexpr uint32_t IDLE_IMAGE_TIMEOUT_MS = 300000;  // 5 min no data = idle image mode
static constexpr uint32_t IDLE_IMAGE_CYCLE_MS   = 15000;  // 15s per image

// ── Event bits for event-driven wakeup ──
static constexpr EventBits_t kEventDataReady  = (1 << 0);
static constexpr EventBits_t kEventButton      = (1 << 1);
static constexpr EventBits_t kEventAll         = kEventDataReady | kEventButton;

// ── Global context ──
static LcdDriver*           g_lcd       = nullptr;
static BuddyDisplay*        g_display   = nullptr;
static ButtonHandler*       g_buttons   = nullptr;
static SerialJtagTransport* g_usb       = nullptr;
static BleTransport*        g_ble       = nullptr;
static LineProtocol*        g_protocol  = nullptr;
static TamaState            g_state;
static SemaphoreHandle_t    g_state_mutex = nullptr;
static EventGroupHandle_t   g_wake_event  = nullptr;

static char g_bt_name[16] = "Claude";
static uint32_t g_prompt_arrived_ms = 0;
static bool g_response_sent = false;
static char g_last_prompt_id[40] = "";

// ── Idle image state ──
enum DisplayMode { MODE_DASHBOARD, MODE_APPROVAL, MODE_IDLE_IMAGE };
static DisplayMode g_display_mode = MODE_DASHBOARD;
static SpiffsImage g_spiffs;
static uint8_t* g_image_buf = nullptr;
static int g_image_slot = 0;
static uint32_t g_last_image_cycle_ms = 0;

// ── Dual-write send: both USB and BLE ──
static void sendLine(const char* json) {
    size_t n = strlen(json);
    if (g_usb) g_usb->Send((const uint8_t*)json, n);
    if (g_ble && g_ble->IsConnected()) g_ble->Send((const uint8_t*)json, n);
}

// xfer_commands callback
SendLineFn g_send_line = [](const char* line, size_t len) {
    if (g_usb) g_usb->Send((const uint8_t*)line, len);
    if (g_ble && g_ble->IsConnected()) g_ble->Send((const uint8_t*)line, len);
};

// ── Protocol message handler ──
static void OnMessage(cJSON* root) {
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        ApplyJson(root, &g_state);
        xSemaphoreGive(g_state_mutex);
    }
    xEventGroupSetBits(g_wake_event, kEventDataReady);
}

// ── USB data callback ──
static void OnUsbData(const uint8_t* data, size_t len) {
    g_protocol->Feed(data, len);
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "=== RLCD Buddy v0.1.0 ===");

    // NVS init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Synchronization primitives
    g_state_mutex = xSemaphoreCreateMutex();
    g_wake_event = xEventGroupCreate();

    // 1. LCD
    static LcdDriver lcd;
    g_lcd = &lcd;
    if (!lcd.Init()) {
        ESP_LOGE(TAG, "LCD init failed, halting");
        return;
    }

    // 2. Stats + pet name
    statsLoad();
    petNameLoad();

    // 3. SPIFFS for idle images
    g_spiffs.Init();
    int n_images = g_spiffs.GetSlotCount();
    if (n_images > 0) {
        g_image_buf = static_cast<uint8_t*>(heap_caps_malloc(SpiffsImage::IMAGE_SIZE, MALLOC_CAP_SPIRAM));
        assert(g_image_buf && "Failed to allocate image buffer");
        ESP_LOGI(TAG, "SPIFFS has %d idle images", n_images);
    }

    // 4. Buttons
    static ButtonHandler buttons;
    g_buttons = &buttons;
    buttons.Init();

    // 5. Display UI
    static BuddyDisplay display;
    g_display = &display;
    lvgl_port_lock(0);
    display.Init();
    display.ShowSplash(g_bt_name);
    lvgl_port_unlock();

    // 6. Protocol
    static LineProtocol protocol;
    g_protocol = &protocol;
    protocol.OnMessage(OnMessage);

    // 7. USB transport
    static SerialJtagTransport usb;
    g_usb = &usb;
    if (!usb.Init()) {
        ESP_LOGE(TAG, "USB-serial-jtag init failed");
    } else {
        usb.Open();
        usb.OnData(OnUsbData);
    }

    // 8. BLE transport
    static BleTransport ble;
    g_ble = &ble;
    ble.Init();
    ble.Open();
    ble.OnData(OnUsbData);  // same protocol handler as USB

    ESP_LOGI(TAG, "All subsystems ready, entering main loop");

    // Show dashboard after splash
    vTaskDelay(pdMS_TO_TICKS(1500));
    lvgl_port_lock(0);
    g_display->ShowDashboard(g_state);
    lvgl_port_unlock();

    // ── Main loop (event-driven) ──
    while (true) {
        // Wait for data or button event, with 1s timeout for periodic checks
        xEventGroupWaitBits(g_wake_event, kEventAll, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));

        // Poll USB for data
        g_usb->Poll();

        // ── State snapshot under mutex ──
        TamaState snap;
        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            snap = g_state;
            xSemaphoreGive(g_state_mutex);
        } else {
            continue;  // Skip this iteration if can't get lock
        }

        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // ── Connection timeout ──
        if (snap.connected && (now - snap.lastUpdated) > CONNECTION_TIMEOUT_MS) {
            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                g_state.connected = false;
                xSemaphoreGive(g_state_mutex);
            }
            snap.connected = false;
        }

        // ── Detect prompt changes ──
        const char* currentPid = snap.promptId;
        if (strcmp(currentPid, g_last_prompt_id) != 0) {
            strncpy(g_last_prompt_id, currentPid, sizeof(g_last_prompt_id) - 1);
            g_last_prompt_id[sizeof(g_last_prompt_id) - 1] = 0;
            g_response_sent = false;
            if (currentPid[0]) {
                g_prompt_arrived_ms = now;
            }
        }

        bool inPrompt = snap.HasPrompt() && !g_response_sent;

        // ── Prompt timeout (mirror bridge's 30s) ──
        if (inPrompt && (now - g_prompt_arrived_ms) > PROMPT_TIMEOUT_MS) {
            g_response_sent = true;
            inPrompt = false;
        }

        // ── Button handling ──
        EventBits_t events = g_buttons->Poll();
        if (events & kButtonEventApprove) {
            ESP_LOGI(TAG, "Approve button pressed");
            if (inPrompt) {
                char resp[128];
                snprintf(resp, sizeof(resp),
                    "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"once\"}\n",
                    snap.promptId);
                sendLine(resp);
                g_response_sent = true;
                statsOnApproval((now - g_prompt_arrived_ms) / 1000);
            }
        }
        if (events & kButtonEventDeny) {
            ESP_LOGI(TAG, "Deny button pressed");
            if (inPrompt) {
                char resp[128];
                snprintf(resp, sizeof(resp),
                    "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"deny\"}\n",
                    snap.promptId);
                sendLine(resp);
                g_response_sent = true;
                statsOnDenial();
            }
        }

        // ── Refresh display ──
        static uint32_t last_refresh_ms = 0;
        uint32_t refresh_gap = inPrompt ? 1000 : 5000;
        bool can_refresh = (now - last_refresh_ms) >= refresh_gap;

        static bool was_in_prompt = false;
        bool prompt_changed = (inPrompt != was_in_prompt);
        was_in_prompt = inPrompt;

        // Any activity (data, prompt change, button) exits idle image mode
        bool has_activity = prompt_changed || (events != 0);

        // Determine display mode
        DisplayMode target_mode;
        if (inPrompt) {
            target_mode = MODE_APPROVAL;
        } else if (g_image_buf && !snap.connected &&
                   (now - snap.lastUpdated) > IDLE_IMAGE_TIMEOUT_MS) {
            target_mode = MODE_IDLE_IMAGE;
        } else {
            target_mode = MODE_DASHBOARD;
        }

        bool mode_changed = (target_mode != g_display_mode);
        if (has_activity && g_display_mode == MODE_IDLE_IMAGE) {
            mode_changed = true;
            g_display_mode = MODE_DASHBOARD;
            target_mode = MODE_DASHBOARD;
        }

        if (target_mode == MODE_APPROVAL || target_mode == MODE_DASHBOARD) {
            if (can_refresh || mode_changed) {
                lvgl_port_lock(0);
                if (target_mode == MODE_APPROVAL) {
                    g_display->ShowApproval(snap, now - g_prompt_arrived_ms);
                } else {
                    g_display->ShowDashboard(snap);
                }
                lvgl_port_unlock();
                last_refresh_ms = now;
            }
        } else if (target_mode == MODE_IDLE_IMAGE) {
            if (mode_changed || (now - g_last_image_cycle_ms) >= IDLE_IMAGE_CYCLE_MS) {
                if (g_spiffs.LoadImage(g_image_buf, SpiffsImage::IMAGE_SIZE, g_image_slot) == SpiffsImage::IMAGE_SIZE) {
                    lvgl_port_lock(0);
                    g_lcd->DisplayRaw(g_image_buf);
                    lvgl_port_unlock();
                }
                g_last_image_cycle_ms = now;
                g_image_slot = (g_image_slot + 1) % SpiffsImage::MAX_SLOTS;
                // Skip empty slots
                for (int i = 0; i < SpiffsImage::MAX_SLOTS && !g_spiffs.HasCustomImage(g_image_slot); i++) {
                    g_image_slot = (g_image_slot + 1) % SpiffsImage::MAX_SLOTS;
                }
            }
        }

        g_display_mode = target_mode;
    }
}
