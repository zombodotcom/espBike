/*
 * ebike_ble_bridge — ESP32 firmware
 *
 * Passively sniffs the controller->display UART line on an Ariel Rider X-Class,
 * decodes telemetry, and rebroadcasts it as BLE GATT notifications.
 *
 * Bike:        Ariel Rider X-Class 52V (pre-2024, single rear hub motor)
 * Motor:       Bafang 1000W geared rear hub
 * Battery:     52V x 20Ah = 1040 Wh pack
 * Tires:       CST 20" x 4.0" fat
 * Controller:  Lishui LSW7765-99E (52V, 33A peak, 16A rated, 41V LVC, 05/2022)
 * Display:     APT 500S-U
 *
 * PROTOCOL — read this before trusting any decoded value.
 * The controller is a Lishui unit. Lishui controllers speak a KM5S / KingMeter /
 * Kunteng-style display protocol at *9600 baud* (NOT Bafang UART @ 1200 — a Bafang
 * 600C display throws a "30H" comm error on the X-Class, proving it is not native
 * Bafang). The exact dialect is confirmed by the first byte of each frame at 9600:
 *     0x41 -> Kunteng / KT-LCD3   (12-byte frame)
 *     0x46 -> KingMeter 618U      (8-byte frame)
 *     0x3A -> KM5S / 901U         (variable, ends 0x0D 0x0A)
 *     0x02 -> No.2 / China S866   (14-byte frame; common on APT/Lishui)
 * If the raw dump is garbage at 9600, first try UART_INVERT_RX 1 (some Lishui/No.2
 * units invert the line), then recompile at UART_BAUD 1200 (unlikely Bafang case).
 *
 * HONEST LIMITS of a display-line tap (single controller-TX wire):
 *   - Battery is reported as a COARSE SOC/bar level + a nominal-voltage byte, never
 *     a finely-scaled pack voltage. The "voltage" characteristic is therefore
 *     approximate; the SOC characteristic is the real battery indicator.
 *   - Power and Wh are approximate (nominal voltage x real current).
 *   - PAS level and throttle travel on the display->controller direction, not the
 *     controller-TX line we tap, so they stay 0 here. Capturing them needs a second
 *     tap on display-TX into a second UART (see README, out of scope for v1).
 *   - Real current, wheel speed, error, brake, and motor temp ARE on this line.
 *
 * Wiring (read-only passive sniff):
 *   Controller TX wire -> 10k -> ESP32 GPIO16 -> 20k -> GND   (divider if line is 5V;
 *                                                              many are ~3.3V — measure)
 *   Bike GND ------------------ ESP32 GND
 *   5V from buck converter ---- ESP32 5V (or USB during bench testing)
 *
 * Build / flash / monitor (ESP-IDF v6.0):
 *   idf.py set-target esp32     # or esp32c3
 *   idf.py build
 *   idf.py -p /dev/ttyUSB0 flash monitor   (Windows: -p COMx)
 *
 * BLE service: 0000eb1c-0000-1000-8000-00805f9b34fb
 *   Char 0xEB0A — Raw hex frame (up to 64 bytes), notify   <- confirm format here first
 *   Char 0xEB01 — Pack voltage   uint16 mV       notify  (APPROX: nominal, coarse)
 *   Char 0xEB02 — Pack current   int32  mA       notify  (int32: 33A > int16 range)
 *   Char 0xEB03 — Power          int16  W        notify  (APPROX)
 *   Char 0xEB04 — Speed          uint16 mph*100  notify
 *   Char 0xEB05 — Cadence        uint8  rpm      notify  (often unavailable)
 *   Char 0xEB06 — PAS level      uint8           notify  (needs display-TX tap)
 *   Char 0xEB07 — Throttle       uint8  %        notify  (needs display-TX tap)
 *   Char 0xEB08 — Brake          uint8  0/1      notify
 *   Char 0xEB09 — Error code     uint8           notify
 *   Char 0xEB0B — Motor temp     uint8 °C        notify  (if motor has a thermistor)
 *   Char 0xEB0C — Wh used        uint32 mWh      notify  (cumulative, APPROX)
 *   Char 0xEB0D — Battery SOC    uint8           notify  (raw bar level — real gauge)
 *
 * Connect from phone with nRF Connect to bring up the protocol: subscribe to 0xEB0A
 * and read the first byte of each frame to confirm dialect + baud.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define TAG "EBIKE"

/* ---------- UART ---------- */
#define UART_PORT        UART_NUM_2
#define UART_RX_PIN      16
#define UART_TX_PIN      UART_PIN_NO_CHANGE  /* passive sniff, no TX */
#define UART_BAUD        9600                /* Lishui/KM5S family; try 1200 if garbage */
#define UART_INVERT_RX   0                   /* set 1 if 9600 yields garbage (some invert) */
#define UART_BUF_SIZE    1024
#define FRAME_MAX        64
#define FRAME_GAP_MS     8    /* idle gap that ends a frame (~7.5 char-times @ 9600) */

