#include "spiffs_image.h"
#include "config.h"
#include <esp_log.h>
#include <esp_spiffs.h>
#include <cstdio>

static const char* TAG = "SpiffsImage";

bool SpiffsImage::Init() {
    if (mounted_) return true;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = SPIFFS_BASE,
        .partition_label = nullptr,
        .max_files = 5,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
        return false;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(nullptr, &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted: %zu/%zu bytes used", used, total);
    mounted_ = true;
    return true;
}

bool SpiffsImage::SaveImage(const uint8_t* data, size_t len, int slot) {
    if (!mounted_) return false;
    if (slot < 0 || slot >= MAX_SLOTS) return false;
    if (len != IMAGE_SIZE) {
        ESP_LOGE(TAG, "Invalid image size: %zu, expected %zu", len, IMAGE_SIZE);
        return false;
    }

    char path[64];
    snprintf(path, sizeof(path), "%s/slot%d.raw", SPIFFS_BASE, slot);

    FILE* f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", path);
        return false;
    }

    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    if (written != len) {
        ESP_LOGE(TAG, "Write failed: %zu/%zu bytes", written, len);
        return false;
    }

    ESP_LOGI(TAG, "Image saved to slot %d: %zu bytes", slot, written);
    return true;
}

size_t SpiffsImage::LoadImage(uint8_t* buf, size_t buf_size, int slot) {
    if (!mounted_ || slot < 0 || slot >= MAX_SLOTS) return 0;

    char path[64];
    snprintf(path, sizeof(path), "%s/slot%d.raw", SPIFFS_BASE, slot);

    FILE* f = fopen(path, "rb");
    if (!f) return 0;

    size_t n = fread(buf, 1, buf_size, f);
    fclose(f);
    return n;
}

bool SpiffsImage::HasCustomImage(int slot) {
    if (!mounted_ || slot < 0 || slot >= MAX_SLOTS) return false;

    char path[64];
    snprintf(path, sizeof(path), "%s/slot%d.raw", SPIFFS_BASE, slot);

    FILE* f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    return size == static_cast<long>(IMAGE_SIZE);
}

void SpiffsImage::DeleteImage(int slot) {
    if (!mounted_ || slot < 0 || slot >= MAX_SLOTS) return;

    char path[64];
    snprintf(path, sizeof(path), "%s/slot%d.raw", SPIFFS_BASE, slot);
    remove(path);
    ESP_LOGI(TAG, "Image deleted from slot %d", slot);
}

bool SpiffsImage::HasAnyImage() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (HasCustomImage(i)) return true;
    }
    return false;
}

int SpiffsImage::GetSlotCount() {
    int count = 0;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (HasCustomImage(i)) count++;
    }
    return count;
}
