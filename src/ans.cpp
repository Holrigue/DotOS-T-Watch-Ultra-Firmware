// ans.cpp - see ans.h. Alert Notification Service GATT server for Gadgetbridge.
#include "ans.h"
#include "notify/ans_packet.h"
#include "notify/notify_center.h"
#include "notify/notify_log.h"
#include "health_state.h"   // health packet ingest (Phase 2.2)

#include <Arduino.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <esp_gap_ble_api.h>   // esp_ble_gap_update_conn_params (link power saving)
#include <esp_gatts_api.h>     // esp_ble_gatts_register_callback (slot re-assert)
#include "ble_scan_manager.h"  // ble_gap_route / ble_stack_pin (shared radio)
#include <LilyGoLib.h>       // instance.isCardReady() for the GPX receiver
#include <SD.h>
#include <esp_heap_caps.h>   // PSRAM buffer for the streamed GPX
#include <cstring>
#include <strings.h>         // strcasecmp

namespace ans {
namespace {

// Standard Bluetooth SIG assigned numbers.
constexpr uint16_t UUID_ANS          = 0x1811;   // Alert Notification Service
constexpr uint16_t UUID_NEW_ALERT    = 0x2A46;   // write: incoming notification
constexpr uint16_t UUID_SUP_NEW_CAT  = 0x2A47;   // read: supported categories
constexpr uint16_t UUID_DIS          = 0x180A;   // Device Information Service
constexpr uint16_t UUID_FW_REV       = 0x2A26;   // Firmware Revision String

// Vendor health-input characteristic (Phase 2.2). A phone-side companion app
// reading Gadgetbridge's Amazfit data writes a compact health packet here; see
// docs/health/README.md for the wire format. It sits on the ANS service (0x1811)
// so it is always discovered alongside notifications. 128-bit custom UUID so it
// never collides with an assigned number.
constexpr char UUID_HEALTH_IN[] = "a2470002-5a4b-4d55-9a3e-1c2d3e4f5a6b";

// Find channel (Find-My-Watch / Find-My-Phone). Also on the ANS service, same
// reasoning as the health characteristic. Phone writes here to ring the watch;
// the watch notifies here to ring the phone.
constexpr char UUID_FIND[] = "a2470003-5a4b-4d55-9a3e-1c2d3e4f5a6b";

// GPX route push. The companion app streams a .gpx file here in frames:
//   [0x01][nameLen][name…]  BEGIN (start a new transfer, reset the buffer)
//   [0x02][bytes…]          DATA  (append file bytes, in order)
//   [0x03]                  END   (transfer complete -> write to SD next loop)
//   [0x04]                  ABORT (discard)
// Writes stay off the radio task: bytes land in a PSRAM buffer and the main loop
// (service_gpx_rx) flushes a completed file to /gpx.
constexpr char UUID_GPX_IN[] = "a2470004-5a4b-4d55-9a3e-1c2d3e4f5a6b";
constexpr size_t GPX_RX_MAX  = 512 * 1024;   // generous cap; routes are a few KB

bool                s_running   = false;
volatile bool       s_connected = false;
BLEServer          *s_server    = nullptr;
uint32_t            s_uid_seq   = 1;   // synthetic ids (ANS carries no stable uid)
BLECharacteristic  *s_find_char = nullptr;             // for watch -> phone notify
void              (*s_find_handler)(uint8_t) = nullptr; // phone -> watch write

// GPX receive buffer (PSRAM). Filled by the BLE write callback; drained to SD by
// service_gpx_rx() on the main loop. s_gpx_ready gates the one-time SD write.
uint8_t          *s_gpx_buf       = nullptr;
size_t            s_gpx_len       = 0;
char              s_gpx_name[48]  = "route.gpx";
volatile bool     s_gpx_receiving = false;
volatile bool     s_gpx_ready     = false;

bool wifi_active() { return WiFi.getMode() != WIFI_MODE_NULL; }

// Sanitise an incoming file name: keep the last path segment, allow only safe
// characters, and guarantee a .gpx suffix. Never lets the phone escape /gpx.
void gpx_sanitize_name(const char *in, int inlen, char *out, size_t outsz)
{
    size_t o = 0;
    for (int i = 0; i < inlen && o + 1 < outsz; i++) {
        char c = in[i];
        if (c == '/' || c == '\\') { o = 0; continue; }   // drop any path prefix
        bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == ' ';
        if (ok) out[o++] = c;
    }
    out[o] = '\0';
    if (o == 0) { strncpy(out, "route.gpx", outsz - 1); out[outsz - 1] = '\0'; return; }
    size_t len = strlen(out);
    if (len < 4 || strcasecmp(out + len - 4, ".gpx") != 0) {
        if (len + 4 < outsz) strcat(out, ".gpx");
    }
}

// Map an ANS CategoryID (Bluetooth spec) to our Category vocabulary.
notify::Category map_category(uint8_t c)
{
    using C = notify::Category;
    switch (c) {
        case 1:  return C::Email;
        case 2:  return C::News;
        case 3:  return C::IncomingCall;
        case 4:  return C::MissedCall;
        case 5:  return C::Social;         // SMS/MMS
        case 6:  return C::Voicemail;
        case 7:  return C::Schedule;
        case 9:  return C::Social;         // instant message
        default: return C::Other;          // 0 simple, 8 high-priority
    }
}

// Parse a New Alert write and publish it. Format (InfiniTime / Gadgetbridge):
//   [category][count] then 0x00-separated UTF-8 strings (title, then body).
void parse_new_alert(const uint8_t *d, size_t len)
{
    if (len < 2) return;
    if (ans_is_call_end(d, len)) { notify::end_call(); return; }   // the phone says the call is over
    uint8_t cat = d[0];
    // Walk the 0x00-separated payload after the 2-byte header, skipping empties.
    const char *fields[2] = { nullptr, nullptr };
    int nf = 0;
    size_t i = 2;
    while (i < len && nf < 2) {
        while (i < len && d[i] == 0x00) i++;        // skip separators
        if (i >= len) break;
        fields[nf++] = (const char *)&d[i];
        while (i < len && d[i] != 0x00) i++;         // to next separator
        // The characteristic value is not guaranteed NUL-terminated at the end;
        // we copy with a bounded length below, so a missing final 0x00 is fine.
    }

    notify::Notification n;
    n.uid = s_uid_seq++;
    n.category = map_category(cat);

    // Copy field 0 -> title, field 1 -> body, bounded and NUL-terminated. Because
    // fields[] point into d[] which may lack a terminator on the last field, cap
    // each copy at the remaining buffer length.
    auto copy_field = [&](const char *src, char *dst, int cap) {
        if (!src) return;
        size_t max = (size_t)(&((const char *)d)[len] - src);
        size_t k = 0;
        while (k < (size_t)(cap - 1) && k < max && src[k] != 0x00) { dst[k] = src[k]; k++; }
        dst[k] = 0;
    };
    copy_field(fields[0], n.title, notify::kTitleLen);
    copy_field(fields[1], n.body,  notify::kBodyLen);
    strncpy(n.app, "Phone", notify::kAppLen - 1);

    notify::publish(n);
}

class NewAlertCb : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        if (!s_running) return;   // notify off: a lingering link's writes are ignored
        std::string v = c->getValue();
        parse_new_alert((const uint8_t *)v.data(), v.size());
    }
};
NewAlertCb s_new_alert_cb;

