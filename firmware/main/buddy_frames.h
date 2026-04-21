// Single-line ASCII buddy faces — one per emotional state.
// Used in the dashboard footer to show device mood at a glance.

#pragma once

struct BuddyFrame {
    const char* face;
};

namespace buddy_cat {
  static const BuddyFrame SLEEP     = { "(-.-) zzz" };
  static const BuddyFrame IDLE      = { "(o.o)" };
  static const BuddyFrame BUSY      = { "(o.o)/" };
  static const BuddyFrame ATTENTION = { "(O.O)!" };
  static const BuddyFrame CELEBRATE = { "\\(^o^)/" };
  static const BuddyFrame DND       = { "(-.-)" };
}
