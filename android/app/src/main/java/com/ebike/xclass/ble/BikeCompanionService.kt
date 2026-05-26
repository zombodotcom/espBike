package com.ebike.xclass.ble

import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.bluetooth.BluetoothDevice
import android.companion.AssociationInfo
import android.companion.CompanionDeviceService
import android.content.pm.ServiceInfo
import android.os.Build
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch

/**
 * Bound by the system when the associated bike appears/disappears in BLE range
 * (CompanionDeviceManager.startObservingDevicePresence). On appearance we hold the
 * GATT connection and promote to a connectedDevice foreground service so the link
 * survives Doze with the screen off and music playing.
 */
@SuppressLint("MissingPermission")
class BikeCompanionService : CompanionDeviceService() {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private var client: BikeGattClient? = null

    override fun onDeviceAppeared(associationInfo: AssociationInfo) {
        val device: BluetoothDevice = associationInfo.associatedDevice
            ?.bluetoothDevice ?: return
        startForeground(NOTIF_ID, buildNotification("Connecting to bike…"))
        client = BikeGattClient(this).also { it.connect(device) }
        // Update the persistent notification as telemetry flows.
        scope.launch {
            BikeRepository.state.collectLatest { s ->
                if (s.connected) {
                    notify("${s.powerW} W · ${"%.1f".format(s.speedMph)} mph · " +
                        "${"%.0f".format(s.whUsed)} Wh")
                }
            }
        }
    }

    override fun onDeviceDisappeared(associationInfo: AssociationInfo) {
        client?.close(); client = null
        stopForeground(STOP_FOREGROUND_REMOVE)
    }

    override fun onDestroy() {
        scope.cancel()
        client?.close()
        super.onDestroy()
    }

    private fun buildNotification(text: String): Notification {
        val mgr = getSystemService(NotificationManager::class.java)
        if (mgr.getNotificationChannel(CHANNEL) == null) {
            mgr.createNotificationChannel(
                NotificationChannel(CHANNEL, "Ride", NotificationManager.IMPORTANCE_LOW)
            )
        }
        return NotificationCompat.Builder(this, CHANNEL)
            .setContentTitle("X-Class")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_menu_compass)
            .setOngoing(true)
            .build()
    }

    private fun notify(text: String) {
        val n = buildNotification(text)
        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(NOTIF_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        } else {
            getSystemService(NotificationManager::class.java).notify(NOTIF_ID, n)
        }
    }

    companion object {
        private const val CHANNEL = "ride"
        private const val NOTIF_ID = 42
    }
}