// Health packet writes. Runs in the BLE host task; health_ingest_packet() only
// copies into a mailbox, so the model itself stays on the loop.
class HealthInCb : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        if (!s_running) return;
        std::string v = c->getValue();
        health_ingest_packet((const uint8_t *)v.data(), v.size());
    }
};
HealthInCb s_health_in_cb;

// Find writes from the phone: a 1-byte op (0x01 start ringing the watch, 0x00
// stop). Hand it to the registered handler, which runs the on-watch alert.
class FindCb : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        if (!s_running) return;
        std::string v = c->getValue();
        if (!v.empty() && s_find_handler) s_find_handler((uint8_t)v[0]);
    }
};
FindCb s_find_cb;

// GPX frame receiver. Buffers into PSRAM only; the main loop writes the SD file.
class GpxInCb : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        if (!s_running) return;
        std::string v = c->getValue();
        const uint8_t *d = (const uint8_t *)v.data();
        size_t len = v.size();
        if (len < 1) return;

        switch (d[0]) {
        case 0x01: {   // BEGIN
            if (!s_gpx_buf) {
                s_gpx_buf = (uint8_t *)heap_caps_malloc(GPX_RX_MAX, MALLOC_CAP_SPIRAM);
            }
            s_gpx_len       = 0;
            s_gpx_ready     = false;
            s_gpx_receiving = (s_gpx_buf != nullptr);
            int nameLen = (len >= 2) ? d[1] : 0;
            if (nameLen > (int)len - 2) nameLen = (int)len - 2;
            gpx_sanitize_name((const char *)(d + 2), nameLen, s_gpx_name, sizeof s_gpx_name);
            break;
        }
        case 0x02:     // DATA
            if (s_gpx_receiving && s_gpx_buf && len > 1) {
                size_t n = len - 1;
                if (s_gpx_len + n > GPX_RX_MAX) n = GPX_RX_MAX - s_gpx_len;
                if (n > 0) { memcpy(s_gpx_buf + s_gpx_len, d + 1, n); s_gpx_len += n; }
            }
            break;
        case 0x03:     // END
            if (s_gpx_receiving) { s_gpx_receiving = false; s_gpx_ready = (s_gpx_len > 0); }
            break;
        case 0x04:     // ABORT
        default:
            s_gpx_receiving = false;
            s_gpx_len       = 0;
            break;
        }
    }
};
GpxInCb s_gpx_cb;

