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

        if (s.errorCode != 0) {
            Text("Error code: ${s.errorCode}", color = MaterialTheme.colorScheme.error)
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

@Composable
private fun Stat(value: String, label: String) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(value, fontSize = 28.sp, fontWeight = FontWeight.SemiBold)
        Text(label, style = MaterialTheme.typography.labelMedium)
    }
}
