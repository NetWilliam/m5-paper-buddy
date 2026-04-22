#ifndef _STATS_H_
#define _STATS_H_

#include <cstdint>

static const uint32_t TOKENS_PER_LEVEL = 50000;

struct Stats {
    uint16_t approvals = 0;
    uint16_t denials = 0;
    uint8_t  level = 0;
    uint32_t tokens = 0;
};

void statsLoad();
void statsSave();
void statsOnApproval(uint32_t secondsToRespond);
void statsOnDenial();
const Stats& stats();

void petNameLoad();
const char* petName();
const char* ownerName();
void petNameSet(const char* name);
void ownerSet(const char* name);

#endif // _STATS_H_
