#ifndef _TAMA_STATE_H_
#define _TAMA_STATE_H_

#include <cstdint>
#include <cstddef>

struct TamaState {
    uint8_t  sessionsTotal = 0;
    uint8_t  sessionsRunning = 0;
    uint8_t  sessionsWaiting = 0;
    bool     recentlyCompleted = false;
    uint32_t tokensToday = 0;
    uint32_t lastUpdated = 0;
    char     msg[24] = {};
    bool     connected = false;
    char     lines[8][92] = {};
    uint8_t  nLines = 0;
    uint16_t lineGen = 0;
    char     promptId[40] = {};
    char     promptTool[20] = {};
    char     promptHint[44] = {};
    char     promptKind[16] = {};
    char     promptBody[512] = {};
    char     promptProject[24] = {};
    char     promptSid[10] = {};

    char     project[40] = {};
    char     branch[40] = {};
    uint16_t dirty = 0;
    uint32_t budgetLimit = 0;
    char     modelName[32] = {};
    char     assistantMsg[240] = {};
    uint16_t assistantGen = 0;

    char     ownerName[24] = {};
    char     petName[24] = {};

    void ClearPrompt() {
        promptId[0] = 0;
        promptTool[0] = 0;
        promptHint[0] = 0;
        promptBody[0] = 0;
        promptKind[0] = 0;
        promptProject[0] = 0;
        promptSid[0] = 0;
    }

    bool HasPrompt() const { return promptId[0] != 0; }
};

#endif // _TAMA_STATE_H_