/* ---------- BLE ---------- */
#define DEVICE_NAME      "EBIKE-X-CLASS"

#define UUID16(x) BLE_UUID16_DECLARE(x)

static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;

/* Handles populated by NimBLE at registration */
static uint16_t h_raw, h_v, h_i, h_w, h_spd, h_cad, h_pas, h_thr, h_brk, h_err,
                h_temp, h_wh, h_soc;

/* Latest parsed values — single writer (uart_task), atomic-width stores */
static volatile uint16_t s_voltage_mv = 0;   /* APPROX nominal, see header */
static volatile int32_t  s_current_ma = 0;   /* int32: 33A rated = 33000mA > int16 */
static volatile int16_t  s_power_w    = 0;
static volatile uint16_t s_speed_x100 = 0;
static volatile uint8_t  s_cadence    = 0;
static volatile uint8_t  s_pas        = 0;
static volatile uint8_t  s_throttle   = 0;
static volatile uint8_t  s_brake      = 0;
static volatile uint8_t  s_error      = 0;
static volatile uint8_t  s_motor_temp = 0;   /* °C from motor thermistor */
static volatile uint32_t s_wh_used_mwh = 0;  /* accumulated energy, mWh (APPROX) */
static volatile uint8_t  s_soc        = 0;   /* raw battery bar/level from frame */
static int64_t s_last_power_us = 0;          /* timestamp of last power sample */

/* Pack nominal used for the APPROX power/Wh math (display line gives no live V). */
#define NOMINAL_V_MV   52000
#define PACK_WH        1040
#define WHEEL_CIRC_MM  1620   /* 20" x 4.0" fat tire ~1620 mm; measure by rolling one
                                 revolution with a chalk mark for best accuracy */

/* ---------- Forward decls ---------- */
static void ble_app_advertise(void);
static int  gap_event_cb(struct ble_gap_event *e, void *arg);

/* ---------- GATT access callbacks ----------
 * Read returns the last cached value; notifications drive updates.
 * v6.0 builds with -Werror=unused-parameter, so mark the unused NimBLE args. */
static int chr_read_u8(uint16_t conn_h, uint16_t attr_h,
                       struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_h; (void)attr_h;
    uint8_t *v = (uint8_t *)arg;
    return os_mbuf_append(ctxt->om, v, sizeof(*v))
           ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}
static int chr_read_u16(uint16_t c, uint16_t a,
                        struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)c; (void)a;
    uint16_t *v = (uint16_t *)arg;
    return os_mbuf_append(ctxt->om, v, sizeof(*v))
           ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}
static int chr_read_u32(uint16_t c, uint16_t a,
                        struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)c; (void)a;
    uint32_t *v = (uint32_t *)arg;
    return os_mbuf_append(ctxt->om, v, sizeof(*v))
           ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}
static int chr_read_i16(uint16_t c, uint16_t a,
                        struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)c; (void)a;
    int16_t *v = (int16_t *)arg;
    return os_mbuf_append(ctxt->om, v, sizeof(*v))
           ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
}
static int chr_read_raw(uint16_t c, uint16_t a,
                        struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)c; (void)a; (void)ctxt; (void)arg;
    /* Raw frame is only meaningful as a notification; plain read returns nothing. */
    return 0;
}

