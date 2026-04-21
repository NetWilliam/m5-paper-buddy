#include "buddy_display.h"
#include "config.h"
#include "buddy_frames.h"
#include "state/stats.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "font/binfont_loader/lv_binfont_loader.h"

static const char* TAG = "BuddyDisplay";

BuddyDisplay::BuddyDisplay() = default;
BuddyDisplay::~BuddyDisplay() = default;

void BuddyDisplay::Init() {
    LoadCjkFonts();
    CreateScreens();
    ESP_LOGI(TAG, "Display initialized (CJK: %s)", cjk_font_14_ ? "yes" : "no");
}

void BuddyDisplay::LoadCjkFonts() {
    // Custom extended CJK font compiled into firmware (1bpp, 20000+ chars)
    extern const lv_font_t lv_font_cjk_ext_14;
    cjk_font_14_ = const_cast<lv_font_t*>(&lv_font_cjk_ext_14);
    ESP_LOGI(TAG, "Using extended CJK 14px font");

    cjk_font_16_ = nullptr;
}

void BuddyDisplay::CreateScreens() {
    // Pick CJK-aware fonts: CJK primary with Montserrat fallback, or plain Montserrat
    const lv_font_t* font14 = cjk_font_14_ ? cjk_font_14_ : &lv_font_montserrat_14;
    const lv_font_t* font16 = cjk_font_16_ ? cjk_font_16_ : &lv_font_montserrat_16;

    lv_obj_t* default_screen = lv_screen_active();

    // Style: black bg, white text (1-bit RLCD)
    static lv_style_t screen_style;
    lv_style_init(&screen_style);
    lv_style_set_bg_color(&screen_style, lv_color_black());
    lv_style_set_bg_opa(&screen_style, LV_OPA_COVER);
    lv_style_set_text_color(&screen_style, lv_color_white());
    lv_style_set_border_width(&screen_style, 0);
    lv_style_set_pad_all(&screen_style, 0);

    // ── Splash screen ──
    splash_screen_ = lv_obj_create(default_screen);
    lv_obj_set_size(splash_screen_, RLCD_WIDTH, RLCD_HEIGHT);
    lv_obj_set_pos(splash_screen_, 0, 0);
    lv_obj_add_style(splash_screen_, &screen_style, 0);
    lv_obj_remove_flag(splash_screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(splash_screen_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* splash_title = lv_label_create(splash_screen_);
    lv_obj_set_style_text_font(splash_title, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(splash_title, lv_color_white(), 0);
    lv_obj_align(splash_title, LV_ALIGN_CENTER, 0, -40);
    lv_label_set_text(splash_title, "Paper Buddy");

    lv_obj_t* splash_sub = lv_label_create(splash_screen_);
    lv_obj_set_style_text_font(splash_sub, font14, 0);
    lv_obj_set_style_text_color(splash_sub, lv_color_white(), 0);
    lv_obj_align(splash_sub, LV_ALIGN_CENTER, 0, 20);
    lv_label_set_text(splash_sub, "ESP32-S3-RLCD-4.2");

    splash_name_label_ = lv_label_create(splash_screen_);
    lv_obj_set_style_text_font(splash_name_label_, font14, 0);
    lv_obj_set_style_text_color(splash_name_label_, lv_color_white(), 0);
    lv_obj_align(splash_name_label_, LV_ALIGN_CENTER, 0, 40);
    lv_label_set_text(splash_name_label_, "");

    // ── Dashboard screen ──
    dash_screen_ = lv_obj_create(default_screen);
    lv_obj_set_size(dash_screen_, RLCD_WIDTH, RLCD_HEIGHT);
    lv_obj_set_pos(dash_screen_, 0, 0);
    lv_obj_add_style(dash_screen_, &screen_style, 0);
    lv_obj_remove_flag(dash_screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(dash_screen_, LV_OBJ_FLAG_HIDDEN);

    // Title bar
    dash_title_ = lv_label_create(dash_screen_);
    lv_obj_set_style_text_font(dash_title_, font16, 0);
    lv_obj_set_style_text_color(dash_title_, lv_color_white(), 0);
    lv_obj_align(dash_title_, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_label_set_text(dash_title_, "Paper Buddy");

    // Model (top-right)
    dash_model_ = lv_label_create(dash_screen_);
    lv_obj_set_style_text_font(dash_model_, font14, 0);
    lv_obj_set_style_text_color(dash_model_, lv_color_white(), 0);
    lv_obj_align(dash_model_, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_label_set_text(dash_model_, "");

    // Project + sessions line
    dash_project_ = lv_label_create(dash_screen_);
    lv_obj_set_style_text_font(dash_project_, font14, 0);
    lv_obj_set_style_text_color(dash_project_, lv_color_white(), 0);
    lv_obj_set_pos(dash_project_, 4, 24);
    lv_label_set_text(dash_project_, "");

    dash_sessions_ = lv_label_create(dash_screen_);
    lv_obj_set_style_text_font(dash_sessions_, font14, 0);
    lv_obj_set_style_text_color(dash_sessions_, lv_color_white(), 0);
    lv_obj_align(dash_sessions_, LV_ALIGN_TOP_RIGHT, -4, 24);
    lv_label_set_text(dash_sessions_, "");

    // Separator line
    lv_obj_t* sep1 = lv_obj_create(dash_screen_);
    lv_obj_set_size(sep1, RLCD_WIDTH - 8, 1);
    lv_obj_set_pos(sep1, 4, 44);
    lv_obj_set_style_bg_color(sep1, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(sep1, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep1, 0, 0);
    lv_obj_set_style_pad_all(sep1, 0, 0);

    // Latest reply label
    lv_obj_t* reply_title = lv_label_create(dash_screen_);
    lv_obj_set_style_text_font(reply_title, font14, 0);
    lv_obj_set_style_text_color(reply_title, lv_color_white(), 0);
    lv_obj_set_pos(reply_title, 4, 48);
    lv_label_set_text(reply_title, "LATEST REPLY");

    dash_reply_label_ = lv_label_create(dash_screen_);
    lv_obj_set_style_text_font(dash_reply_label_, font14, 0);
    lv_obj_set_style_text_color(dash_reply_label_, lv_color_white(), 0);
    lv_obj_set_pos(dash_reply_label_, 4, 64);
    lv_label_set_long_mode(dash_reply_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(dash_reply_label_, RLCD_WIDTH - 8);
    lv_label_set_text(dash_reply_label_, "");

    // Activity section
    lv_obj_t* sep2 = lv_obj_create(dash_screen_);
    lv_obj_set_size(sep2, RLCD_WIDTH - 8, 1);
    lv_obj_set_pos(sep2, 4, 140);
    lv_obj_set_style_bg_color(sep2, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(sep2, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep2, 0, 0);
    lv_obj_set_style_pad_all(sep2, 0, 0);

    lv_obj_t* act_title = lv_label_create(dash_screen_);
    lv_obj_set_style_text_font(act_title, font14, 0);
    lv_obj_set_style_text_color(act_title, lv_color_white(), 0);
    lv_obj_set_pos(act_title, 4, 144);
    lv_label_set_text(act_title, "ACTIVITY");

    dash_activity_label_ = lv_label_create(dash_screen_);
    lv_obj_set_style_text_font(dash_activity_label_, font14, 0);
    lv_obj_set_style_text_color(dash_activity_label_, lv_color_white(), 0);
    lv_obj_set_pos(dash_activity_label_, 4, 160);
    lv_label_set_long_mode(dash_activity_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(dash_activity_label_, RLCD_WIDTH - 8);
    lv_label_set_text(dash_activity_label_, "");

    // Footer: buddy + status
    lv_obj_t* sep3 = lv_obj_create(dash_screen_);
    lv_obj_set_size(sep3, RLCD_WIDTH - 8, 1);
    lv_obj_set_pos(sep3, 4, RLCD_HEIGHT - 44);
    lv_obj_set_style_bg_color(sep3, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(sep3, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep3, 0, 0);
    lv_obj_set_style_pad_all(sep3, 0, 0);

    dash_buddy_label_ = lv_label_create(dash_screen_);
    lv_obj_set_style_text_font(dash_buddy_label_, font14, 0);
    lv_obj_set_style_text_color(dash_buddy_label_, lv_color_white(), 0);
    lv_obj_set_pos(dash_buddy_label_, 4, RLCD_HEIGHT - 40);
    lv_label_set_text(dash_buddy_label_, "");

    dash_status_label_ = lv_label_create(dash_screen_);
    lv_obj_set_style_text_font(dash_status_label_, font14, 0);
    lv_obj_set_style_text_color(dash_status_label_, lv_color_white(), 0);
    lv_obj_align(dash_status_label_, LV_ALIGN_TOP_RIGHT, -4, RLCD_HEIGHT - 40);
    lv_label_set_text(dash_status_label_, "KEY=yes BOOT=no");

    // ── Approval screen (inverted: white bg, black text) ──
    static lv_style_t approve_style;
    lv_style_init(&approve_style);
    lv_style_set_bg_color(&approve_style, lv_color_white());
    lv_style_set_bg_opa(&approve_style, LV_OPA_COVER);
    lv_style_set_text_color(&approve_style, lv_color_black());
    lv_style_set_border_width(&approve_style, 0);
    lv_style_set_pad_all(&approve_style, 0);

    approve_screen_ = lv_obj_create(default_screen);
    lv_obj_set_size(approve_screen_, RLCD_WIDTH, RLCD_HEIGHT);
    lv_obj_set_pos(approve_screen_, 0, 0);
    lv_obj_add_style(approve_screen_, &approve_style, 0);
    lv_obj_remove_flag(approve_screen_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(approve_screen_, LV_OBJ_FLAG_HIDDEN);

    // "PERMISSION REQUESTED" header
    lv_obj_t* approve_header = lv_label_create(approve_screen_);
    lv_obj_set_style_text_font(approve_header, font14, 0);
    lv_obj_set_style_text_color(approve_header, lv_color_black(), 0);
    lv_obj_align(approve_header, LV_ALIGN_TOP_MID, 0, 4);
    lv_label_set_text(approve_header, "PERMISSION REQUESTED");

    // Tool name
    approve_tool_label_ = lv_label_create(approve_screen_);
    lv_obj_set_style_text_font(approve_tool_label_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(approve_tool_label_, lv_color_black(), 0);
    lv_obj_align(approve_tool_label_, LV_ALIGN_TOP_MID, 0, 22);
    lv_label_set_text(approve_tool_label_, "");

    // Project/session origin
    approve_project_label_ = lv_label_create(approve_screen_);
    lv_obj_set_style_text_font(approve_project_label_, font14, 0);
    lv_obj_set_style_text_color(approve_project_label_, lv_color_black(), 0);
    lv_obj_align(approve_project_label_, LV_ALIGN_TOP_MID, 0, 46);
    lv_label_set_text(approve_project_label_, "");

    // Separator
    lv_obj_t* approve_sep = lv_obj_create(approve_screen_);
    lv_obj_set_size(approve_sep, RLCD_WIDTH - 8, 1);
    lv_obj_set_pos(approve_sep, 4, 64);
    lv_obj_set_style_bg_color(approve_sep, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(approve_sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(approve_sep, 0, 0);
    lv_obj_set_style_pad_all(approve_sep, 0, 0);

    // Body (scrollable)
    lv_obj_t* body_container = lv_obj_create(approve_screen_);
    lv_obj_set_size(body_container, RLCD_WIDTH - 8, 180);
    lv_obj_set_pos(body_container, 4, 68);
    lv_obj_set_style_bg_color(body_container, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(body_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(body_container, 0, 0);
    lv_obj_set_style_pad_all(body_container, 2, 0);
    lv_obj_set_scroll_dir(body_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body_container, LV_SCROLLBAR_MODE_AUTO);

    approve_body_label_ = lv_label_create(body_container);
    lv_obj_set_style_text_font(approve_body_label_, font14, 0);
    lv_obj_set_style_text_color(approve_body_label_, lv_color_black(), 0);
    lv_label_set_long_mode(approve_body_label_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(approve_body_label_, RLCD_WIDTH - 16);
    lv_label_set_text(approve_body_label_, "");

    // Bottom section: wait counter + button hints
    approve_wait_label_ = lv_label_create(approve_screen_);
    lv_obj_set_style_text_font(approve_wait_label_, font14, 0);
    lv_obj_set_style_text_color(approve_wait_label_, lv_color_black(), 0);
    lv_obj_set_pos(approve_wait_label_, 4, 254);
    lv_label_set_text(approve_wait_label_, "");

    approve_hints_label_ = lv_label_create(approve_screen_);
    lv_obj_set_style_text_font(approve_hints_label_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(approve_hints_label_, lv_color_black(), 0);
    lv_obj_align(approve_hints_label_, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_label_set_text(approve_hints_label_, "KEY=approve  BOOT=deny");
}

void BuddyDisplay::ShowSplash(const char* deviceName) {
    // Update splash name label
    lv_label_set_text(splash_name_label_, deviceName ? deviceName : "");

    // Hide other screens, show splash
    lv_obj_add_flag(dash_screen_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(approve_screen_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(splash_screen_, LV_OBJ_FLAG_HIDDEN);
    showing_approval_ = false;
}

void BuddyDisplay::ShowDashboard(const TamaState& state) {
    if (showing_approval_) {
        lv_obj_add_flag(approve_screen_, LV_OBJ_FLAG_HIDDEN);
        showing_approval_ = false;
    }
    lv_obj_add_flag(splash_screen_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(dash_screen_, LV_OBJ_FLAG_HIDDEN);
    UpdateDashboardContent(state);
}

void BuddyDisplay::ShowApproval(const TamaState& state, uint32_t waitedMs) {
    if (!showing_approval_) {
        lv_obj_add_flag(dash_screen_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(splash_screen_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(approve_screen_, LV_OBJ_FLAG_HIDDEN);
        showing_approval_ = true;
    }
    UpdateApprovalContent(state, waitedMs);
}

static const BuddyFrame& PickBuddyFrame(const TamaState& state) {
    if (!state.connected)                    return buddy_cat::SLEEP;
    if (state.sessionsWaiting > 0)           return buddy_cat::ATTENTION;
    if (state.HasPrompt())                   return buddy_cat::ATTENTION;
    if (state.sessionsRunning > 0)           return buddy_cat::BUSY;
    return buddy_cat::IDLE;
}

void BuddyDisplay::UpdateDashboardContent(const TamaState& state) {
    // Title
    char title[64];
    if (ownerName()[0]) snprintf(title, sizeof(title), "%s's %s", ownerName(), petName());
    else                 snprintf(title, sizeof(title), "Paper Buddy");
    lv_label_set_text(dash_title_, title);

    // Model
    lv_label_set_text(dash_model_, state.modelName[0] ? state.modelName : "");

    // Project
    char proj[160];
    if (state.project[0]) {
        if (state.branch[0]) {
            if (state.dirty > 0)
                snprintf(proj, sizeof(proj), "%s  %s *%u", state.project, state.branch, state.dirty);
            else
                snprintf(proj, sizeof(proj), "%s  %s", state.project, state.branch);
        } else {
            snprintf(proj, sizeof(proj), "%s", state.project);
        }
    } else {
        snprintf(proj, sizeof(proj), "-");
    }
    lv_label_set_text(dash_project_, proj);

    // Sessions
    char sess[32];
    if (!state.connected)       snprintf(sess, sizeof(sess), "offline");
    else if (state.sessionsTotal == 0) snprintf(sess, sizeof(sess), "idle");
    else snprintf(sess, sizeof(sess), "%u run %u wait", state.sessionsRunning, state.sessionsWaiting);
    lv_label_set_text(dash_sessions_, sess);

    // Latest reply
    if (state.assistantMsg[0]) {
        lv_label_set_text(dash_reply_label_, state.assistantMsg);
    } else {
        lv_label_set_text(dash_reply_label_, "(nothing yet)");
    }

    // Activity
    if (state.nLines > 0) {
        char act[256];
        int offset = 0;
        for (uint8_t i = 0; i < state.nLines && offset < (int)sizeof(act) - 30; i++) {
            offset += snprintf(act + offset, sizeof(act) - offset,
                               "%s\n", state.lines[i]);
        }
        lv_label_set_text(dash_activity_label_, act);
    } else {
        lv_label_set_text(dash_activity_label_, "-");
    }

    // Buddy face
    const BuddyFrame& f = PickBuddyFrame(state);
    char buddy[80];
    int off = 0;
    for (int i = 0; i < 5; i++) {
        off += snprintf(buddy + off, sizeof(buddy) - off, "%s\n", f.lines[i]);
    }
    lv_label_set_text(dash_buddy_label_, buddy);

    // Status bar
    const char* linkStr = state.connected ? "LINKED" : "USB/BLE";
    lv_label_set_text(dash_status_label_, linkStr);
}

void BuddyDisplay::UpdateApprovalContent(const TamaState& state, uint32_t waitedMs) {
    lv_label_set_text(approve_tool_label_, state.promptTool[0] ? state.promptTool : "(tool)");

    char who[64];
    if (state.promptProject[0])
        snprintf(who, sizeof(who), "%s [%s]", state.promptProject, state.promptSid);
    else
        who[0] = 0;
    lv_label_set_text(approve_project_label_, who);

    const char* body = state.promptBody[0] ? state.promptBody : state.promptHint;
    lv_label_set_text(approve_body_label_, body[0] ? body : "");

    char wait[32];
    snprintf(wait, sizeof(wait), "waiting %lus", (unsigned long)(waitedMs / 1000));
    lv_label_set_text(approve_wait_label_, wait);
}
