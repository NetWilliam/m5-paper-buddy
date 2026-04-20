// Stub nvs_flash.h for host-side testing
#pragma once
#include <cstdint>
#include <cstddef>

typedef int nvs_handle_t;
#define NVS_READONLY  0
#define NVS_READWRITE 1
#define ESP_OK        0
#define ESP_ERR_NVS_NO_FREE_PAGES    1
#define ESP_ERR_NVS_NEW_VERSION_FOUND 2

static inline int nvs_flash_init() { return ESP_OK; }
static inline int nvs_flash_erase() { return ESP_OK; }
static inline int nvs_open(const char*, int, nvs_handle_t*) { return ESP_OK; }
static inline void nvs_close(nvs_handle_t) {}
static inline int nvs_commit(nvs_handle_t) { return ESP_OK; }

static inline int nvs_get_u16(nvs_handle_t, const char*, uint16_t* v) { *v = 0; return ESP_OK; }
static inline int nvs_get_u8(nvs_handle_t, const char*, uint8_t* v) { *v = 0; return ESP_OK; }
static inline int nvs_get_u32(nvs_handle_t, const char*, uint32_t* v) { *v = 0; return ESP_OK; }
static inline int nvs_get_str(nvs_handle_t, const char*, char* buf, size_t* len) { if (*len > 0) buf[0] = 0; return ESP_OK; }

static inline int nvs_set_u16(nvs_handle_t, const char*, uint16_t) { return ESP_OK; }
static inline int nvs_set_u8(nvs_handle_t, const char*, uint8_t) { return ESP_OK; }
static inline int nvs_set_u32(nvs_handle_t, const char*, uint32_t) { return ESP_OK; }
static inline int nvs_set_str(nvs_handle_t, const char*, const char*) { return ESP_OK; }
