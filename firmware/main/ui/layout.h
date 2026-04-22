#pragma once
#include "config.h"

// All layout constants for the 300x400 portrait RLCD.
// Tweak these to adjust UI positioning without touching buddy_display.cc.

namespace layout {

// Common
constexpr int MARGIN        = 4;
constexpr int SEP_HEIGHT    = 1;

// ── Dashboard ──
constexpr int TITLE_Y       = 2;
constexpr int MODEL_Y       = 2;
constexpr int PROJECT_Y     = 18;
constexpr int SESSIONS_Y    = 18;
constexpr int SEP1_Y        = 36;
constexpr int REPLY_TITLE_Y = 38;
constexpr int REPLY_Y       = 52;
constexpr int SEP2_Y        = 170;
constexpr int ACT_TITLE_Y   = 172;
constexpr int ACTIVITY_Y    = 186;
constexpr int ACTIVITY_H    = 186;
constexpr int FOOTER_SEP_Y  = DISP_HEIGHT - 28;
constexpr int FOOTER_Y      = DISP_HEIGHT - 24;
constexpr int CONTENT_W     = DISP_WIDTH - 8;

// ── Approval screen ──
constexpr int APPROVE_HEADER_Y    = 4;
constexpr int APPROVE_TOOL_Y      = 22;
constexpr int APPROVE_PROJECT_Y   = 46;
constexpr int APPROVE_SEP_Y       = 64;
constexpr int APPROVE_BODY_Y      = 68;
constexpr int APPROVE_BODY_H      = 280;
constexpr int APPROVE_BODY_PAD    = 2;
constexpr int APPROVE_BODY_W      = DISP_WIDTH - 16;
constexpr int APPROVE_WAIT_Y      = 354;
constexpr int APPROVE_HINTS_PAD   = 4;

// ── Passkey screen ──
constexpr int PASSKEY_TITLE_Y     = 40;
constexpr int PASSKEY_HINT_PAD    = 30;

// ── Splash screen ──
constexpr int SPLASH_TITLE_OFFSET = -40;
constexpr int SPLASH_SUB_OFFSET   = 20;
constexpr int SPLASH_NAME_OFFSET  = 40;

}  // namespace layout
