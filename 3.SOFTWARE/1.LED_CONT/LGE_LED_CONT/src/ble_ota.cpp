/* ============================================================================
 *  ble_ota.cpp  -  BLE wireless firmware update (see ble_ota.h for the protocol)
 *
 *  Threads:
 *    - NimBLE host task : GATT callbacks (must stay short: copy data, post to the queue)
 *    - "ble_ota" task   : flash work (esp_ota_begin / write / end), sends the notifications
 *    - loop()           : ble_ota_poll() -> USB plug check (BLE on only while USB is out),
 *                         reboot after success; ble_ota_host_read() -> 1-byte commands from BLE
 * ========================================================================== */
#include <Arduino.h>
#include <string.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include "HWCDC.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"
#include "config.h"
#include "ble_ota.h"

#define XSTR_(x) #x
#define XSTR(x)  XSTR_(x)

/* Firmware tag: kept in the image (INFO characteristic reads it), found by the PC tool and by
 * the board itself in a received image. */
static const char kFwTag[] = "DC27FW|" FW_TYPE "|" XSTR(DEVICE_ID) "|" FW_VERSION "|";
static const char kBleName[] = FW_TYPE " " XSTR(DEVICE_ID) " " FW_DESC;

#define OTA_SVC_UUID   "4c474501-dc27-4f54-a000-000000000001"
#define OTA_INFO_UUID  "4c474501-dc27-4f54-a000-000000000002"
#define OTA_CTRL_UUID  "4c474501-dc27-4f54-a000-000000000003"
#define OTA_DATA_UUID  "4c474501-dc27-4f54-a000-000000000004"
#define OTA_HOST_UUID  "4c474501-dc27-4f54-a000-000000000005"

#define OTA_BLOCK      4096u
#define OTA_MAX_PIN_TRIES 3
#define USB_OUT_MS     2000u   /* no USB SOF for this long -> "USB out" -> BLE on */
#define USB_IN_MS      300u    /* SOF back for this long   -> "USB in"  -> BLE off */

enum { EV_BEGIN, EV_BLOCK, EV_END, EV_ABORT };
struct OtaEvent { uint8_t type; };

static BLEServer         *s_server   = nullptr;
static BLECharacteristic *s_ctrl     = nullptr;
static BLECharacteristic *s_host     = nullptr;
static QueueHandle_t      s_queue    = nullptr;
static QueueHandle_t      s_host_q   = nullptr;   /* 1-byte host commands from BLE -> loop() */

static volatile bool      s_connected   = false;
static volatile uint16_t  s_conn_handle = 0;
static volatile bool      s_advertising = false;
static volatile bool      s_ble_allowed = false;   /* USB is out (debounced) */
static volatile bool      s_authed      = false;   /* PIN given on this connection */
static volatile bool      s_locked      = false;
static uint8_t            s_pin_fail    = 0;

/* transfer state (written by the host task before posting an event, then owned by the worker) */
static volatile bool      s_active      = false;   /* BEGIN accepted, END/ABORT not done yet */
static volatile bool      s_block_busy  = false;   /* block buffer handed to the worker */
static volatile bool      s_reboot      = false;
static uint32_t           s_reboot_at   = 0;
static uint32_t           s_size        = 0;
static uint32_t           s_rx_offset   = 0;       /* next expected DATA offset */
static uint32_t           s_block_start = 0;
static uint32_t           s_block_len   = 0;
static uint8_t            s_block[OTA_BLOCK];
static uint8_t            s_sha_expect[32];
static bool               s_force_id    = false;

/* worker-only */
static esp_ota_handle_t       s_handle  = 0;
static const esp_partition_t *s_part    = nullptr;
static mbedtls_sha256_context s_sha;

const char *ble_ota_fw_tag(void) { return kFwTag; }
bool ble_ota_busy(void) { return s_active; }

static void put_u32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static uint32_t get_u32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static void notify(const uint8_t *d, size_t n)
{
  if (!s_ctrl || !s_connected) return;
  s_ctrl->setValue((uint8_t *)d, n);
  s_ctrl->notify();
}
static void notify_status(uint8_t code, uint8_t st) { uint8_t b[2] = { code, st }; notify(b, 2); }
static void notify_ack(uint32_t next) { uint8_t b[5] = { 0x82 }; put_u32(b + 1, next); notify(b, 5); }
static void notify_data_err(uint8_t st, uint32_t next)
{
  uint8_t b[6] = { 0x83, st }; put_u32(b + 2, next); notify(b, 6);
}

static void post(uint8_t type)
{
  OtaEvent ev = { type };
  xQueueSend(s_queue, &ev, 0);
}

