#ifndef _SERIAL_JTAG_TRANSPORT_H_
#define _SERIAL_JTAG_TRANSPORT_H_

#include "transport.h"
#include "driver/usb_serial_jtag.h"

class SerialJtagTransport : public Transport {
public:
    SerialJtagTransport() = default;
    ~SerialJtagTransport() override;

    bool Init() override;
    bool Open() override;
    void Close() override;
    int Send(const uint8_t* data, size_t len) override;
    void OnData(DataCallback callback) override;
    size_t Poll() override;

private:
    static constexpr const char* TAG = "SerialJtag";
    bool initialized_ = false;
    DataCallback data_callback_;
};

#endif // _SERIAL_JTAG_TRANSPORT_H_
