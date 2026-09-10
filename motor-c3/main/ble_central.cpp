// =============================================================================
//  ble_central.cpp — NimBLE central implementation
// =============================================================================
//  Structure follows the canonical ESP-IDF "blecent" example: sync -> scan ->
//  match -> connect -> secure -> discover service/char -> enable CCCD notify ->
//  receive. NimBLE's C callbacks are routed back to the singleton.
//
//  BUILD NOTE: build this in your ESP-IDF v5.x environment (Docker or native).
//  The UUIDs and target name it looks for come from throttle_protocol.hpp and
//  config.hpp — make sure those match your S3 before expecting a connection.
// =============================================================================
#include "ble_central.hpp"

#include <cstring>

#include "config.hpp"
#include "throttle_protocol.hpp"

#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"

// Provided by NimBLE's bonding store; declared here to avoid pulling a private
// header. It registers the persistent key store callbacks.
extern "C" void ble_store_config_init(void);

namespace motor {

namespace {

constexpr const char* TAG = "BleCentral";

// Handles discovered on the peer, kept for the lifetime of a connection.
uint16_t s_conn_handle   = BLE_HS_CONN_HANDLE_NONE;
uint16_t s_chr_val_handle = 0;   // throttle characteristic value handle
uint8_t  s_own_addr_type  = 0;

// Forward declarations.
int  gap_event(struct ble_gap_event* event, void* arg);
void start_scan();

// ------------------------------------------------------------------ scanning
bool adv_matches(const struct ble_gap_disc_desc* disc)
{
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) {
        return false;
    }

    // Match by complete/short local name == TARGET_NAME.
    if (fields.name != nullptr && fields.name_len > 0) {
        const size_t want = std::strlen(cfg::TARGET_NAME);
        if (fields.name_len == want &&
            std::memcmp(fields.name, cfg::TARGET_NAME, want) == 0) {
            return true;
        }
    }

    // Or match if it advertises our 128-bit service UUID.
    for (int i = 0; i < fields.num_uuids128; i++) {
        if (ble_uuid_cmp(&fields.uuids128[i].u, &proto::THROTTLE_SVC_UUID.u) == 0) {
            return true;
        }
    }
    return false;
}

void start_scan()
{
    struct ble_gap_disc_params params = {};
    params.filter_duplicates = 1;
    params.passive           = 0;
    params.itvl              = 0;
    params.window            = 0;
    params.filter_policy     = 0;
    params.limited           = 0;

    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &params, gap_event, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
    } else {
        ESP_LOGI(TAG, "Scanning for '%s'...", cfg::TARGET_NAME);
    }
}

// -------------------------------------------------------- CCCD / subscription
int on_cccd_write(uint16_t conn_handle, const struct ble_gatt_error* error,
                  struct ble_gatt_attr* /*attr*/, void* /*arg*/)
{
    if (error != nullptr && error->status != 0) {
        ESP_LOGE(TAG, "CCCD write failed: status=%d", error->status);
        return 0;
    }
    ESP_LOGI(TAG, "Notifications enabled (conn=%u handle=%u)", conn_handle,
             s_chr_val_handle);
    return 0;
}

int on_dsc_disc(uint16_t conn_handle, const struct ble_gatt_error* error,
                uint16_t /*chr_val_handle*/, const struct ble_gatt_dsc* dsc,
                void* /*arg*/)
{
    if (error != nullptr && error->status != 0 && error->status != BLE_HS_EDONE) {
        ESP_LOGE(TAG, "descriptor discovery failed: %d", error->status);
        return 0;
    }
    if (dsc == nullptr) {
        return 0;  // BLE_HS_EDONE without finding the CCCD
    }

    // Client Characteristic Configuration Descriptor = 0x2902.
    if (ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
        uint16_t value = 1;  // 0x0001 = enable notifications
        int rc = ble_gattc_write_flat(conn_handle, dsc->handle, &value, sizeof(value),
                                      on_cccd_write, nullptr);
        if (rc != 0) {
            ESP_LOGE(TAG, "ble_gattc_write_flat(CCCD) failed: %d", rc);
        }
    }
    return 0;
}

int on_chr_disc(uint16_t conn_handle, const struct ble_gatt_error* error,
                const struct ble_gatt_chr* chr, void* /*arg*/)
{
    if (error != nullptr && error->status != 0 && error->status != BLE_HS_EDONE) {
        ESP_LOGE(TAG, "characteristic discovery failed: %d", error->status);
        return 0;
    }
    if (chr == nullptr) {
        return 0;  // done
    }

    s_chr_val_handle = chr->val_handle;
    ESP_LOGI(TAG, "Found throttle characteristic (val_handle=%u)", s_chr_val_handle);

    // Discover its descriptors to locate the CCCD (search to end of attribute
    // space; NimBLE stops at the next characteristic/service).
    int rc = ble_gattc_disc_all_dscs(conn_handle, chr->val_handle, 0xffff,
                                     on_dsc_disc, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gattc_disc_all_dscs failed: %d", rc);
    }
    return 0;
}

