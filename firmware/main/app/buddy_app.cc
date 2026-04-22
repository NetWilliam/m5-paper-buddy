#include "app/buddy_app.h"

#include <cstdio>
#include <cstring>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <nvs_flash.h>

#include "model/state_parser.h"
#include "model/stats.h"

static const char* TAG = "BuddyApp";

// ── Constants ──
static constexpr uint32_t CONNECTION_TIMEOUT_MS = 15000;
static constexpr uint32_t PROMPT_TIMEOUT_MS     = 30000;
static constexpr uint32_t IDLE_IMAGE_TIMEOUT_MS = 300000;
static constexpr uint32_t IDLE_IMAGE_CYCLE_MS   = 15000;

static constexpr EventBits_t kEventDataReady = (1 << 0);
static constexpr EventBits_t kEventButton    = (1 << 1);
static constexpr EventBits_t kEventPasskey   = (1 << 2);
static constexpr EventBits_t kEventAll       = kEventDataReady | kEventButton | kEventPasskey;

// ── Helpers ──

void BuddyApp::SendRaw(const char* line, size_t len) {
    usb_.Send((const uint8_t*)line, len);
    if (ble_.IsConnected()) ble_.Send((const uint8_t*)line, len);
}

void BuddyApp::SendLine(const char* json) {
    SendRaw(json, strlen(json));
}

void BuddyApp::SyncNames(TamaState& state) {
    strncpy(state.ownerName, ownerName(), sizeof(state.ownerName) - 1);
    strncpy(state.petName, petName(), sizeof(state.petName) - 1);
}

// ── Callbacks ──

void BuddyApp::OnUsbData(const uint8_t* data, size_t len) {
    protocol_.Feed(data, len);
}

void BuddyApp::OnMessage(cJSON* root) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (!xfer_.TryHandle(root)) {
            ApplyJson(root, &state_);
        } else {
            state_.lastUpdated = xTaskGetTickCount() * portTICK_PERIOD_MS;
        }
        xSemaphoreGive(mutex_);
    }
    xEventGroupSetBits(events_, kEventDataReady);
}

// ── Init ──

bool BuddyApp::Init() {
    ESP_LOGI(TAG, "=== RLCD Buddy v0.1.0 ===");

    // NVS init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Synchronization primitives
    mutex_ = xSemaphoreCreateMutex();
    events_ = xEventGroupCreate();

    // 1. LCD
    if (!lcd_.Init()) {
        ESP_LOGE(TAG, "LCD init failed, halting");
        return false;
    }

    // 2. Stats + pet name
    statsLoad();
    petNameLoad();

    // 3. SPIFFS for idle images
    spiffs_.Init();
    int n_images = spiffs_.GetSlotCount();
    if (n_images > 0) {
        image_buf_ = static_cast<uint8_t*>(heap_caps_malloc(SpiffsImage::IMAGE_SIZE, MALLOC_CAP_SPIRAM));
        assert(image_buf_ && "Failed to allocate image buffer");
        ESP_LOGI(TAG, "SPIFFS has %d idle images", n_images);
    }

    // 4. Buttons
    buttons_.Init();

    // 5. Display UI
    lvgl_port_lock(0);
    display_.Init();
    display_.ShowSplash(bt_name_);
    lvgl_port_unlock();

    // 6. Protocol
    protocol_.OnMessage([this](cJSON* root) { OnMessage(root); });

    // 7. USB transport
    if (!usb_.Init()) {
        ESP_LOGE(TAG, "USB-serial-jtag init failed");
    } else {
        usb_.Open();
        usb_.OnData([this](const uint8_t* data, size_t len) { OnUsbData(data, len); });
    }

    // 8. BLE transport
    ble_.Init();
    ble_.Open();
    ble_.OnData([this](const uint8_t* data, size_t len) { OnUsbData(data, len); });
    ble_.OnPasskey([this](uint32_t pk) {
        pending_passkey_ = pk;
        xEventGroupSetBits(events_, kEventPasskey);
    });

    // 9. Xfer commands
    xfer_.Init([this](const char* line, size_t len) { SendRaw(line, len); });

    ESP_LOGI(TAG, "All subsystems ready, entering main loop");

    // Show dashboard after splash
    vTaskDelay(pdMS_TO_TICKS(1500));
    SyncNames(state_);
    lvgl_port_lock(0);
    display_.ShowDashboard(state_);
    lvgl_port_unlock();

    return true;
}

// ── Main loop ──

