#include <cstring>
#include <cassert>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_lcd_panel_io.h>
#include <esp_lvgl_port.h>
#include <esp_heap_caps.h>
#include "lcd_driver.h"
#include "config.h"

// ----------------------------------------------------------------------
// SOFTWARE ROTATION FOR ST7305
// ----------------------------------------------------------------------
#define PHYS_WIDTH  400
#define PHYS_HEIGHT 300

static inline uint32_t GetSt7305Idx(uint16_t px, uint16_t py) {
    uint16_t inv_y = PHYS_HEIGHT - 1 - py;
    return (px >> 1) * (PHYS_HEIGHT >> 2) + (inv_y >> 2);
}

static inline uint8_t GetSt7305Mask(uint16_t px, uint16_t py) {
    uint16_t inv_y = PHYS_HEIGHT - 1 - py;
    return 1 << (7 - (((inv_y & 3) << 1) | (px & 1)));
}

static void SetPixel(uint8_t* buf, uint16_t x, uint16_t y, uint8_t color) {
    if (x >= RLCD_WIDTH || y >= RLCD_HEIGHT) return;
    // Clockwise 90 rotation: maps 300x400 to 400x300
    uint16_t phys_x = (PHYS_WIDTH - 1) - y;
    uint16_t phys_y = x;

    uint32_t idx = GetSt7305Idx(phys_x, phys_y);
    uint8_t mask = GetSt7305Mask(phys_x, phys_y);
    if (color) buf[idx] |= mask;
    else buf[idx] &= ~mask;
}

void LcdDriver::FlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* color_p) {
    auto* driver = static_cast<LcdDriver*>(lv_display_get_user_data(disp));
    uint16_t* buffer = reinterpret_cast<uint16_t*>(color_p);
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            uint8_t color = (*buffer < 0x7FFF) ? 0 : 1;
            SetPixel(driver->disp_buffer_, x, y, color);
            buffer++;
        }
    }
    driver->RLCD_Display();
    lv_disp_flush_ready(disp);
}

LcdDriver::LcdDriver() = default;
LcdDriver::~LcdDriver() = default;

bool LcdDriver::Init() {
    ESP_LOGI(TAG, "Initializing LcdDriver (Software Portrait 300x400, SPI3)");
    
    InitSpi();
    InitRstGpio();
    InitBuffers();
    InitLvgl();
    RLCD_InitSequence();

    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        return false;
    }

    ESP_LOGI(TAG, "LcdDriver initialization complete");
    return true;
}

void LcdDriver::Clear() {
    RLCD_ColorClear(0xFF);
    RLCD_Display();
}

void LcdDriver::DisplayRaw(const uint8_t* raw) {
    for (int y = 0; y < RLCD_HEIGHT; y++) {
        for (int x = 0; x < RLCD_WIDTH; x++) {
            int idx = (y * RLCD_WIDTH + x) / 8;
            int bit = 7 - (x % 8);
            uint8_t color = (raw[idx] >> bit) & 1;
            SetPixel(disp_buffer_, x, y, color);
        }
    }
    RLCD_Display();
}

void LcdDriver::InitSpi() {
    ESP_LOGI(TAG, "Initializing SPI bus (Host %d, SCK=%d, MOSI=%d)", 
             (int)LCD_SPI_HOST, (int)RLCD_SCK_PIN, (int)RLCD_MOSI_PIN);
             
    spi_bus_config_t buscfg = {};
    buscfg.miso_io_num     = -1;
    buscfg.mosi_io_num     = RLCD_MOSI_PIN;
    buscfg.sclk_io_num     = RLCD_SCK_PIN;
    buscfg.quadwp_io_num   = -1;
    buscfg.quadhd_io_num   = -1;
    buscfg.max_transfer_sz = PHYS_WIDTH * PHYS_HEIGHT;
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Creating LCD panel IO (CS=%d, DC=%d)", (int)RLCD_CS_PIN, (int)RLCD_DC_PIN);
    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.dc_gpio_num       = RLCD_DC_PIN;
    io_cfg.cs_gpio_num       = RLCD_CS_PIN;
    io_cfg.pclk_hz           = RLCD_SPI_CLOCK_HZ;
    io_cfg.lcd_cmd_bits      = 8;
    io_cfg.lcd_param_bits    = 8;
    io_cfg.spi_mode          = 0;
    io_cfg.trans_queue_depth = 10;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_cfg, &io_handle_));
}

void LcdDriver::InitRstGpio() {
    ESP_LOGI(TAG, "Configuring Reset GPIO (Pin %d)", (int)RLCD_RST_PIN);
    gpio_config_t gpio_conf = {};
    gpio_conf.mode = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask = (1ULL << RLCD_RST_PIN);
    ESP_ERROR_CHECK(gpio_config(&gpio_conf));
    gpio_set_level(RLCD_RST_PIN, 1);
}

void LcdDriver::WaitBusy() {} 

void LcdDriver::InitBuffers() {
    disp_buffer_len_ = (PHYS_WIDTH * PHYS_HEIGHT) >> 3;
    ESP_LOGI(TAG, "Allocating display buffer (%d bytes) in PSRAM", disp_buffer_len_);
    disp_buffer_ = static_cast<uint8_t*>(heap_caps_malloc(disp_buffer_len_, MALLOC_CAP_SPIRAM));
    assert(disp_buffer_ && "Failed to allocate display buffer in PSRAM");
    memset(disp_buffer_, 0xFF, disp_buffer_len_);
}

