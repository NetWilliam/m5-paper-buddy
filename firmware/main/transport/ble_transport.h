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
    void SetConnected(bool v) { connected_ = v; }
    uint32_t GetPasskey() const;
    DataCallback& GetDataCallback() { return data_callback_; }

    // Called by C-linkage NimBLE callbacks
    void StartAdvertising();

private:
    static constexpr const char* TAG = "BleTransport";
    bool initialized_ = false;
    bool connected_ = false;
    DataCallback data_callback_;

    // NimBLE host callbacks
    static void OnSync();
    static void OnReset(int reason);
    static void HostTask(void* param);
};

#endif // _BLE_TRANSPORT_H_
