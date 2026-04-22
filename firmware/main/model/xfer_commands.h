#ifndef _XFER_COMMANDS_H_
#define _XFER_COMMANDS_H_

#include "cJSON.h"
#include <cstddef>
#include <functional>

class XferCommands {
public:
    using SendLine = std::function<void(const char* line, size_t len)>;

    void Init(SendLine send);
    bool TryHandle(cJSON* root);

private:
    SendLine send_;
};

#endif // _XFER_COMMANDS_H_
