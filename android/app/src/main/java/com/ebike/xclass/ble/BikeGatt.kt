package com.ebike.xclass.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import java.util.ArrayDeque
import java.util.UUID

/** Process-wide holder of the latest telemetry, observed by the UI. */
object BikeRepository {
    private val _state = MutableStateFlow(BikeState())
    val state: StateFlow<BikeState> = _state
    internal fun mutate(block: (BikeState) -> BikeState) = _state.update(block)
}

/** Little-endian readers (matches the ESP32's native byte order). */
private fun ByteArray.u8(): Int = if (isNotEmpty()) this[0].toInt() and 0xFF else 0
private fun ByteArray.u16(): Int =
    if (size >= 2) (this[0].toInt() and 0xFF) or ((this[1].toInt() and 0xFF) shl 8) else 0
private fun ByteArray.i16(): Int = u16().toShort().toInt()
private fun ByteArray.i32(): Int =
    if (size >= 4) (this[0].toInt() and 0xFF) or ((this[1].toInt() and 0xFF) shl 8) or
        ((this[2].toInt() and 0xFF) shl 16) or ((this[3].toInt() and 0xFF) shl 24) else 0
private fun ByteArray.u32(): Long = i32().toLong() and 0xFFFFFFFFL
private fun ByteArray.hex(): String = joinToString(" ") { "%02X".format(it) }

/**
 * Owns one BluetoothGatt connection, subscribes to every characteristic, and decodes
 * notifications into [BikeRepository]. Subscriptions are written one descriptor at a
 * time (Android serializes GATT ops).
 */
@SuppressLint("MissingPermission")
class BikeGattClient(private val context: Context) {

    private var gatt: BluetoothGatt? = null
    private val toSubscribe = ArrayDeque<UUID>()

    fun connect(device: BluetoothDevice) {
        gatt = device.connectGatt(context, /* autoConnect = */ true, callback,
            BluetoothDevice.TRANSPORT_LE)
    }

    fun close() {
        gatt?.close()
        gatt = null
        BikeRepository.mutate { it.copy(connected = false) }
    }

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                BikeRepository.mutate { it.copy(connected = true) }
                g.requestMtu(247)   // fit the raw frame in one notification
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                BikeRepository.mutate { it.copy(connected = false) }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) {
            g.discoverServices()
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val svc = g.getService(BikeUuids.SERVICE) ?: run {
                Log.e(TAG, "service ${BikeUuids.SERVICE} not found"); return
            }
            toSubscribe.clear()
            BikeUuids.ALL.filter { svc.getCharacteristic(it) != null }.forEach { toSubscribe.add(it) }
            subscribeNext(g, svc)
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            subscribeNext(g, g.getService(BikeUuids.SERVICE) ?: return)
        }

        // Android 13+ delivers the value directly.
        override fun onCharacteristicChanged(
            g: BluetoothGatt, ch: BluetoothGattCharacteristic, value: ByteArray,
        ) = decode(ch.uuid, value)
    }

    private fun subscribeNext(g: BluetoothGatt, svc: android.bluetooth.BluetoothGattService) {
        val next = toSubscribe.poll() ?: return
        val ch = svc.getCharacteristic(next) ?: return subscribeNext(g, svc)
        g.setCharacteristicNotification(ch, true)
        val cccd = ch.getDescriptor(BikeUuids.CCCD) ?: return subscribeNext(g, svc)
        // New (API 33+) descriptor write API.
        g.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
    }

    private fun decode(uuid: UUID, v: ByteArray) {
        BikeRepository.mutate { s ->
            when (uuid) {
                BikeUuids.VOLTAGE  -> s.copy(voltageMv = v.u16())
                BikeUuids.CURRENT  -> s.copy(currentMa = v.i32())
                BikeUuids.POWER    -> s.copy(powerW = v.i16())
                BikeUuids.SPEED    -> s.copy(speedX100 = v.u16())
                BikeUuids.CADENCE  -> s.copy(cadenceRpm = v.u8())
                BikeUuids.PAS      -> s.copy(pas = v.u8())
                BikeUuids.THROTTLE -> s.copy(throttlePct = v.u8())
                BikeUuids.BRAKE    -> s.copy(brake = v.u8() != 0)
                BikeUuids.ERROR    -> s.copy(errorCode = v.u8())
                BikeUuids.TEMP     -> s.copy(motorTempC = v.u8())
                BikeUuids.WH       -> s.copy(whUsedMilli = v.u32())
                BikeUuids.SOC      -> s.copy(socRaw = v.u8())
                BikeUuids.RAW      -> s.copy(rawFrameHex = v.hex())
                else -> s
            }
        }
    }

    companion object { private const val TAG = "BikeGatt" }
}
