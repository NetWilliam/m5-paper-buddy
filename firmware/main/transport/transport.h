#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include <cstdint>
#include <cstddef>
#include <functional>

using DataCallback = std::function<void(const uint8_t* data, size_t len)>;

class Transport {
public:
    virtual ~Transport() = default;
    virtual bool Init() = 0;
    virtual bool Open() = 0;
    virtual void Close() = 0;
    virtual int Send(const uint8_t* data, size_t len) = 0;
    virtual void OnData(DataCallback callback) = 0;
    virtual size_t Poll() = 0;
};

#endif // _TRANSPORT_H_
