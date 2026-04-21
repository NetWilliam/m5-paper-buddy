#ifndef _BLE_TRANSPORT_H_
#define _BLE_TRANSPORT_H_

#include "transport.h"
#include <functional>

class BleTransport : public Transport {
public:
    using PasskeyCallback = std::function<void(uint32_t)>;

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
    void ClearPasskey();
    DataCallback& GetDataCallback() { return data_callback_; }

    void OnPasskey(PasskeyCallback cb) { passkey_callback_ = cb; }
    PasskeyCallback& GetPasskeyCallback() { return passkey_callback_; }

    // Called by C-linkage NimBLE callbacks
    void StartAdvertising();

private:
    static constexpr const char* TAG = "BleTransport";
    bool initialized_ = false;
    bool connected_ = false;
    DataCallback data_callback_;
    PasskeyCallback passkey_callback_;

    // NimBLE host callbacks
    static void OnSync();
    static void OnReset(int reason);
    static void HostTask(void* param);
};

#endif // _BLE_TRANSPORT_H_