/* ---------- GATT service definition ---------- */
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = UUID16(0xEB1C),
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = UUID16(0xEB0A), .access_cb = chr_read_raw,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &h_raw },
            { .uuid = UUID16(0xEB01), .access_cb = chr_read_u16,
              .arg = (void*)&s_voltage_mv,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &h_v },
            { .uuid = UUID16(0xEB02), .access_cb = chr_read_u32,
              .arg = (void*)&s_current_ma,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &h_i },
            { .uuid = UUID16(0xEB03), .access_cb = chr_read_i16,
              .arg = (void*)&s_power_w,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &h_w },
            { .uuid = UUID16(0xEB04), .access_cb = chr_read_u16,
              .arg = (void*)&s_speed_x100,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &h_spd },
            { .uuid = UUID16(0xEB05), .access_cb = chr_read_u8,
              .arg = (void*)&s_cadence,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &h_cad },
            { .uuid = UUID16(0xEB06), .access_cb = chr_read_u8,
              .arg = (void*)&s_pas,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &h_pas },
            { .uuid = UUID16(0xEB07), .access_cb = chr_read_u8,
              .arg = (void*)&s_throttle,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &h_thr },
            { .uuid = UUID16(0xEB08), .access_cb = chr_read_u8,
              .arg = (void*)&s_brake,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &h_brk },
            { .uuid = UUID16(0xEB09), .access_cb = chr_read_u8,
              .arg = (void*)&s_error,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &h_err },
            { .uuid = UUID16(0xEB0B), .access_cb = chr_read_u8,
              .arg = (void*)&s_motor_temp,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &h_temp },
            { .uuid = UUID16(0xEB0C), .access_cb = chr_read_u32,
              .arg = (void*)&s_wh_used_mwh,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &h_wh },
            { .uuid = UUID16(0xEB0D), .access_cb = chr_read_u8,
              .arg = (void*)&s_soc,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &h_soc },
            { 0 },
        },
    },
    { 0 },
};

/* ---------- BLE notification helpers ---------- */
static void notify_bytes(uint16_t handle, const void *data, size_t len) {
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om) ble_gatts_notify_custom(conn_handle, handle, om);
}

/* ---------- decode helpers ---------- */

/* Convert a wheel-revolution period in ms to mph*100. */
static uint16_t period_ms_to_mph_x100(uint16_t period_ms) {
    if (period_ms == 0) return 0;
    /* mm/h = circ_mm * 3600 * 1000 / period_ms ; mph*100 = (mm/h) / 16093.
       uint64: WHEEL_CIRC_MM*3600000 ~5.8e9 overflows uint32. */
    uint64_t mmh = (uint64_t)WHEEL_CIRC_MM * 3600000u / period_ms;
    return (uint16_t)(mmh / 16093u);
}

/* Update derived power + integrate energy from real current and nominal voltage. */
static void update_power_and_wh(int32_t current_ma) {
    /* int64 intermediate: 52000mV * ~85000mA overflows int32. */
    int16_t p_w = (int16_t)(((int64_t)s_voltage_mv * current_ma) / 1000000);
    s_power_w = p_w;
    notify_bytes(h_w, (void*)&s_power_w, 2);

    int64_t now = esp_timer_get_time();
    if (s_last_power_us != 0 && p_w > 0) {
        int64_t dt_us = now - s_last_power_us;
        /* mWh = W * (dt_us / 3.6e9) * 1000 = W * dt_us / 3.6e6 */
        uint32_t add_mwh = (uint32_t)((int64_t)p_w * dt_us / 3600000);
        s_wh_used_mwh += add_mwh;
        notify_bytes(h_wh, (void*)&s_wh_used_mwh, 4);
    }
    s_last_power_us = now;
}

static void push_common(uint16_t speed_x100, int32_t current_ma,
                        uint8_t soc, uint8_t err) {
    s_voltage_mv = NOMINAL_V_MV;   /* APPROX — display line carries no live volts */
    s_current_ma = current_ma;
    s_speed_x100 = speed_x100;
    s_soc        = soc;
    s_error      = err;
    notify_bytes(h_v,   (void*)&s_voltage_mv, 2);
    notify_bytes(h_i,   (void*)&s_current_ma, 4);
    notify_bytes(h_spd, (void*)&s_speed_x100, 2);
    notify_bytes(h_soc, (void*)&s_soc,        1);
    notify_bytes(h_err, (void*)&s_error,      1);
    update_power_and_wh(current_ma);
}

/* 0x41 — Kunteng / KT-LCD3, 12-byte controller->display frame.
 * Ref: stancecoke/BMSBattery_S_controllers_firmware Src/display.c
 *   [1] SOC bars (3=empty,4,8,12,16=full)   [2] nominal-voltage byte
 *   [3..4] wheel period ms big-endian        [5] error
 *   [7] mode flags (brake = bit5)            [8] current in 0.25 A units
 *   [9] motor temp (value-15 °C)             [6] CRC (XOR, KT quirk around byte5)
 */