// Peer address of the connected phone, for connection-parameter requests.
esp_bd_addr_t s_peer_bda   = {0};
volatile bool s_have_peer  = false;
volatile bool s_link_relaxed = false;   // what the UI last asked for

// Ask the phone for a slower or a faster link. It is a REQUEST: the phone (the
// central) decides, and a refusal simply leaves the link as it was.
//
// Relaxed (panel off): 100-150 ms interval with slave latency 4, so the radio
// may sleep through up to 4 empty events - a notification still lands within
// ~0.75 s. Fast (panel on): 30-50 ms, no latency, for a responsive UI. Both sets
// stay inside the stricter of the Android/Apple accessory limits (interval *
// (latency + 1) <= 2 s, supervision timeout > 3x that).
void request_link_params(bool relaxed)
{
    if (!s_have_peer) return;
    esp_ble_conn_update_params_t p = {};
    memcpy(p.bda, s_peer_bda, sizeof(esp_bd_addr_t));
    if (relaxed) { p.min_int = 0x50; p.max_int = 0x78; p.latency = 4; p.timeout = 600; }
    else         { p.min_int = 0x18; p.max_int = 0x28; p.latency = 0; p.timeout = 400; }
    esp_ble_gap_update_conn_params(&p);
}

class ServerCb : public BLEServerCallbacks {
    void onConnect(BLEServer *) override { s_connected = true; }
    void onConnect(BLEServer *, esp_ble_gatts_cb_param_t *param) override {
        memcpy(s_peer_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        s_have_peer = true;
        // A phone that connects while the panel is already off gets the relaxed
        // link straight away.
        if (s_link_relaxed) request_link_params(true);
    }
    void onDisconnect(BLEServer *) override {
        s_connected = false;
        s_have_peer = false;
        if (s_running) BLEDevice::startAdvertising();   // reconnectable
    }
};
ServerCb s_server_cb;

// ---- Shared-radio dispatchers ----------------------------------------------
// Bluedroid has ONE callback slot per layer (GAP, GATTS) and the last
// esp_ble_*_register_callback() call wins. Arduino's BLEDevice grabs both
// slots in init(), and its own handlers are PRIVATE — so once ANCS (an iOS
// interlude) has taken the slots there is no way to hand them back to Arduino.
// These trampolines are our own slot owners, equivalent to Arduino's handlers
// for this server-only setup, so start() can re-assert them at ANY time — they
// are installed on EVERY start so the common path and the after-iOS path are
// the same, hardware-exercised code. gap_tramp also forwards every event to
// the detector scan dispatcher first, so the AirTag / Flock / wardriver scans
// keep flowing while (and after) Android notifications are up.
//
// BLEServer::handleGATTServerEvent — the exact dispatch Arduino's handler
// performs — is PRIVATE in the vendored core (friend-only, BLEDevice), and so
// is Arduino's registered handler itself, so there is no public way to point
// the GATTS slot back at the server. The standard exempts the arguments of an
// explicit template instantiation from access checks (C++11 [temp.explicit],
// C++17 [temp.spec]), which lets us capture a pointer to that exact member
// LEGALLY — no "#define private public", no copied library internals.
typedef void (BLEServer::*GattsHandlerFn)(esp_gatts_cb_event_t, esp_gatt_if_t,
                                          esp_ble_gatts_cb_param_t *);
GattsHandlerFn gatts_handler_fn();   // defined by the friend injection below
template <GattsHandlerFn M> struct GattsBackdoor {
    friend GattsHandlerFn gatts_handler_fn() { return M; }
};
template struct GattsBackdoor<&BLEServer::handleGATTServerEvent>;

void gap_tramp(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    ble_gap_route(event, param);   // detectors first — never blocked by notify
    if (event == ESP_GAP_BLE_SEC_REQ_EVT) {
        // Just-works accept (IO cap NONE) — the same answer Arduino's private
        // handler gives when no security callbacks are installed.
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        return;
    }
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    if (adv) adv->handleGAPEvent(event, param);   // advertising state machine
}

void gatts_tramp(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                 esp_ble_gatts_cb_param_t *param)
{
    // Same dispatch Arduino's (private) handler performs: hand the event to
    // the one BLEServer, via the access backdoor above.
    if (s_server) (s_server->*gatts_handler_fn())(event, gatts_if, param);
}

}  // namespace

