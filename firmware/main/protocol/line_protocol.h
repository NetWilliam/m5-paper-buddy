#ifndef _LINE_PROTOCOL_H_
#define _LINE_PROTOCOL_H_

#include <cstdint>
#include <cstddef>
#include <functional>
#include "cJSON.h"

using MessageCallback = std::function<void(cJSON* msg)>;

/// Line-buffered JSON parser for the heartbeat protocol.
/// Accumulates bytes until \n, then parses the line as JSON
/// and calls the registered callback.
class LineProtocol {
public:
    LineProtocol();
    ~LineProtocol() = default;

    /// Feed raw bytes (from transport). Handles partial lines.
    void Feed(const uint8_t* data, size_t len);

    /// Register callback for parsed JSON messages.
    void OnMessage(MessageCallback callback);

    /// Reset internal buffer.
    void Reset();

private:
    static constexpr size_t MAX_LINE = 4096;
    char line_buf_[MAX_LINE];
    size_t line_len_ = 0;
    bool skipping_ = false;
    MessageCallback callback_;
};

#endif // _LINE_PROTOCOL_H_