static void decode_kt(const uint8_t *f, size_t n) {
    if (n < 10) return;
    uint16_t period = ((uint16_t)f[3] << 8) | f[4];
    int32_t  cur_ma = (int32_t)f[8] * 250;              /* 0.25 A -> mA */
    uint8_t  brake  = (f[7] & 0x20) ? 1 : 0;
    uint8_t  temp   = (f[9] > 15) ? (uint8_t)(f[9] - 15) : 0;

    s_brake = brake;          notify_bytes(h_brk,  (void*)&s_brake, 1);
    s_motor_temp = temp;      notify_bytes(h_temp, (void*)&s_motor_temp, 1);
    push_common(period_ms_to_mph_x100(period), cur_ma, f[1], f[5]);
}

/* 0x46 — KingMeter 618U, 8-byte controller->display frame.
 * Ref: EBiCS/EBiCS_Firmware Src/display_kingmeter.c (KM_618U_Service)
 *   [1] battery status (0 low / 1 normal)    [2] current (*3/10 => 1/3 A units)
 *   [3..4] wheel period ms big-endian         [6] error   [7] XOR of [1..6]
 */
static void decode_km618u(const uint8_t *f, size_t n) {
    if (n < 7) return;
    uint16_t period = ((uint16_t)f[3] << 8) | f[4];
    /* on-wire value v satisfies v = Current_x10 * 3 / 10, so A*10 = v*10/3,
       mA = v * 1000 / 3 */
    int32_t  cur_ma = ((int32_t)f[2] * 1000) / 3;
    push_common(period_ms_to_mph_x100(period), cur_ma, f[1], f[6]);
}

/* 0x3A — KM5S / 901U, variable-length, terminated 0x0D 0x0A.
 * Ref: EBiCS/EBiCS_Firmware Src/display_kingmeter.c (KM_901U_Service)
 *   [4] state/battery (low = bit 0x40)        [5] current (1/3 A units)
 *   [6..7] wheel period ms big-endian          [8] error  (16-bit additive cksum)
 */
static void decode_km5s(const uint8_t *f, size_t n) {
    if (n < 9) return;
    uint16_t period = ((uint16_t)f[6] << 8) | f[7];
    int32_t  cur_ma = ((int32_t)f[5] * 1000) / 3;
    uint8_t  soc    = (f[4] & 0x40) ? 0 : 1;   /* coarse: low flag -> 0, else "ok" */
    push_common(period_ms_to_mph_x100(period), cur_ma, soc, f[8]);
}

/* 0x02 — "No.2 / China" protocol (S866/SW900 family, common on APT/Lishui).
 * Ref: EBiCS/EBiCS_Firmware Src/display_No_2.c — controller->display, 14 bytes:
 *   [3] error  [4] brake (bit5)  [6..7] current 0.1 A units, big-endian
 *   [8..9] wheel period ms, big-endian  [13] XOR checksum of preceding bytes.
 * No distinct SOC byte in this layout, so battery bars report 0 (unknown) here. */
static void decode_no2(const uint8_t *f, size_t n) {
    if (n < 10) return;
    uint16_t period = ((uint16_t)f[8] << 8) | f[9];
    int32_t  cur_ma = (((int32_t)f[6] << 8) | f[7]) * 100;   /* 0.1 A -> mA */
    uint8_t  brake  = (f[4] & 0x20) ? 1 : 0;
    s_brake = brake; notify_bytes(h_brk, (void*)&s_brake, 1);
    push_common(period_ms_to_mph_x100(period), cur_ma, 0, f[3]);
}

/* Error byte values (forwarded raw on char 0xEB09), per APT 500S datasheet §9:
 *   0x01 normal        0x09 motor phase error    0x13 battery temp sensor err
 *   0x03 brake signal  0x10 controller over-temp 0x14 motor temp sensor err
 *   0x04 throttle high 0x11 motor over-temp      0x21 speed sensor err
 *   0x06 low-volt prot 0x12 current sensor err   0x22 BMS comm err
 *   0x07 high-volt prot                          0x30 communication error
 *   0x08 motor hall err
 * We forward the raw byte; the phone app maps it to text. */