static void start_advertising(void)
{
  if (!s_ble_allowed || s_locked || s_connected) return;
  BLEDevice::startAdvertising();
  s_advertising = true;
}

/* ---------------------------------------------------------------- tag check */
/* Parse "DC27FW|TYPE|0xHH|VER|" at p (len bytes available). Returns tag length or 0. */
static size_t parse_tag(const uint8_t *p, size_t len, char *type, size_t tcap, uint8_t *id)
{
  const size_t pre = 7;                               /* "DC27FW|" */
  if (len < pre || memcmp(p, "DC27FW|", pre) != 0) return 0;
  size_t i = pre, t = 0;
  while (i < len && p[i] != '|' && t + 1 < tcap) {
    if (p[i] < 'A' || p[i] > 'Z') return 0;
    type[t++] = (char)p[i++];
  }
  type[t] = 0;
  if (t == 0 || i + 6 > len || p[i] != '|' || p[i + 1] != '0' || p[i + 2] != 'x' || p[i + 5] != '|') return 0;
  char hex[3] = { (char)p[i + 3], (char)p[i + 4], 0 };
  if (!isxdigit((unsigned char)hex[0]) || !isxdigit((unsigned char)hex[1])) return 0;
  *id = (uint8_t)strtoul(hex, nullptr, 16);
  i += 6;
  size_t v = 0;
  while (i < len && p[i] != '|' && p[i] >= 0x20 && p[i] < 0x7f && v < 16) { i++; v++; }
  if (v == 0 || i >= len || p[i] != '|') return 0;
  return i + 1;
}

/* Scan the written image for a valid firmware tag. The search key "DC27FW|" also appears in
 * this code itself, so every hit is parsed and only a complete tag is accepted. */
static bool find_image_tag(const esp_partition_t *part, uint32_t size, char *type, size_t tcap, uint8_t *id)
{
  static uint8_t buf[1024 + 64];
  uint32_t pos = 0;
  while (pos < size) {
    uint32_t n = size - pos;
    if (n > sizeof(buf)) n = sizeof(buf);
    if (esp_partition_read(part, pos, buf, n) != ESP_OK) return false;
    for (uint32_t i = 0; i + 7 <= n; i++) {
      if (buf[i] == 'D' && parse_tag(buf + i, n - i, type, tcap, id)) return true;
    }
    if (pos + n >= size) break;
    pos += n - 64;                                     /* overlap: a tag is < 64 bytes */
  }
  return false;
}

/* ---------------------------------------------------------------- worker task */
static void ota_reset_state(void)
{
  s_active = false;
  s_block_busy = false;
  s_rx_offset = 0;
  s_block_len = 0;
}

static void ota_abort(void)
{
  if (s_handle) { esp_ota_abort(s_handle); s_handle = 0; }
  mbedtls_sha256_free(&s_sha);
  ota_reset_state();
}

static void ota_task(void *)
{
  OtaEvent ev;
  for (;;) {
    if (xQueueReceive(s_queue, &ev, portMAX_DELAY) != pdTRUE) continue;

    if (ev.type == EV_BEGIN) {
      s_part = esp_ota_get_next_update_partition(nullptr);
      if (!s_part || s_size > s_part->size) {
        ota_reset_state();
        notify_status(0x81, s_part ? OTA_ST_TOO_BIG : OTA_ST_BEGIN_FAIL);
        continue;
      }
      if (esp_ota_begin(s_part, OTA_WITH_SEQUENTIAL_WRITES, &s_handle) != ESP_OK) {
        s_handle = 0;
        ota_reset_state();
        notify_status(0x81, OTA_ST_BEGIN_FAIL);
        continue;
      }
      mbedtls_sha256_init(&s_sha);
      mbedtls_sha256_starts(&s_sha, 0);
      notify_status(0x81, OTA_ST_OK);
    }
    else if (ev.type == EV_BLOCK) {
      if (!s_handle) { s_block_busy = false; continue; }
      if (esp_ota_write(s_handle, s_block, s_block_len) != ESP_OK) {
        uint32_t at = s_block_start;
        ota_abort();
        notify_data_err(OTA_ST_WRITE_FAIL, at);
        continue;
      }
      mbedtls_sha256_update(&s_sha, s_block, s_block_len);
      uint32_t next = s_block_start + s_block_len;
      s_block_start = next;
      s_block_len = 0;
      s_block_busy = false;
      notify_ack(next);
    }
    else if (ev.type == EV_END) {
      uint8_t st = OTA_ST_OK;
      uint8_t sha[32];
      if (!s_handle) st = OTA_ST_NOT_STARTED;
      else if (s_block_start != s_size || s_block_len != 0) st = OTA_ST_SIZE;
      if (st == OTA_ST_OK) {
        mbedtls_sha256_finish(&s_sha, sha);
        if (memcmp(sha, s_sha_expect, 32) != 0) st = OTA_ST_SHA;
      }
      if (st == OTA_ST_OK) {
        esp_err_t e = esp_ota_end(s_handle);            /* ESP image check (header, segments, hash) */
        s_handle = 0;
        if (e != ESP_OK) st = OTA_ST_IMAGE;
      }
      if (st == OTA_ST_OK) {
        char type[12]; uint8_t id = 0;
        if (!find_image_tag(s_part, s_size, type, sizeof(type), &id)) st = OTA_ST_NO_TAG;
        else if (strcmp(type, FW_TYPE) != 0) st = OTA_ST_WRONG_TYPE;
        else if (id != (uint8_t)DEVICE_ID && !s_force_id) st = OTA_ST_WRONG_ID;
      }
      if (st == OTA_ST_OK && esp_ota_set_boot_partition(s_part) != ESP_OK) st = OTA_ST_SET_BOOT;

      if (st == OTA_ST_OK) {
        mbedtls_sha256_free(&s_sha);
        notify_status(0x84, OTA_ST_OK);
        s_reboot_at = millis() + 700;                   /* let the notification go out */
        s_reboot = true;
      } else {
        ota_abort();
        notify_status(0x84, st);
      }
    }
    else if (ev.type == EV_ABORT) {
      ota_abort();
    }
  }
}