void LcdDriver::InitLvgl() {
    ESP_LOGI(TAG, "Initializing LVGL port");
    lv_init();
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority   = 2;
    port_cfg.timer_period_ms = 50;
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    lvgl_port_lock(0);
    display_ = lv_display_create(RLCD_WIDTH, RLCD_HEIGHT);
    lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display_, FlushCb);
    lv_display_set_user_data(display_, this);

    size_t buf_size = 2 * RLCD_WIDTH * RLCD_HEIGHT;
    ESP_LOGI(TAG, "Allocating LVGL RGB565 buffer (%d bytes) in PSRAM", (int)buf_size);
    auto* lvgl_buf = static_cast<uint8_t*>(heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM));
    assert(lvgl_buf && "Failed to allocate LVGL buffer in PSRAM");
    lv_display_set_buffers(display_, lvgl_buf, nullptr, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lvgl_port_unlock();
}

void LcdDriver::RLCD_SendCommand(uint8_t reg) {
    esp_lcd_panel_io_tx_param(io_handle_, reg, nullptr, 0);
}

void LcdDriver::RLCD_SendData(uint8_t data) {
    esp_lcd_panel_io_tx_param(io_handle_, -1, &data, 1);
}

void LcdDriver::RLCD_SendBuffer(const uint8_t* data, int len) {
    esp_lcd_panel_io_tx_color(io_handle_, -1, data, len);
}

void LcdDriver::RLCD_Reset() {
    gpio_set_level(RLCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(RLCD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(RLCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
}

void LcdDriver::RLCD_ColorClear(uint8_t color) {
    memset(disp_buffer_, color, disp_buffer_len_);
}

void LcdDriver::RLCD_Display() {
    RLCD_SendCommand(0x2A); RLCD_SendData(0x12); RLCD_SendData(0x2A);
    RLCD_SendCommand(0x2B); RLCD_SendData(0x00); RLCD_SendData(0xC7);
    RLCD_SendCommand(0x2C);
    RLCD_SendBuffer(disp_buffer_, disp_buffer_len_);
}

void LcdDriver::RLCD_InitSequence() {
    ESP_LOGI(TAG, "Sending ST7305 init sequence...");
    RLCD_Reset();
    RLCD_SendCommand(0xD6); RLCD_SendData(0x17); RLCD_SendData(0x02);
    RLCD_SendCommand(0xD1); RLCD_SendData(0x01);
    RLCD_SendCommand(0xC0); RLCD_SendData(0x11); RLCD_SendData(0x04);
    RLCD_SendCommand(0xC1); RLCD_SendData(0x69); RLCD_SendData(0x69); RLCD_SendData(0x69); RLCD_SendData(0x69);
    RLCD_SendCommand(0xC2); RLCD_SendData(0x19); RLCD_SendData(0x19); RLCD_SendData(0x19); RLCD_SendData(0x19);
    RLCD_SendCommand(0xC4); RLCD_SendData(0x4B); RLCD_SendData(0x4B); RLCD_SendData(0x4B); RLCD_SendData(0x4B);
    RLCD_SendCommand(0xC5); RLCD_SendData(0x19); RLCD_SendData(0x19); RLCD_SendData(0x19); RLCD_SendData(0x19);
    RLCD_SendCommand(0xD8); RLCD_SendData(0x80); RLCD_SendData(0xE9);
    RLCD_SendCommand(0xB2); RLCD_SendData(0x02);
    RLCD_SendCommand(0xB3); RLCD_SendData(0xE5); RLCD_SendData(0xF6); RLCD_SendData(0x05);
                       RLCD_SendData(0x46); RLCD_SendData(0x77); RLCD_SendData(0x77);
                       RLCD_SendData(0x77); RLCD_SendData(0x77); RLCD_SendData(0x76); RLCD_SendData(0x45);
    RLCD_SendCommand(0xB4); RLCD_SendData(0x05); RLCD_SendData(0x46); RLCD_SendData(0x77);
                       RLCD_SendData(0x77); RLCD_SendData(0x77); RLCD_SendData(0x77); RLCD_SendData(0x76); RLCD_SendData(0x45);
    RLCD_SendCommand(0x62); RLCD_SendData(0x32); RLCD_SendData(0x03); RLCD_SendData(0x1F);
    RLCD_SendCommand(0xB7); RLCD_SendData(0x13);
    RLCD_SendCommand(0xB0); RLCD_SendData(0x64);
    RLCD_SendCommand(0x11);
    vTaskDelay(pdMS_TO_TICKS(200));
    RLCD_SendCommand(0xC9); RLCD_SendData(0x00);
    RLCD_SendCommand(0x36); RLCD_SendData(0x48);
    RLCD_SendCommand(0x3A); RLCD_SendData(0x11);
    RLCD_SendCommand(0xB9); RLCD_SendData(0x20);
    RLCD_SendCommand(0xB8); RLCD_SendData(0x29);
    RLCD_SendCommand(0x21); 
    RLCD_SendCommand(0x2A); RLCD_SendData(0x12); RLCD_SendData(0x2A);
    RLCD_SendCommand(0x2B); RLCD_SendData(0x00); RLCD_SendData(0xC7);
    RLCD_SendCommand(0x35); RLCD_SendData(0x00);
    RLCD_SendCommand(0xD0); RLCD_SendData(0xFF);
    RLCD_SendCommand(0x38);
    RLCD_SendCommand(0x29);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    RLCD_ColorClear(0xFF); 
    RLCD_Display();
}
