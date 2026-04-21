#include "ble_transport.h"
#include <esp_log.h>
#include <esp_random.h>
#include <cstring>
#include <cstdlib>

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// Forward declaration from NimBLE store ram module
extern "C" void ble_store_config_init(void);

static const char* TAG = "BleTransport";

// ── NUS UUIDs (Nordic UART Service) ──
static const ble_uuid128_t kNusSvcUuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

static const ble_uuid128_t kNusRxUuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

static const ble_uuid128_t kNusTxUuid = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

// ── Module-level state ──
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_val_handle = 0;
static bool s_notify_enabled = false;
static uint16_t s_mtu = 23;
static uint32_t s_passkey = 0;
static BleTransport* s_instance = nullptr;

// ── Forward declarations for C-linkage callbacks ──
static int ble_gap_event_handler(struct ble_gap_event* event, void* arg);
static int ble_gatt_access_handler(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt* ctxt, void* arg);

// ── GATT service table ──
static const struct ble_gatt_svc_def kNusSvcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &kNusSvcUuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // TX: notify (device -> client)
                .uuid = &kNusTxUuid.u,
                .access_cb = ble_gatt_access_handler,
                .arg = nullptr,
                .descriptors = nullptr,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .min_key_size = 0,
                .val_handle = &s_tx_val_handle,
                .cpfd = nullptr,
            },
            {
                // RX: write (client -> device)
                .uuid = &kNusRxUuid.u,
                .access_cb = ble_gatt_access_handler,
                .arg = nullptr,
                .descriptors = nullptr,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .min_key_size = 0,
                .val_handle = nullptr,
                .cpfd = nullptr,
            },
            { 0 }
        },
    },
    { 0 }
};

// ── Constructor / Destructor ──

BleTransport::BleTransport() {
    s_instance = this;
}

BleTransport::~BleTransport() {
    Close();
    s_instance = nullptr;
}

// ── Transport interface ──

bool BleTransport::Init() {
    if (initialized_) return true;

    ESP_LOGI(TAG, "Initializing NimBLE");

    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
        return false;
    }

    // Host config
    ble_hs_cfg.reset_cb = OnReset;
    ble_hs_cfg.sync_cb = OnSync;
    ble_hs_cfg.gatts_register_cb = nullptr;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Security: Secure Connections + Legacy, display passkey
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    // Register GATT services
    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(kNusSvcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return false;
    }

    rc = ble_gatts_add_svcs(kNusSvcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return false;
    }

    ble_svc_gap_device_name_set("Paper Buddy");

    ble_store_config_init();

    nimble_port_freertos_init(HostTask);

    initialized_ = true;
    ESP_LOGI(TAG, "BLE initialized (NUS service registered)");
    return true;
}

bool BleTransport::Open() {
    if (!initialized_ && !Init()) return false;
    return true;
}

void BleTransport::Close() {
    if (initialized_) {
        if (connected_ && s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        nimble_port_stop();
        initialized_ = false;
        connected_ = false;
    }
}

int BleTransport::Send(const uint8_t* data, size_t len) {
    if (!connected_ || !s_notify_enabled || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return -1;
    }

    const uint16_t chunk_size = (s_mtu > 3) ? (s_mtu - 3) : 20;
    size_t offset = 0;

    while (offset < len) {
        size_t remain = len - offset;
        uint16_t n = (remain > chunk_size) ? chunk_size : (uint16_t)remain;

        struct os_mbuf* om = ble_hs_mbuf_from_flat(data + offset, n);
        if (!om) {
            ESP_LOGE(TAG, "mbuf_from_flat failed at offset %u", (unsigned)offset);
            return (offset > 0) ? (int)offset : -1;
        }

        int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_val_handle, om);
        if (rc != 0) {
            ESP_LOGW(TAG, "notify_custom failed: %d", rc);
            os_mbuf_free_chain(om);
            return (offset > 0) ? (int)offset : -1;
        }

        offset += n;
    }

    return (int)offset;
}

void BleTransport::OnData(DataCallback callback) {
    data_callback_ = callback;
}

size_t BleTransport::Poll() {
    return 0;
}

bool BleTransport::IsConnected() const { return connected_; }
uint32_t BleTransport::GetPasskey() const { return s_passkey; }

// ── NimBLE callbacks (static / free functions) ──

void BleTransport::OnSync() {
    ESP_LOGI(TAG, "BLE synced");

    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure_addr failed: %d", rc);
        return;
    }

    if (s_instance) {
        s_instance->StartAdvertising();
    }
}

void BleTransport::OnReset(int reason) {
    ESP_LOGW(TAG, "BLE reset: reason=%d", reason);
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_notify_enabled = false;
    if (s_instance) {
        s_instance->SetConnected(false);
    }
}

void BleTransport::HostTask(void* param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void BleTransport::StartAdvertising() {
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // 128-bit UUID = 18 bytes, flags = 3 bytes → 21 total (fits 31-byte limit)
    fields.uuids128 = &kNusSvcUuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed: %d", rc);
        return;
    }

    // Put device name in scan response (separate 31-byte payload)
    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));

    const char* name = ble_svc_gap_device_name();
    rsp_fields.name = (uint8_t*)name;
    rsp_fields.name_len = strlen(name);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_rsp_set_fields failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    uint8_t own_addr_type;
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "id_infer_auto failed: %d", rc);
        return;
    }

    rc = ble_gap_adv_start(own_addr_type, nullptr, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event_handler, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start failed: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE advertising started");
}

static int ble_gap_event_handler(struct ble_gap_event* event, void* arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            if (s_instance) {
                s_instance->SetConnected(true);
            }
            ESP_LOGI(TAG, "BLE connected (handle=%d)", s_conn_handle);
        } else {
            ESP_LOGW(TAG, "BLE connect failed: %d", event->connect.status);
            if (s_instance) {
                s_instance->StartAdvertising();
            }
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnected (reason=%d)", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_notify_enabled = false;
        if (s_instance) {
            s_instance->SetConnected(false);
            s_instance->StartAdvertising();
        }
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_instance) {
            s_instance->StartAdvertising();
        }
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_val_handle) {
            s_notify_enabled = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "TX notify %s", s_notify_enabled ? "enabled" : "disabled");
        }
        break;

    case BLE_GAP_EVENT_MTU:
        s_mtu = event->mtu.value;
        ESP_LOGI(TAG, "MTU update: %d", s_mtu);
        break;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        struct ble_sm_io pkey = {};
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pkey.action = BLE_SM_IOACT_DISP;
            s_passkey = esp_random() % 1000000;
            pkey.passkey = s_passkey;
            ESP_LOGI(TAG, "BLE passkey: %06" PRIu32, s_passkey);
            ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        }
        break;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    default:
        break;
    }
    return 0;
}

static int ble_gatt_access_handler(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt* ctxt, void* arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (s_instance) {
            auto& cb = s_instance->GetDataCallback();
            if (cb) {
                uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
                if (om_len == 0) return 0;

                uint8_t* buf = (uint8_t*)malloc(om_len);
                if (!buf) return BLE_ATT_ERR_INSUFFICIENT_RES;

                int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, om_len, nullptr);
                if (rc == 0) {
                    cb(buf, om_len);
                }
                free(buf);
            }
        }
        return 0;
    }

    return 0;
}
