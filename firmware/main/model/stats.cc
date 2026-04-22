#include "stats.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <cstring>
#include <esp_log.h>

static const char* TAG = "Stats";

static Stats _stats;
static bool _stats_dirty = false;

static char _petName[24] = "Buddy";
static char _ownerName[32] = "";

void statsLoad() {
    nvs_handle_t h;
    if (nvs_open("buddy", NVS_READONLY, &h) != ESP_OK) return;
    nvs_get_u16(h, "appr", &_stats.approvals);
    nvs_get_u16(h, "deny", &_stats.denials);
    nvs_get_u8(h, "lvl", &_stats.level);
    nvs_get_u32(h, "tok", &_stats.tokens);
    nvs_close(h);
    if (_stats.tokens == 0 && _stats.level > 0) {
        _stats.tokens = (uint32_t)_stats.level * TOKENS_PER_LEVEL;
    }
}

void statsSave() {
    if (!_stats_dirty) return;
    nvs_handle_t h;
    if (nvs_open("buddy", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u16(h, "appr", _stats.approvals);
    nvs_set_u16(h, "deny", _stats.denials);
    nvs_set_u8(h, "lvl", _stats.level);
    nvs_set_u32(h, "tok", _stats.tokens);
    nvs_commit(h);
    nvs_close(h);
    _stats_dirty = false;
}

void statsOnApproval(uint32_t secondsToRespond) {
    _stats.approvals++;
    _stats_dirty = true;
    statsSave();
}

void statsOnDenial() {
    _stats.denials++;
    _stats_dirty = true;
    statsSave();
}

const Stats& stats() { return _stats; }

void petNameLoad() {
    nvs_handle_t h;
    if (nvs_open("buddy", NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(_petName);
    nvs_get_str(h, "petname", _petName, &len);
    len = sizeof(_ownerName);
    nvs_get_str(h, "owner", _ownerName, &len);
    nvs_close(h);
}

const char* petName() { return _petName; }
const char* ownerName() { return _ownerName; }

void petNameSet(const char* name) {
    strncpy(_petName, name, sizeof(_petName) - 1);
    _petName[sizeof(_petName) - 1] = 0;
    nvs_handle_t h;
    if (nvs_open("buddy", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "petname", _petName);
        nvs_commit(h);
        nvs_close(h);
    }
}

void ownerSet(const char* name) {
    strncpy(_ownerName, name, sizeof(_ownerName) - 1);
    _ownerName[sizeof(_ownerName) - 1] = 0;
    nvs_handle_t h;
    if (nvs_open("buddy", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "owner", _ownerName);
        nvs_commit(h);
        nvs_close(h);
    }
}