bool start()
{
    if (s_running) return true;
    if (wifi_active()) return false;

    if (!s_server) {
        // First start this boot. Bring the Arduino Bluedroid wrapper up — it is
        // state-aware, so a controller the detector keep-alive already enabled
        // is reused, not re-initialised — and build the GATT services ONCE.
        // They are never torn down again; see stop().
        //
        // Advertise as an InfiniTime so Gadgetbridge auto-detects the device and
        // drives it through the standard Alert Notification Service.
        BLEDevice::init("InfiniTime");
        ble_stack_pin();   // the shared controller now outlives the scanners

        s_server = BLEDevice::createServer();
        if (!s_server) return false;
        s_server->setCallbacks(&s_server_cb);

        // Alert Notification Service with the New Alert (write) characteristic that
        // Gadgetbridge pushes notifications to, plus the read-only supported-category
        // bitmask (advertise "all categories").
        BLEService *ans = s_server->createService(BLEUUID(UUID_ANS));
        BLECharacteristic *new_alert = ans->createCharacteristic(
            BLEUUID(UUID_NEW_ALERT),
            BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
        new_alert->setCallbacks(&s_new_alert_cb);

        BLECharacteristic *sup = ans->createCharacteristic(
            BLEUUID(UUID_SUP_NEW_CAT), BLECharacteristic::PROPERTY_READ);
        uint8_t all_cats[2] = { 0xFF, 0x03 };   // all defined categories supported
        sup->setValue(all_cats, sizeof(all_cats));

        // Health-input characteristic (Phase 2.2): the phone relay writes health
        // packets here. It lives ON the ANS service rather than a separate 3rd
        // service - adding a third GATT service did not register reliably on this
        // BLE stack, so the app found no health service. Gadgetbridge ignores this
        // extra characteristic; only our companion app writes to it.
        BLECharacteristic *health_in = ans->createCharacteristic(
            BLEUUID(UUID_HEALTH_IN),
            BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
        health_in->setCallbacks(&s_health_in_cb);

        // Find characteristic (Find-My-Watch / Find-My-Phone). WRITE for phone->watch
        // ring, NOTIFY (with a CCCD) for watch->phone ring.
        s_find_char = ans->createCharacteristic(
            BLEUUID(UUID_FIND),
            BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR |
                BLECharacteristic::PROPERTY_NOTIFY);
        s_find_char->addDescriptor(new BLE2902());
        s_find_char->setCallbacks(&s_find_cb);

        // GPX-in: the companion app streams a shared route here (see UUID_GPX_IN).
        BLECharacteristic *gpx_in = ans->createCharacteristic(
            BLEUUID(UUID_GPX_IN),
            BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
        gpx_in->setCallbacks(&s_gpx_cb);

        ans->start();

        // Minimal Device Information Service; Gadgetbridge's InfiniTime coordinator
        // reads a firmware revision during setup.
        BLEService *dis = s_server->createService(BLEUUID(UUID_DIS));
        BLECharacteristic *fw = dis->createCharacteristic(
            BLEUUID(UUID_FW_REV), BLECharacteristic::PROPERTY_READ);
        fw->setValue("1.7.0");
        dis->start();

        // Just-works bonded pairing, same posture as the HID mouse.
        BLESecurity security;
        security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
        security.setCapability(ESP_IO_CAP_NONE);
        security.setKeySize(16);
        security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
        security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);


        BLEAdvertising *adv = BLEDevice::getAdvertising();
        adv->addServiceUUID(BLEUUID(UUID_ANS));
        adv->setScanResponse(true);
    }

    // (Re-)assert OUR dispatchers and name on the single Bluedroid slots.
    // An iOS (ANCS) interlude takes both slots and renames the device, and
    // Arduino's own handlers are private, so they cannot be re-registered —
    // our trampolines can, any number of times. Done on EVERY start so the
    // common path and the after-iOS path are the same code.
    esp_ble_gap_register_callback(gap_tramp);
    esp_ble_gatts_register_callback(gatts_tramp);
    esp_ble_gap_set_device_name("InfiniTime");

    s_running   = true;
    s_connected = false;
    BLEDevice::getAdvertising()->start();
    NLOGLN("[ans] started - advertising as InfiniTime for Gadgetbridge");
    return true;
}

void stop()
{
    if (!s_running) return;
    s_running   = false;
    s_connected = false;
    // Advertising off; a still-connected phone is left to drop on its own
    // (its writes are gated on s_running). The Bluedroid stack, the GATT
    // services and s_server are deliberately KEPT: BLEDevice::deinit() here
    // used to pull the whole shared BT controller down under the detector
    // scan manager (boot keep-alive + Flock et al. still registered), which
    // crashed the watch and — replayed by the boot restore — blackscreened
    // every boot until the boot guard wiped NVS (v3.0.0, 2026-11-12).
    // "Stopped" now just means not discoverable and not listening.
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    if (adv) adv->stop();
}

bool is_running()   { return s_running;   }
bool is_connected() { return s_connected; }

void set_link_low_power(bool low)
{
    if (low == s_link_relaxed) return;
    s_link_relaxed = low;
    if (s_running && s_connected) request_link_params(low);
}

void set_find_handler(void (*fn)(uint8_t)) { s_find_handler = fn; }

bool find_notify(uint8_t op)
{
    if (!s_running || !s_connected || !s_find_char) return false;
    s_find_char->setValue(&op, 1);
    s_find_char->notify();
    return true;
}

void service_gpx_rx()
{
    if (!s_gpx_ready) return;
    s_gpx_ready = false;
    if (!s_gpx_buf || s_gpx_len == 0) return;
    if (!instance.isCardReady()) return;

    if (!SD.exists("/gpx")) SD.mkdir("/gpx");
    char path[64];
    snprintf(path, sizeof(path), "/gpx/%s", s_gpx_name);
    SD.remove(path);                       // overwrite any prior file of this name
    File f = SD.open(path, FILE_WRITE);
    if (!f) return;
    size_t wrote = f.write(s_gpx_buf, s_gpx_len);
    f.close();
    s_gpx_len = 0;
    if (wrote == 0) return;

    // Let the wearer know the route landed (also lights the notifications square).
    notify::Notification n;
    n.uid      = s_uid_seq++;
    n.category = notify::Category::Other;
    strncpy(n.app,   "Routes",         notify::kAppLen  - 1);
    strncpy(n.title, "Route received", notify::kTitleLen - 1);
    strncpy(n.body,  s_gpx_name,        notify::kBodyLen - 1);
    notify::publish(n);
}

}  // namespace ans
