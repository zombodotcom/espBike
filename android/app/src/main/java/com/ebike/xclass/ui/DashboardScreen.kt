package com.ebike.xclass.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.ebike.xclass.ble.BikeRepository

/**
 * Surfaces what the display-line tap actually provides: live watts, speed, the honest
 * Wh-used gauge (not the lying voltage bar), current, motor temp, error. Voltage is
 * shown as approximate. PAS/throttle stay 0 without a second tap.
 */
@Composable
fun DashboardScreen(onPair: () -> Unit) {
    val s by BikeRepository.state.collectAsStateWithLifecycle()

    Column(
        modifier = Modifier.fillMaxWidth().padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(if (s.connected) "Connected" else "Not connected",
            style = MaterialTheme.typography.labelLarge)

        Text("${s.powerW} W", fontSize = 72.sp, fontWeight = FontWeight.Bold)

        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
            Stat("%.1f".format(s.speedMph), "mph")
            Stat("%.1f".format(s.currentA), "A")
            Stat("${s.motorTempC}", "°C")
        }

        Text("Battery used: %.0f / 1040 Wh".format(s.whUsed))
        LinearProgressIndicator(
            progress = { s.packUsedFraction },
            modifier = Modifier.fillMaxWidth(),
        )

        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
            Stat("${s.socRaw}", "bars")
            Stat("%.1f".format(s.voltageMv / 1000f), "V≈")
            Stat(if (s.brake) "ON" else "off", "brake")
        }

        errorText(s.errorCode)?.let { msg ->
            Text(msg, color = MaterialTheme.colorScheme.error)
        }

        // Protocol bring-up aid: show the most recent raw frame.
        if (s.rawFrameHex.isNotEmpty()) {
            Text("raw: ${s.rawFrameHex}",
                style = MaterialTheme.typography.bodySmall,
                modifier = Modifier.padding(top = 8.dp))
        }

        Button(onClick = onPair) { Text("Pair bike") }
    }
}

/** Map the controller's error byte to text (APT 500S datasheet §9); null = no fault. */
private fun errorText(code: Int): String? = when (code) {
    0x00, 0x01, 0x03 -> null                    // normal / brake — not faults
    0x04 -> "Throttle stuck high"
    0x06 -> "Low-voltage protection"
    0x07 -> "High-voltage protection"
    0x08 -> "Motor hall sensor error"
    0x09 -> "Motor phase-line error"
    0x10 -> "Controller over temperature"
    0x11 -> "Motor over temperature"
    0x12 -> "Current sensor error"
    0x13 -> "Battery temp sensor error"
    0x14 -> "Motor temp sensor error"
    0x21 -> "Speed sensor error"
    0x22 -> "BMS communication error"
    0x30 -> "Communication error"
    else -> "Error 0x%02X".format(code)
}

@Composable
private fun Stat(value: String, label: String) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(value, fontSize = 28.sp, fontWeight = FontWeight.SemiBold)
        Text(label, style = MaterialTheme.typography.labelMedium)
    }
}