int on_svc_disc(uint16_t conn_handle, const struct ble_gatt_error* error,
                const struct ble_gatt_svc* svc, void* /*arg*/)
{
    if (error != nullptr && error->status != 0 && error->status != BLE_HS_EDONE) {
        ESP_LOGE(TAG, "service discovery failed: %d", error->status);
        return 0;
    }
    if (svc == nullptr) {
        return 0;  // done
    }

    ESP_LOGI(TAG, "Found throttle service (handles %u..%u)", svc->start_handle,
             svc->end_handle);
    int rc = ble_gattc_disc_chrs_by_uuid(conn_handle, svc->start_handle,
                                         svc->end_handle, &proto::THROTTLE_CHR_UUID.u,
                                         on_chr_disc, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gattc_disc_chrs_by_uuid failed: %d", rc);
    }
    return 0;
}

void begin_discovery(uint16_t conn_handle)
{
    int rc = ble_gattc_disc_svc_by_uuid(conn_handle, &proto::THROTTLE_SVC_UUID.u,
                                        on_svc_disc, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gattc_disc_svc_by_uuid failed: %d", rc);
    }
}

// --------------------------------------------------------------- GAP events
int gap_event(struct ble_gap_event* event, void* /*arg*/)
{
    ThrottleSupervisor* sup = BleCentral::instance().supervisor();

    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            if (adv_matches(&event->disc)) {
                ESP_LOGI(TAG, "Target found; stopping scan and connecting");
                ble_gap_disc_cancel();
                int rc = ble_gap_connect(s_own_addr_type, &event->disc.addr,
                                         30000, nullptr, gap_event, nullptr);
                if (rc != 0) {
                    ESP_LOGE(TAG, "ble_gap_connect failed: %d; rescanning", rc);
                    start_scan();
                }
            }
            return 0;
        }

        case BLE_GAP_EVENT_CONNECT: {
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                if (sup) sup->on_connected();
                // Establish an encrypted/bonded link before using the service.
                int rc = ble_gap_security_initiate(s_conn_handle);
                if (rc != 0 && rc != BLE_HS_EALREADY) {
                    ESP_LOGW(TAG, "security_initiate rc=%d; discovering anyway", rc);
                    begin_discovery(s_conn_handle);
                }
            } else {
                ESP_LOGW(TAG, "connect failed; status=%d; rescanning",
                         event->connect.status);
                start_scan();
            }
            return 0;
        }

        case BLE_GAP_EVENT_ENC_CHANGE: {
            ESP_LOGI(TAG, "encryption change; status=%d", event->enc_change.status);
            // Whether or not encryption succeeded, proceed to discovery; if the
            // characteristic requires encryption and it failed, the CCCD write
            // will report the error.
            begin_discovery(s_conn_handle);
            return 0;
        }

        case BLE_GAP_EVENT_DISCONNECT: {
            ESP_LOGW(TAG, "disconnected; reason=%d", event->disconnect.reason);
            s_conn_handle    = BLE_HS_CONN_HANDLE_NONE;
            s_chr_val_handle = 0;
            if (sup) sup->on_disconnected();
            start_scan();
            return 0;
        }

        case BLE_GAP_EVENT_NOTIFY_RX: {
            if (event->notify_rx.attr_handle == s_chr_val_handle &&
                s_chr_val_handle != 0) {
                uint8_t buf[8];
                uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
                if (len > sizeof(buf)) len = sizeof(buf);
                os_mbuf_copydata(event->notify_rx.om, 0, len, buf);

                proto::ThrottleMsg msg;
                if (proto::parse(buf, len, msg)) {
                    if (sup) sup->on_throttle(msg.throttle, msg.seq);
                } else {
                    ESP_LOGW(TAG, "bad throttle packet (len=%u) -> holding SAFE", len);
                }
            }
            return 0;
        }

        default:
            return 0;
    }
}

// --------------------------------------------------------------- host setup
void on_sync()
{
    // Ensure we have a usable identity address, then scan.
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: %d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }
    start_scan();
}

void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset; reason=%d", reason);
}

void host_task(void* /*param*/)
{
    nimble_port_run();  // returns only at nimble_port_stop()
    nimble_port_freertos_deinit();
}

}  // namespace

// ------------------------------------------------------------------ public
BleCentral& BleCentral::instance()
{
    static BleCentral s_instance;
    return s_instance;
}

void BleCentral::start(std::shared_ptr<ThrottleSupervisor> supervisor)
{
    m_supervisor = std::move(supervisor);

    // NVS is required for bonding key storage.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(nimble_port_init());

    // Host configuration.
    ble_hs_cfg.reset_cb        = on_reset;
    ble_hs_cfg.sync_cb         = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Security manager: bond, MITM off (no IO), secure connections, distribute keys.
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm    = 0;
    ble_hs_cfg.sm_sc      = 1;
    ble_hs_cfg.sm_our_key_dist  = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_store_config_init();

    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "NimBLE central started");
}

}  // namespace motor
