#include "tama_state.h"
#include "state_parser.h"
#include "cJSON.h"
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

static const char* TAG = "StateParser";

static void safeStrCopy(char* dst, const char* src, size_t maxLen) {
    if (!src) { dst[0] = 0; return; }
    strncpy(dst, src, maxLen - 1);
    dst[maxLen - 1] = 0;
}

void ApplyJson(cJSON* root, TamaState* out) {
    if (!root) return;

    // Time sync
    cJSON* timeArr = cJSON_GetObjectItemCaseSensitive(root, "time");
    if (cJSON_IsArray(timeArr) && cJSON_GetArraySize(timeArr) == 2) {
        // Time sync received — just mark connected
        out->lastUpdated = xTaskGetTickCount() * portTICK_PERIOD_MS;
        return;
    }

    // Session counts
    cJSON* val;
    if ((val = cJSON_GetObjectItemCaseSensitive(root, "total")))
        out->sessionsTotal = val->valueint;
    if ((val = cJSON_GetObjectItemCaseSensitive(root, "running")))
        out->sessionsRunning = val->valueint;
    if ((val = cJSON_GetObjectItemCaseSensitive(root, "waiting")))
        out->sessionsWaiting = val->valueint;

    // Message
    if ((val = cJSON_GetObjectItemCaseSensitive(root, "msg")) && cJSON_IsString(val)) {
        safeStrCopy(out->msg, val->valuestring, sizeof(out->msg));
    }

    // Activity entries
    cJSON* entries = cJSON_GetObjectItemCaseSensitive(root, "entries");
    if (cJSON_IsArray(entries)) {
        uint8_t n = 0;
        cJSON* item;
        cJSON_ArrayForEach(item, entries) {
            if (n >= 8) break;
            if (cJSON_IsString(item)) {
                safeStrCopy(out->lines[n], item->valuestring, 92);
                n++;
            }
        }
        if (n != out->nLines) out->lineGen++;
        out->nLines = n;
    }

    // Tokens
    if ((val = cJSON_GetObjectItemCaseSensitive(root, "tokens_today")) && cJSON_IsNumber(val)) {
        out->tokensToday = (uint32_t)val->valuedouble;
    }

    // Prompt
    cJSON* prompt = cJSON_GetObjectItemCaseSensitive(root, "prompt");
    if (cJSON_IsObject(prompt)) {
        if ((val = cJSON_GetObjectItemCaseSensitive(prompt, "id")) && cJSON_IsString(val))
            safeStrCopy(out->promptId, val->valuestring, sizeof(out->promptId));
        if ((val = cJSON_GetObjectItemCaseSensitive(prompt, "tool")) && cJSON_IsString(val))
            safeStrCopy(out->promptTool, val->valuestring, sizeof(out->promptTool));
        if ((val = cJSON_GetObjectItemCaseSensitive(prompt, "hint")) && cJSON_IsString(val))
            safeStrCopy(out->promptHint, val->valuestring, sizeof(out->promptHint));
        if ((val = cJSON_GetObjectItemCaseSensitive(prompt, "body")) && cJSON_IsString(val))
            safeStrCopy(out->promptBody, val->valuestring, sizeof(out->promptBody));
        if ((val = cJSON_GetObjectItemCaseSensitive(prompt, "kind")) && cJSON_IsString(val))
            safeStrCopy(out->promptKind, val->valuestring, sizeof(out->promptKind));
        if ((val = cJSON_GetObjectItemCaseSensitive(prompt, "project")) && cJSON_IsString(val))
            safeStrCopy(out->promptProject, val->valuestring, sizeof(out->promptProject));
        if ((val = cJSON_GetObjectItemCaseSensitive(prompt, "sid")) && cJSON_IsString(val))
            safeStrCopy(out->promptSid, val->valuestring, sizeof(out->promptSid));
    } else {
        out->ClearPrompt();
    }

    // Dashboard fields
    if ((val = cJSON_GetObjectItemCaseSensitive(root, "project")) && cJSON_IsString(val))
        safeStrCopy(out->project, val->valuestring, sizeof(out->project));
    if ((val = cJSON_GetObjectItemCaseSensitive(root, "branch")) && cJSON_IsString(val))
        safeStrCopy(out->branch, val->valuestring, sizeof(out->branch));
    if ((val = cJSON_GetObjectItemCaseSensitive(root, "dirty")) && cJSON_IsNumber(val))
        out->dirty = (uint16_t)val->valueint;
    if ((val = cJSON_GetObjectItemCaseSensitive(root, "budget")) && cJSON_IsNumber(val))
        out->budgetLimit = (uint32_t)val->valuedouble;
    if ((val = cJSON_GetObjectItemCaseSensitive(root, "model")) && cJSON_IsString(val))
        safeStrCopy(out->modelName, val->valuestring, sizeof(out->modelName));
    if ((val = cJSON_GetObjectItemCaseSensitive(root, "assistant_msg")) && cJSON_IsString(val)) {
        if (strncmp(val->valuestring, out->assistantMsg, sizeof(out->assistantMsg)) != 0) {
            safeStrCopy(out->assistantMsg, val->valuestring, sizeof(out->assistantMsg));
            out->assistantGen++;
        }
    }

    out->lastUpdated = xTaskGetTickCount() * portTICK_PERIOD_MS;
    out->connected = true;
}
