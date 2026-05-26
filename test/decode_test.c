/*
 * Host unit test for the protocol decode math in main/main.c.
 *
 * QEMU cannot exercise the BLE radio, and the decode arithmetic (speed/current
 * scaling, big-endian period extraction, brake bit, temp offset) is the part with
 * real correctness risk. These functions mirror main.c exactly; build & run on host:
 *
 *   gcc -Wall -Wextra -Werror -o decode_test test/decode_test.c && ./decode_test
 *
 * Keep these in sync with main/main.c if the byte maps change.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define WHEEL_CIRC_MM  1620
#define NOMINAL_V_MV   52000

/* --- copies of the formulas under test (must match main/main.c) --- */
static uint16_t period_ms_to_mph_x100(uint16_t period_ms) {
    if (period_ms == 0) return 0;
    uint64_t mmh = (uint64_t)WHEEL_CIRC_MM * 3600000u / period_ms;
    return (uint16_t)(mmh / 16093u);
}
static int32_t kt_current_ma(uint8_t b)      { return (int32_t)b * 250; }
static int32_t km_current_ma(uint8_t b)      { return ((int32_t)b * 1000) / 3; }
static int16_t power_w(uint16_t v_mv, int32_t i_ma) {
    return (int16_t)(((int64_t)v_mv * i_ma) / 1000000);
}
static uint16_t be16(uint8_t hi, uint8_t lo) { return ((uint16_t)hi << 8) | lo; }

static int fails = 0;
#define CHECK_EQ(label, got, want) do {                                   \
    long g = (long)(got), w = (long)(want);                               \
    if (g != w) { printf("FAIL %-28s got=%ld want=%ld\n", label, g, w);   \
                  fails++; }                                              \
    else        { printf("ok   %-28s = %ld\n", label, g); }               \
} while (0)

int main(void) {
    /* speed: period of one 20x4" revolution */
    CHECK_EQ("speed period=0",        period_ms_to_mph_x100(0),     0);     /* stopped */
    CHECK_EQ("speed period=242ms",    period_ms_to_mph_x100(242),   1497);  /* ~14.97 mph */
    CHECK_EQ("speed period=121ms",    period_ms_to_mph_x100(121),   2994);  /* ~29.94 mph */

    /* current scaling */
    CHECK_EQ("KT current f8=20 (0.25A)", kt_current_ma(20),   5000);   /* 5.0 A */
    CHECK_EQ("KT current f8=132",        kt_current_ma(132),  33000);  /* 33 A (max rated) */
    CHECK_EQ("KM current b=30 (1/3A)",   km_current_ma(30),   10000);  /* 10.0 A */
    CHECK_EQ("KM current b=3",           km_current_ma(3),    1000);   /* 1.0 A */

    /* power = nominal V x current */
    CHECK_EQ("power 52V x 10A",  power_w(NOMINAL_V_MV, 10000), 520);
    CHECK_EQ("power 52V x 20A",  power_w(NOMINAL_V_MV, 20000), 1040);

    /* big-endian wheel-period extraction from a sample KT frame */
    uint8_t kt[12] = {0x41, 0x0C, 0x34, 0x00, 0xF2, 0x00, 0x00, 0x20, 20, 55, 0,0};
    CHECK_EQ("KT period be16[3..4]", be16(kt[3], kt[4]), 242);
    CHECK_EQ("KT brake bit (f7&0x20)", (kt[7] & 0x20) ? 1 : 0, 1);
    CHECK_EQ("KT motor temp (f9-15)",  (kt[9] > 15) ? kt[9] - 15 : 0, 40);
    CHECK_EQ("KT soc byte f1",         kt[1], 0x0C);

    if (fails) { printf("\n%d CHECK(S) FAILED\n", fails); return 1; }
    printf("\nall decode-math checks passed\n");
    return 0;
}
