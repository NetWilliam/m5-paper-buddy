#ifndef _XFER_COMMANDS_H_
#define _XFER_COMMANDS_H_

#include "cJSON.h"
#include "stats.h"
#include <cstdio>
#include <cstring>
#include <esp_log.h>

// Forward-declared: implemented by the transport layer to send a line.
// Must be set during initialization.
using SendLineFn = void(*)(const char* line, size_t len);
extern SendLineFn g_send_line;

inline bool TryXferCommand(cJSON* root) {
    if (!g_send_line) return false;

    cJSON* cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!cmd || !cJSON_IsString(cmd)) return false;

    const char* cmdStr = cmd->valuestring;

    if (strcmp(cmdStr, "name") == 0) {
        cJSON* n = cJSON_GetObjectItemCaseSensitive(root, "name");
        if (n && cJSON_IsString(n)) petNameSet(n->valuestring);
        char ack[64];
        int len = snprintf(ack, sizeof(ack),
            "{\"ack\":\"name\",\"ok\":%s}\n", (n ? "true" : "false"));
        g_send_line(ack, len);
        return true;
    }

    if (strcmp(cmdStr, "owner") == 0) {
        cJSON* n = cJSON_GetObjectItemCaseSensitive(root, "name");
        if (n && cJSON_IsString(n)) ownerSet(n->valuestring);
        char ack[64];
        int len = snprintf(ack, sizeof(ack),
            "{\"ack\":\"owner\",\"ok\":%s}\n", (n ? "true" : "false"));
        g_send_line(ack, len);
        return true;
    }

    if (strcmp(cmdStr, "status") == 0) {
        char ack[256];
        int len = snprintf(ack, sizeof(ack),
            "{\"ack\":\"status\",\"ok\":true,\"data\":{"
            "\"name\":\"%s\",\"owner\":\"%s\","
            "\"stats\":{\"appr\":%u,\"deny\":%u,\"lvl\":%u}"
            "}}\n",
            petName(), ownerName(),
            (unsigned)stats().approvals, (unsigned)stats().denials,
            (unsigned)stats().level);
        g_send_line(ack, len);
        return true;
    }

    return false;
}

#endif // _XFER_COMMANDS_H_