/* ---------- frame parser ----------
 * Always emit the raw frame first (this is how we confirm format on the phone),
 * then dispatch on the header byte. Unknown headers fall through with raw forwarded. */
static void parse_frame(const uint8_t *f, size_t n) {
    if (n < 3) return;

    notify_bytes(h_raw, f, n > FRAME_MAX ? FRAME_MAX : n);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, f, n, ESP_LOG_INFO);

    switch (f[0]) {
    case 0x41: decode_kt(f, n);     break;
    case 0x46: decode_km618u(f, n); break;
    case 0x3A: decode_km5s(f, n);   break;
    case 0x02: decode_no2(f, n);    break;
    default:   /* unknown dialect — raw already forwarded for bring-up */ break;
    }
}

/* ---------- UART sniffer task ----------
 * Accumulates bytes into a frame buffer; a gap of FRAME_GAP_MS between bytes is
 * treated as a frame boundary. */
static void uart_task(void *arg) {
    (void)arg;
    uint8_t buf[FRAME_MAX];
    size_t  bi = 0;
    int64_t last_byte_us = 0;

    while (1) {
        uint8_t b;
        int r = uart_read_bytes(UART_PORT, &b, 1, pdMS_TO_TICKS(5));
        int64_t now = esp_timer_get_time();

        if (r == 1) {
            if (bi > 0 && (now - last_byte_us) > FRAME_GAP_MS * 1000) {
                parse_frame(buf, bi);
                bi = 0;
            }
            if (bi < FRAME_MAX) buf[bi++] = b;
            last_byte_us = now;
        } else if (bi > 0 && (now - last_byte_us) > FRAME_GAP_MS * 1000) {
            parse_frame(buf, bi);
            bi = 0;
        }
    }
}

/* ---------- NimBLE plumbing ---------- */
static void on_sync(void) {
    /* Use the PUBLIC (stable) device address. Android's CompanionDeviceManager has
       no support for random/resolvable addresses, so the app filters/associates on
       a stable identifier. ensure_addr(0) selects the public/hardware address. */
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    ble_app_advertise();
}

static void on_reset(int reason) {
    ESP_LOGW(TAG, "BLE reset; reason=%d", reason);
}

static void ble_app_advertise(void) {
    struct ble_hs_adv_fields fields = {0};
    const char *name = ble_svc_gap_device_name();

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc) { ESP_LOGE(TAG, "adv_set_fields=%d", rc); return; }

    struct ble_gap_adv_params adv = {0};
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv, gap_event_cb, NULL);
    if (rc) ESP_LOGE(TAG, "adv_start=%d", rc);
}

static int gap_event_cb(struct ble_gap_event *e, void *arg) {
    (void)arg;
    switch (e->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (e->connect.status == 0) {
            conn_handle = e->connect.conn_handle;
            ESP_LOGI(TAG, "BLE connected");
        } else {
            ble_app_advertise();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE disconnected");
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ble_app_advertise();
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "subscribe handle=%d notify=%d",
                 e->subscribe.attr_handle, e->subscribe.cur_notify);
        return 0;
    default:
        return 0;
    }
}

static void nimble_host_task(void *param) {
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ---------- UART init ---------- */
static void uart_init(void) {
    uart_config_t cfg = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
#if UART_INVERT_RX
    ESP_ERROR_CHECK(uart_set_line_inverse(UART_PORT, UART_SIGNAL_RXD_INV));
#endif
    ESP_LOGI(TAG, "UART2 up @ %d baud on GPIO%d (invert_rx=%d)",
             UART_BAUD, UART_RX_PIN, UART_INVERT_RX);
}

/* ---------- app_main ---------- */
void app_main(void) {
    /* NVS — required by NimBLE for bonding storage */
    esp_err_t rc = nvs_flash_init();
    if (rc == ESP_ERR_NVS_NO_FREE_PAGES || rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* NimBLE stack */
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_ERROR_CHECK(ble_gatts_count_cfg(gatt_svcs));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(gatt_svcs));
    ESP_ERROR_CHECK(ble_svc_gap_device_name_set(DEVICE_NAME));
    nimble_port_freertos_init(nimble_host_task);

    /* UART sniffer */
    uart_init();
    xTaskCreate(uart_task, "uart_task", 4096, NULL, 10, NULL);

    ESP_LOGI(TAG, "ebike_ble_bridge online");
}
