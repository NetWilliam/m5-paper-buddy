#ifndef _BLE_TRANSPORT_H_
#define _BLE_TRANSPORT_H_

#include "transport.h"

class BleTransport : public Transport {
public:
    BleTransport();
    ~BleTransport() override;

    bool Init() override;
    bool Open() override;
    void Close() override;
    int Send(const uint8_t* data, size_t len) override;
    void OnData(DataCallback callback) override;
    size_t Poll() override;

    bool IsConnected() const;
    uint32_t GetPasskey() const;

private:
    static constexpr const char* TAG = "BleTransport";
    bool initialized_ = false;
    bool connected_ = false;
    DataCallback data_callback_;
    uint32_t passkey_ = 0;
};

#endif // _BLE_TRANSPORT_H_
