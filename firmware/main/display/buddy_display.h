#ifndef _BUDDY_DISPLAY_H_
#define _BUDDY_DISPLAY_H_

#include "lvgl.h"
#include "state/tama_state.h"
#include <cstdint>

/// LVGL-based display for the 400x300 RLCD.
/// Renders dashboard and approval card screens.
class BuddyDisplay {
public:
    BuddyDisplay();
    ~BuddyDisplay();

    /// Initialize all LVGL screens. Call after LVGL is ready.
    void Init();

    /// Show dashboard with current state.
    void ShowDashboard(const TamaState& state);

    /// Show approval card.
    void ShowApproval(const TamaState& state, uint32_t waitedMs);

    /// Show splash screen.
    void ShowSplash(const char* deviceName);

private:
    void CreateScreens();
    void UpdateDashboardContent(const TamaState& state);
    void UpdateApprovalContent(const TamaState& state, uint32_t waitedMs);

    // Splash screen
    lv_obj_t* splash_screen_ = nullptr;
    lv_obj_t* splash_name_label_ = nullptr;

    // Dashboard screen
    lv_obj_t* dash_screen_ = nullptr;
    lv_obj_t* dash_title_ = nullptr;
    lv_obj_t* dash_project_ = nullptr;
    lv_obj_t* dash_sessions_ = nullptr;
    lv_obj_t* dash_model_ = nullptr;
    lv_obj_t* dash_reply_label_ = nullptr;
    lv_obj_t* dash_activity_label_ = nullptr;
    lv_obj_t* dash_buddy_label_ = nullptr;
    lv_obj_t* dash_status_label_ = nullptr;

    // Approval screen
    lv_obj_t* approve_screen_ = nullptr;
    lv_obj_t* approve_tool_label_ = nullptr;
    lv_obj_t* approve_project_label_ = nullptr;
    lv_obj_t* approve_body_label_ = nullptr;
    lv_obj_t* approve_wait_label_ = nullptr;
    lv_obj_t* approve_hints_label_ = nullptr;

    // Tracking
    char last_prompt_id_[40] = {};
    bool showing_approval_ = false;
};

#endif // _BUDDY_DISPLAY_H_
