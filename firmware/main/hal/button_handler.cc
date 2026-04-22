#include "button_handler.h"
#include <iot_button.h>
#include <button_gpio.h>
#include <esp_log.h>

static const char* TAG = "ButtonHandler";

ButtonHandler::ButtonHandler() = default;

ButtonHandler::~ButtonHandler() = default;

void ButtonHandler::Init() {
    event_group_ = xEventGroupCreate();
    assert(event_group_ && "Failed to create button event group");

    button_config_t approve_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 0,
        .short_press_time = BUTTON_DEBOUNCE_MS,
        .gpio_button_config = {
            .gpio_num = APPROVE_BUTTON_GPIO,
            .active_level = 0,
            .disable_pull = false,
        },
    };
    button_handle_t approve_btn = iot_button_create(&approve_cfg);
    assert(approve_btn && "Failed to create approve button");
    iot_button_register_cb(approve_btn, BUTTON_SINGLE_CLICK,
                           OnApprovePressed, event_group_);

    button_config_t deny_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 0,
        .short_press_time = BUTTON_DEBOUNCE_MS,
        .gpio_button_config = {
            .gpio_num = DENY_BUTTON_GPIO,
            .active_level = 0,
            .disable_pull = false,
        },
    };
    button_handle_t deny_btn = iot_button_create(&deny_cfg);
    assert(deny_btn && "Failed to create deny button");
    iot_button_register_cb(deny_btn, BUTTON_SINGLE_CLICK,
                           OnDenyPressed, event_group_);

    ESP_LOGI(TAG, "Buttons initialized: Approve=GPIO%d, Deny=GPIO%d",
             APPROVE_BUTTON_GPIO, DENY_BUTTON_GPIO);
}

EventBits_t ButtonHandler::Poll() {
    if (!event_group_) return kButtonEventNone;
    return xEventGroupClearBits(event_group_,
                                kButtonEventApprove | kButtonEventDeny);
}

void ButtonHandler::OnApprovePressed(void* button_handle, void* usr_data) {
    auto* eg = static_cast<EventGroupHandle_t>(usr_data);
    xEventGroupSetBits(eg, kButtonEventApprove);
}

void ButtonHandler::OnDenyPressed(void* button_handle, void* usr_data) {
    auto* eg = static_cast<EventGroupHandle_t>(usr_data);
    xEventGroupSetBits(eg, kButtonEventDeny);
}
