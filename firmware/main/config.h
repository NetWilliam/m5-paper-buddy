#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <driver/gpio.h>

// ── SPI LCD (RLCD 4.2") ──────────────────────────────────────────────
#define LCD_SPI_HOST       SPI3_HOST
#define RLCD_DC_PIN        GPIO_NUM_5
#define RLCD_CS_PIN        GPIO_NUM_40
#define RLCD_SCK_PIN       GPIO_NUM_11
#define RLCD_MOSI_PIN      GPIO_NUM_12
#define RLCD_RST_PIN       GPIO_NUM_41
#define RLCD_TE_PIN        GPIO_NUM_6

#define RLCD_WIDTH         400
#define RLCD_HEIGHT        300
#define DISP_WIDTH         300   // logical (90° CW rotation)
#define DISP_HEIGHT        400
#define RLCD_SPI_CLOCK_HZ  (40 * 1000 * 1000)

// ── Buttons ───────────────────────────────────────────────────────────
// NOTE: GPIO0 (BOOT) is the ESP32-S3 strapping pin. Holding it low during
// reset enters download mode. Users must release the BOOT button before
// powering on or resetting the device.
#define APPROVE_BUTTON_GPIO    GPIO_NUM_0   // BOOT — Approve
#define DENY_BUTTON_GPIO       GPIO_NUM_18  // KEY — Deny
#define BUTTON_DEBOUNCE_MS     200

// ── Display UI ────────────────────────────────────────────────────────
#define STATUS_BAR_HEIGHT      20
#define ACTION_BAR_HEIGHT      24

#endif // _CONFIG_H_