/* ---------------------------------------------------------------- GATT callbacks (host task) */
class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer *, ble_gap_conn_desc *desc) override
  {
    s_conn_handle = desc->conn_handle;
    s_connected = true;
    s_advertising = false;
  }
  void onDisconnect(BLEServer *, ble_gap_conn_desc *) override
  {
    s_connected = false;
    s_authed = false;
    if (s_active) post(EV_ABORT);                      /* link lost: drop the half-written image */
    start_advertising();
  }
};

/* PIN check shared by AUTH and BEGIN. 3 wrong PINs -> locked until power cycle. */
static uint8_t check_pin(const uint8_t *pin, size_t len)
{
  if (s_locked) return OTA_ST_LOCKED;
  if (len != strlen(BLE_OTA_PIN) || memcmp(pin, BLE_OTA_PIN, len) != 0) {
    if (++s_pin_fail >= OTA_MAX_PIN_TRIES) { s_locked = true; return OTA_ST_LOCKED; }
    return OTA_ST_BAD_PIN;
  }
  s_pin_fail = 0;
  s_authed = true;
  return OTA_ST_OK;
}

class CtrlCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override
  {
    const uint8_t *d = c->getData();
    size_t n = c->getLength();
    if (n < 1) return;

    if (d[0] == 0x01) {                                 /* BEGIN */
      if (s_locked) { notify_status(0x81, OTA_ST_LOCKED); return; }
      if (s_active || s_reboot) { notify_status(0x81, OTA_ST_BUSY); return; }
      if (n < 39 || n < (size_t)(39 + d[38])) { notify_status(0x81, OTA_ST_BAD_CMD); return; }
      const uint8_t pst = check_pin(d + 39, d[38]);
      if (pst != OTA_ST_OK) { notify_status(0x81, pst); return; }
      s_size = get_u32(d + 1);
      memcpy(s_sha_expect, d + 5, 32);
      s_force_id = (d[37] & 0x01) != 0;
      if (s_size == 0) { notify_status(0x81, OTA_ST_BAD_CMD); return; }
      s_rx_offset = 0;
      s_block_start = 0;
      s_block_len = 0;
      s_block_busy = false;
      s_active = true;
      post(EV_BEGIN);
    }
    else if (d[0] == 0x02) {                            /* END */
      if (!s_active) { notify_status(0x84, OTA_ST_NOT_STARTED); return; }
      post(EV_END);
    }
    else if (d[0] == 0x03) {                            /* ABORT */
      if (s_active) post(EV_ABORT);
    }
    else if (d[0] == 0x04) {                            /* AUTH: unlock the HOST characteristic */
      if (n < 2 || n < (size_t)(2 + d[1])) { notify_status(0x85, OTA_ST_BAD_CMD); return; }
      notify_status(0x85, check_pin(d + 2, d[1]));
    }
  }
};

class DataCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override
  {
    const uint8_t *d = c->getData();
    size_t n = c->getLength();
    if (!s_active || n < 5) return;
    uint32_t off = get_u32(d);
    d += 4; n -= 4;
    if (s_block_busy || off != s_rx_offset || s_block_len + n > OTA_BLOCK || off + n > s_size) {
      notify_data_err(OTA_ST_OFFSET, s_block_start);   /* PC resends the whole block */
      s_rx_offset = s_block_start;
      s_block_len = 0;
      return;
    }
    memcpy(s_block + s_block_len, d, n);
    s_block_len += n;
    s_rx_offset += n;
    if (s_block_len == OTA_BLOCK || s_rx_offset == s_size) {
      s_block_busy = true;
      post(EV_BLOCK);
    }
  }
};

/* HOST: the same 1-byte commands as the USB port (0x00 ID request, 0x01..0x28 effects).
 * Handled in loop() (the effect engine is not thread safe); the ID answer comes back as a
 * notification on HOST. Ignored until AUTH. */
class HostCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override
  {
    if (!s_authed) return;
    const uint8_t *d = c->getData();
    for (size_t i = 0; i < c->getLength(); i++) xQueueSend(s_host_q, &d[i], 0);
  }
};

/* ---------------------------------------------------------------- public */
bool ble_ota_host_read(uint8_t *b)
{
  return s_host_q && xQueueReceive(s_host_q, b, 0) == pdTRUE;
}

void ble_ota_host_reply(uint8_t b)
{
  if (!s_host || !s_connected) return;
  s_host->setValue(&b, 1);
  s_host->notify();
}

void ble_ota_init(void)
{
  s_queue = xQueueCreate(8, sizeof(OtaEvent));
  s_host_q = xQueueCreate(64, sizeof(uint8_t));
  xTaskCreate(ota_task, "ble_ota", 6144, nullptr, 2, nullptr);

  BLEDevice::init(kBleName);
  s_server = BLEDevice::createServer();
  s_server->setCallbacks(new ServerCb());

  BLEService *svc = s_server->createService(OTA_SVC_UUID);
  BLECharacteristic *info = svc->createCharacteristic(OTA_INFO_UUID, BLECharacteristic::PROPERTY_READ);
  info->setValue(kFwTag);
  s_ctrl = svc->createCharacteristic(OTA_CTRL_UUID,
             BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  s_ctrl->setCallbacks(new CtrlCb());
  BLECharacteristic *data = svc->createCharacteristic(OTA_DATA_UUID, BLECharacteristic::PROPERTY_WRITE_NR);
  data->setCallbacks(new DataCb());
  s_host = svc->createCharacteristic(OTA_HOST_UUID,
             BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR | BLECharacteristic::PROPERTY_NOTIFY);
  s_host->setCallbacks(new HostCb());
  svc->start();

  /* name in the advertising packet, service UUID in the scan response (both do not fit in 31 B) */
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  BLEAdvertisementData ad, sr;
  ad.setFlags(0x06);
  ad.setName(kBleName);
  sr.setCompleteServices(BLEUUID(OTA_SVC_UUID));
  adv->setAdvertisementData(ad);
  adv->setScanResponseData(sr);
  /* advertising starts from ble_ota_poll() once the USB port is seen to be out */
}

void ble_ota_poll(void)
{
  if (s_reboot && (int32_t)(millis() - s_reboot_at) >= 0) {
    esp_restart();
  }

  /* USB plug state from the USB-Serial-JTAG SOF frames (the PC sends one every 1 ms while the
   * port is powered and enumerated). Debounced: the reading can flap for a few ms. */
  static bool     last_raw = true;
  static uint32_t since    = 0;
  const bool raw = HWCDC::isPlugged();
  const uint32_t now = millis();
  if (raw != last_raw) { last_raw = raw; since = now; }
  if (!raw && !s_ble_allowed && now - since >= USB_OUT_MS) {
    s_ble_allowed = true;                              /* USB out -> BLE on */
    start_advertising();
  } else if (raw && s_ble_allowed && now - since >= USB_IN_MS) {
    s_ble_allowed = false;                             /* USB back -> BLE off */
  }

  /* USB connected: USB has priority -> stop advertising, drop the BLE link, a running transfer
   * is aborted (the old firmware stays). Locked (wrong PIN): same, except a transfer. */
  if (!s_ble_allowed || s_locked) {
    if (s_advertising) { BLEDevice::stopAdvertising(); s_advertising = false; }
    if (s_connected && !s_reboot && (!s_active || !s_ble_allowed)) {
      if (s_active) post(EV_ABORT);
      s_server->disconnect(s_conn_handle);
      s_connected = false;
    }
  }
}

/* Mark a freshly updated image valid only after setup() has run (see main.cpp). A crash before
 * that makes the bootloader roll back to the previous firmware. */
extern "C" bool verifyRollbackLater(void) { return true; }
