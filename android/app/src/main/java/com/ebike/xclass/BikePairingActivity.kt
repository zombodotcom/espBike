package com.ebike.xclass

import android.companion.AssociationInfo
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.Intent
import android.content.IntentSender
import android.os.Bundle
import android.util.Log
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import com.ebike.xclass.ble.BikeUuids
import java.util.regex.Pattern

/**
 * One-time association to the ESP32 via CompanionDeviceManager. After the user picks
 * the bike from the system dialog we start observing its presence, which lets
 * [com.ebike.xclass.ble.BikeCompanionService] bind automatically when it's in range —
 * without ACCESS_FINE_LOCATION.
 */
class BikePairingActivity : ComponentActivity() {

    private val cdm by lazy { getSystemService(CompanionDeviceManager::class.java) }

    private val chooser =
        registerForActivityResult(ActivityResultContracts.StartIntentSenderForResult()) { result ->
            val info: AssociationInfo? =
                result.data?.getParcelableExtra(CompanionDeviceManager.EXTRA_ASSOCIATION)
            if (info != null) {
                startObserving(info)
                toast("Bike paired")
            } else {
                toast("Pairing cancelled")
            }
            finish()
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        associate()
    }

    private fun associate() {
        val deviceFilter = BluetoothLeDeviceFilter.Builder()
            .setNamePattern(Pattern.compile(Pattern.quote(BikeUuids.DEVICE_NAME)))
            .build()

        val request = AssociationRequest.Builder()
            .addDeviceFilter(deviceFilter)
            .setSingleDevice(true)
            .build()

        cdm.associate(request, mainExecutor, object : CompanionDeviceManager.Callback() {
            override fun onAssociationPending(intentSender: IntentSender) {
                chooser.launch(IntentSenderRequest.Builder(intentSender).build())
            }

            override fun onAssociationCreated(associationInfo: AssociationInfo) {
                startObserving(associationInfo)
                toast("Bike paired"); finish()
            }

            override fun onFailure(error: CharSequence?) {
                Log.e("Pairing", "associate failed: $error")
                toast("Pairing failed: $error"); finish()
            }
        })
    }

    private fun startObserving(info: AssociationInfo) {
        // Presence observation wakes BikeCompanionService when the bike is near.
        // The MAC-based overload works on API 33-35; on API 36+ you can switch to
        // startObservingDevicePresence(ObservingDevicePresenceRequest) keyed by info.id.
        @Suppress("DEPRECATION")
        info.deviceMacAddress?.let { cdm.startObservingDevicePresence(it.toString()) }
    }

    private fun toast(msg: String) = Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()

    companion object {
        fun intent(ctx: android.content.Context) = Intent(ctx, BikePairingActivity::class.java)
    }
}
