package com.ebike.xclass.ble

/**
 * Snapshot of the latest decoded telemetry. Units mirror the firmware's
 * characteristics; derived/display values are computed in helpers below.
 */
data class BikeState(
    val connected: Boolean = false,
    val voltageMv: Int = 0,      // APPROX (nominal)
    val currentMa: Int = 0,      // signed; negative = regen
    val powerW: Int = 0,         // APPROX
    val speedX100: Int = 0,      // mph * 100
    val cadenceRpm: Int = 0,
    val pas: Int = 0,
    val throttlePct: Int = 0,
    val brake: Boolean = false,
    val errorCode: Int = 0,
    val motorTempC: Int = 0,
    val whUsedMilli: Long = 0,   // mWh cumulative
    val socRaw: Int = 0,         // raw bar level from frame
    val rawFrameHex: String = "",
) {
    val speedMph: Float get() = speedX100 / 100f
    val currentA: Float get() = currentMa / 1000f
    val whUsed: Float get() = whUsedMilli / 1000f
    /** Honest "battery used" gauge: Wh consumed against the 1040 Wh pack. */
    val packUsedFraction: Float get() = (whUsed / PACK_WH).coerceIn(0f, 1f)

    companion object {
        const val PACK_WH = 1040f
    }
}
