#include "line_protocol.h"
#include <esp_log.h>
#include <cstring>

static const char* TAG = "LineProtocol";

LineProtocol::LineProtocol() {
    memset(line_buf_, 0, sizeof(line_buf_));
}

void LineProtocol::Feed(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = static_cast<char>(data[i]);

        if (c == '\n' || c == '\r') {
            skipping_ = false;
            if (line_len_ > 0) {
                line_buf_[line_len_] = '\0';
                if (line_buf_[0] == '{' && callback_) {
                    cJSON* root = cJSON_Parse(line_buf_);
                    if (root) {
                        callback_(root);
                        cJSON_Delete(root);
                    } else {
                        ESP_LOGD(TAG, "JSON parse error: %.60s", line_buf_);
                    }
                }
                line_len_ = 0;
            }
        } else if (skipping_) {
            // Discard bytes from an overflowed line until newline
        } else {
            if (line_len_ < MAX_LINE - 1) {
                line_buf_[line_len_++] = c;
            } else {
                ESP_LOGW(TAG, "Line buffer overflow, discarding until newline");
                line_len_ = 0;
                skipping_ = true;
            }
        }
    }
}

void LineProtocol::OnMessage(MessageCallback callback) {
    callback_ = callback;
}

void LineProtocol::Reset() {
    line_len_ = 0;
    skipping_ = false;
    line_buf_[0] = '\0';
}
