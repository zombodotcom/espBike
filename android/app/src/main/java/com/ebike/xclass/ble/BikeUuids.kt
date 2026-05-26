package com.ebike.xclass.ble

import java.util.UUID

/**
 * BLE contract — must match main/main.c on the ESP32 exactly.
 *
 * Service 0xEB1C. All characteristics are READ + NOTIFY, little-endian.
 * Advertised name: "EBIKE-X-CLASS" with a stable public address (CompanionDeviceManager
 * cannot filter on random addresses).
 */
object BikeUuids {
    const val DEVICE_NAME = "EBIKE-X-CLASS"

    private fun u16(short: Int): UUID =
        UUID.fromString(String.format("0000%04x-0000-1000-8000-00805f9b34fb", short))

    val SERVICE: UUID = u16(0xEB1C)

    val RAW      = u16(0xEB0A)   // byte[<=64]  raw frame (protocol bring-up)
    val VOLTAGE  = u16(0xEB01)   // uint16 mV   APPROX (nominal)
    val CURRENT  = u16(0xEB02)   // int32  mA   (33A > int16)
    val POWER    = u16(0xEB03)   // int16  W    APPROX
    val SPEED    = u16(0xEB04)   // uint16 mph*100
    val CADENCE  = u16(0xEB05)   // uint8  rpm
    val PAS      = u16(0xEB06)   // uint8       (needs display-TX tap; usually 0)
    val THROTTLE = u16(0xEB07)   // uint8  %    (needs display-TX tap; usually 0)
    val BRAKE    = u16(0xEB08)   // uint8  0/1
    val ERROR    = u16(0xEB09)   // uint8
    val TEMP     = u16(0xEB0B)   // uint8  °C
    val WH       = u16(0xEB0C)   // uint32 mWh  cumulative, APPROX
    val SOC      = u16(0xEB0D)   // uint8  raw bar level — the real battery gauge

    /** Client Characteristic Configuration Descriptor — write to enable notifications. */
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    /** Every notify/read characteristic we subscribe to. */
    val ALL: List<UUID> = listOf(
        RAW, VOLTAGE, CURRENT, POWER, SPEED, CADENCE,
        PAS, THROTTLE, BRAKE, ERROR, TEMP, WH, SOC,
    )
}
