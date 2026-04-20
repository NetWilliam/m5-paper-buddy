// Stubs for ESP-IDF functions used by code under test.
#include <cstdint>
#include <cstring>
#include <cstdlib>

// ── FreeRTOS tick stub ──
extern "C" uint32_t xTaskGetTickCount() {
    static uint32_t tick = 0;
    return ++tick;
}

// ── Stats stubs ──
// Mirror the stats.h API without NVS
#include "state/stats.h"

static Stats _test_stats = {};
static char _test_petname[24] = "Buddy";
static char _test_ownername[32] = "";

void statsLoad() {}
void statsSave() {}
void statsOnApproval(uint32_t) { _test_stats.approvals++; }
void statsOnDenial() { _test_stats.denials++; }
const Stats& stats() { return _test_stats; }
void petNameLoad() {}
const char* petName() { return _test_petname; }
const char* ownerName() { return _test_ownername; }
void petNameSet(const char* n) { strncpy(_test_petname, n, sizeof(_test_petname) - 1); _test_petname[sizeof(_test_petname) - 1] = 0; }
void ownerSet(const char* n) { strncpy(_test_ownername, n, sizeof(_test_ownername) - 1); _test_ownername[sizeof(_test_ownername) - 1] = 0; }

// ── xfer_commands global ──
using SendLineFn = void(*)(const char* line, size_t len);
SendLineFn g_send_line = nullptr;
