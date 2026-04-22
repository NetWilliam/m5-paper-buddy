#ifndef _LCD_DRIVER_H_
#define _LCD_DRIVER_H_

#include <driver/gpio.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include "lvgl.h"

/// Standalone RLCD 4.2" (400x300, 1-bit) SPI driver with LVGL integration.
class LcdDriver {
public:
    LcdDriver();
    ~LcdDriver();

    bool Init();
    void Clear();
    lv_display_t* GetDisplay() const { return display_; }

    /// Display a linear 1-bit raw image (MSB first, row-major, 400x300).
    /// Converts from linear format to ST7305 block format and sends via SPI.
    void DisplayRaw(const uint8_t* raw);

private:
    void InitSpi();
    void InitRstGpio();
    void InitBuffers();
    void InitLvgl();

    void RLCD_Reset();
    void WaitBusy();
    void RLCD_SendCommand(uint8_t reg);
    void RLCD_SendData(uint8_t data);
    void RLCD_SendBuffer(const uint8_t* data, int len);
    void RLCD_InitSequence();
    void RLCD_ColorClear(uint8_t color);
    void RLCD_Display();

    static void FlushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* color_p);

    static constexpr const char* TAG = "LcdDriver";

    esp_lcd_panel_io_handle_t io_handle_ = nullptr;
    lv_display_t* display_ = nullptr;

    // 1-bit frame buffer (400 * 300 / 8 = 15000 bytes)
    uint8_t* disp_buffer_ = nullptr;
    int disp_buffer_len_ = 0;
};

#endif // _LCD_DRIVER_H_
