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

// Pixel address computation for ST7305 in landscape mode (400x300 physical).
// Each byte holds 8 pixels in a 2-wide x 4-tall block:
//   byte index = (x/2) * (H/4) + ((H-1-y)/4)
//   bit position = 7 - (((H-1-y)%4)*2 + x%2)
// Total: (400/2) * (300/4) = 15000 bytes.
// LVGL sees 300x400 (DISP_WIDTH x DISP_HEIGHT) with 90° CW rotation applied in FlushCb.

static inline uint32_t PixelIndex(uint16_t x, uint16_t y) {
    uint16_t inv_y = RLCD_HEIGHT - 1 - y;
    return (x >> 1) * (RLCD_HEIGHT >> 2) + (inv_y >> 2);
}

static inline uint8_t PixelMask(uint16_t x, uint16_t y) {
    uint16_t inv_y = RLCD_HEIGHT - 1 - y;
    return 1 << (7 - (((inv_y & 3) << 1) | (x & 1)));
}

static void SetPixel(uint8_t* buf, uint16_t x, uint16_t y, uint8_t color) {
    uint32_t idx = PixelIndex(x, y);
    uint8_t mask = PixelMask(x, y);
    if (color) {
        buf[idx] |= mask;
    } else {
        buf[idx] &= ~mask;
    }
}

// FlushCb: convert RGB565 to 1-bit, copy to frame buffer, send via SPI.
// Matches the official Waveshare driver exactly.
void LcdDriver::FlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* color_p) {
    auto* driver = static_cast<LcdDriver*>(lv_display_get_user_data(disp));

    uint16_t* buffer = reinterpret_cast<uint16_t*>(color_p);
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            // RGB565 threshold: below 0x7FFF = black, else white
            uint8_t color = (*buffer < 0x7FFF) ? 0 : 1;
            // 90° CW rotation: logical (x,y) → physical (399-y, x)
            SetPixel(driver->disp_buffer_, RLCD_WIDTH - 1 - y, x, color);
            buffer++;
        }
    }

    driver->RLCD_Display();
    lv_disp_flush_ready(disp);
}

LcdDriver::LcdDriver() = default;
LcdDriver::~LcdDriver() = default;

bool LcdDriver::Init() {
    ESP_LOGI(TAG, "Initializing LcdDriver (RLCD 400x300, ST7305)");

    InitSpi();
    InitRstGpio();
    InitBuffers();
    InitLvgl();
    RLCD_InitSequence();

    if (display_ == nullptr) {
        ESP_LOGE(TAG, "LVGL display creation failed");
        return false;
    }

    ESP_LOGI(TAG, "LcdDriver ready");
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
    ESP_LOGI(TAG, "Initializing SPI bus");
    spi_bus_config_t buscfg = {};
    buscfg.miso_io_num     = -1;
    buscfg.mosi_io_num     = RLCD_MOSI_PIN;
    buscfg.sclk_io_num     = RLCD_SCK_PIN;
    buscfg.quadwp_io_num   = -1;
    buscfg.quadhd_io_num   = -1;
    buscfg.max_transfer_sz = RLCD_WIDTH * RLCD_HEIGHT;

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {};
    io_cfg.dc_gpio_num       = RLCD_DC_PIN;
    io_cfg.cs_gpio_num       = RLCD_CS_PIN;
    io_cfg.pclk_hz           = RLCD_SPI_CLOCK_HZ;
    io_cfg.lcd_cmd_bits      = 8;
    io_cfg.lcd_param_bits    = 8;
    io_cfg.spi_mode          = 0;
    io_cfg.trans_queue_depth = 7;

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        static_cast<esp_lcd_spi_bus_handle_t>(LCD_SPI_HOST),
        &io_cfg, &io_handle_));
}

void LcdDriver::InitRstGpio() {
    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type     = GPIO_INTR_DISABLE;
    gpio_conf.mode          = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask  = (1ULL << RLCD_RST_PIN);
    gpio_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en    = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
    gpio_set_level(RLCD_RST_PIN, 1);
}

void LcdDriver::InitBuffers() {
    disp_buffer_len_ = (RLCD_WIDTH * RLCD_HEIGHT) >> 3;
    disp_buffer_ = static_cast<uint8_t*>(heap_caps_malloc(disp_buffer_len_, MALLOC_CAP_SPIRAM));
    assert(disp_buffer_ && "Failed to allocate display buffer");
}

void LcdDriver::InitLvgl() {
    ESP_LOGI(TAG, "Initializing LVGL");
    lv_init();

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority   = 2;
    port_cfg.timer_period_ms = 50;
    esp_err_t err = lvgl_port_init(&port_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "lvgl_port_init OK");
    lvgl_port_lock(0);

    display_ = lv_display_create(DISP_WIDTH, DISP_HEIGHT);
    lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display_, FlushCb);
    lv_display_set_user_data(display_, this);

    // RGB565 buffer: 2 bytes per pixel
    size_t buf_size = LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB565) * DISP_WIDTH * DISP_HEIGHT;
    auto* lvgl_buf = static_cast<uint8_t*>(heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM));
    assert(lvgl_buf && "Failed to allocate LVGL buffer");
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
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(RLCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void LcdDriver::RLCD_ColorClear(uint8_t color) {
    memset(disp_buffer_, color, disp_buffer_len_);
}

void LcdDriver::RLCD_Display() {
    RLCD_SendCommand(0x2A);
    RLCD_SendData(0x12);
    RLCD_SendData(0x2A);
    RLCD_SendCommand(0x2B);
    RLCD_SendData(0x00);
    RLCD_SendData(0xC7);
    RLCD_SendCommand(0x2C);
    RLCD_SendBuffer(disp_buffer_, disp_buffer_len_);
}

void LcdDriver::RLCD_InitSequence() {
    RLCD_Reset();

    // ST7305 init sequence (matches Waveshare official driver)
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

    RLCD_ColorClear(0xFF);
}
