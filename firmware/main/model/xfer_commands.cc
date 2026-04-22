#include "xfer_commands.h"
#include "stats.h"
#include <cstdio>
#include <cstring>

void XferCommands::Init(SendLine send) {
    send_ = send;
}

bool XferCommands::TryHandle(cJSON* root) {
    if (!send_) return false;

    cJSON* cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    if (!cmd || !cJSON_IsString(cmd)) return false;

    const char* cmdStr = cmd->valuestring;

    if (strcmp(cmdStr, "name") == 0) {
        cJSON* n = cJSON_GetObjectItemCaseSensitive(root, "name");
        if (n && cJSON_IsString(n)) petNameSet(n->valuestring);
        char ack[64];
        int len = snprintf(ack, sizeof(ack),
            "{\"ack\":\"name\",\"ok\":%s}\n", (n ? "true" : "false"));
        send_(ack, len);
        return true;
    }

    if (strcmp(cmdStr, "owner") == 0) {
        cJSON* n = cJSON_GetObjectItemCaseSensitive(root, "name");
        if (n && cJSON_IsString(n)) ownerSet(n->valuestring);
        char ack[64];
        int len = snprintf(ack, sizeof(ack),
            "{\"ack\":\"owner\",\"ok\":%s}\n", (n ? "true" : "false"));
        send_(ack, len);
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
        send_(ack, len);
        return true;
    }

    return false;
}
