#ifndef _BUTTON_HANDLER_H_
#define _BUTTON_HANDLER_H_

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include "config.h"

enum ButtonEvent : EventBits_t {
    kButtonEventNone     = 0,
    kButtonEventApprove  = (1 << 0),
    kButtonEventDeny     = (1 << 1),
};

class ButtonHandler {
public:
    ButtonHandler();
    ~ButtonHandler();

    void Init();
    EventBits_t Poll();

private:
    EventGroupHandle_t event_group_ = nullptr;
    static void OnApprovePressed(void* button_handle, void* usr_data);
    static void OnDenyPressed(void* button_handle, void* usr_data);
};

#endif // _BUTTON_HANDLER_H_