void BuddyApp::Run() {
    while (true) {
        xEventGroupWaitBits(events_, kEventAll, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));

        usb_.Poll();

        // State snapshot under mutex
        TamaState snap;
        if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
            snap = state_;
            xSemaphoreGive(mutex_);
            SyncNames(snap);
        } else {
            continue;
        }

        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Connection timeout
        if (snap.connected && (now - snap.lastUpdated) > CONNECTION_TIMEOUT_MS) {
            if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
                state_.connected = false;
                xSemaphoreGive(mutex_);
            }
            snap.connected = false;
        }

        // Detect prompt changes
        const char* currentPid = snap.promptId;
        if (strcmp(currentPid, last_prompt_id_) != 0) {
            strncpy(last_prompt_id_, currentPid, sizeof(last_prompt_id_) - 1);
            last_prompt_id_[sizeof(last_prompt_id_) - 1] = 0;
            response_sent_ = false;
            if (currentPid[0]) {
                prompt_arrived_ms_ = now;
            }
        }

        bool inPrompt = snap.HasPrompt() && !response_sent_;

        // Prompt timeout
        if (inPrompt && (now - prompt_arrived_ms_) > PROMPT_TIMEOUT_MS) {
            response_sent_ = true;
            inPrompt = false;
        }

        // Button handling
        EventBits_t events = buttons_.Poll();
        if (events & kButtonEventApprove) {
            ESP_LOGI(TAG, "Approve button pressed");
            if (inPrompt) {
                char resp[128];
                snprintf(resp, sizeof(resp),
                    "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"once\"}\n",
                    snap.promptId);
                SendLine(resp);
                response_sent_ = true;
                statsOnApproval((now - prompt_arrived_ms_) / 1000);
            }
        }
        if (events & kButtonEventDeny) {
            ESP_LOGI(TAG, "Deny button pressed");
            if (inPrompt) {
                char resp[128];
                snprintf(resp, sizeof(resp),
                    "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"deny\"}\n",
                    snap.promptId);
                SendLine(resp);
                response_sent_ = true;
                statsOnDenial();
            }
        }

        // Refresh display
        static uint32_t last_refresh_ms = 0;
        uint32_t refresh_gap = inPrompt ? 1000 : 5000;
        bool can_refresh = (now - last_refresh_ms) >= refresh_gap;

        static bool was_in_prompt = false;
        bool prompt_changed = (inPrompt != was_in_prompt);
        was_in_prompt = inPrompt;

        bool has_activity = prompt_changed || (events != 0);

        // Determine display mode
        DisplayMode target_mode;
        if (pending_passkey_ > 0) {
            target_mode = MODE_PASSKEY;
        } else if (inPrompt) {
            target_mode = MODE_APPROVAL;
        } else if (image_buf_ && !snap.connected &&
                   (now - snap.lastUpdated) > IDLE_IMAGE_TIMEOUT_MS) {
            target_mode = MODE_IDLE_IMAGE;
        } else {
            target_mode = MODE_DASHBOARD;
        }

        bool mode_changed = (target_mode != display_mode_);
        if (has_activity && display_mode_ == MODE_IDLE_IMAGE) {
            mode_changed = true;
            display_mode_ = MODE_DASHBOARD;
            target_mode = MODE_DASHBOARD;
        }

        if (target_mode == MODE_PASSKEY) {
            if (mode_changed || display_mode_ != MODE_PASSKEY) {
                lvgl_port_lock(0);
                display_.ShowPasskey(pending_passkey_);
                lvgl_port_unlock();
                last_refresh_ms = now;
            }
        } else if (target_mode == MODE_APPROVAL || target_mode == MODE_DASHBOARD) {
            if (can_refresh || mode_changed) {
                pending_passkey_ = 0;
                lvgl_port_lock(0);
                if (target_mode == MODE_APPROVAL) {
                    display_.ShowApproval(snap, now - prompt_arrived_ms_);
                } else {
                    display_.ShowDashboard(snap);
                }
                lvgl_port_unlock();
                last_refresh_ms = now;
            }
        } else if (target_mode == MODE_IDLE_IMAGE) {
            if (mode_changed || (now - last_image_cycle_ms_) >= IDLE_IMAGE_CYCLE_MS) {
                if (spiffs_.LoadImage(image_buf_, SpiffsImage::IMAGE_SIZE, image_slot_) == SpiffsImage::IMAGE_SIZE) {
                    lvgl_port_lock(0);
                    lcd_.DisplayRaw(image_buf_);
                    lvgl_port_unlock();
                }
                last_image_cycle_ms_ = now;
                image_slot_ = (image_slot_ + 1) % SpiffsImage::MAX_SLOTS;
                for (int i = 0; i < SpiffsImage::MAX_SLOTS && !spiffs_.HasCustomImage(image_slot_); i++) {
                    image_slot_ = (image_slot_ + 1) % SpiffsImage::MAX_SLOTS;
                }
            }
        }

        display_mode_ = target_mode;
    }
}
