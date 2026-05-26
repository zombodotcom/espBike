# X-Class Android app

Native Kotlin + Jetpack Compose companion app for the `ebike_ble_bridge` ESP32. Holds
a background BLE connection (screen off, phone in pocket, music playing) via
CompanionDeviceManager + a `connectedDevice` foreground service, and shows live
telemetry.

## Building

In **Android Studio**: Open this `android/` folder and sync.

From the **CLI** (verified — produces `app/build/outputs/apk/debug/app-debug.apk`):

```powershell
$env:JAVA_HOME  = "C:\Program Files\Java\jdk-17"
$env:ANDROID_HOME = "C:\Users\zombo\AppData\Local\Android\Sdk"
cd android
.\gradlew.bat :app:assembleDebug
```

Install to a connected phone (USB debugging on): `.\gradlew.bat :app:installDebug`.

> Status: **compiled and packaged successfully** with Gradle 8.11.1 + JDK 17 +
> compileSdk 35. minSdk is **33** (CompanionDeviceManager's `AssociationInfo` /
> `associate(...callback)` are API 33+). Runtime behaviour on a real phone is not yet
> exercised. The ESP32 firmware was verified separately (IDF v6.0 build, QEMU 9.2.2
> boot, decode-math unit tests).

## BLE contract

`app/.../ble/BikeUuids.kt` mirrors the firmware service `0xEB1C` and its
characteristics exactly. If you change a UUID or a value's width in `main/main.c`,
change it here too. Current is `int32` mA, voltage is `uint16` mV (approximate).

## Architecture

- `BikePairingActivity` — one-time `CompanionDeviceManager.associate` + presence
  observation (no `ACCESS_FINE_LOCATION`).
- `ble/BikeCompanionService` — `CompanionDeviceService`; on device-appeared, connects
  GATT and promotes to a `connectedDevice` foreground service with a live notification.
- `ble/BikeGatt.kt` — GATT client; subscribes to every characteristic, decodes
  little-endian values into `BikeRepository.state` (a `StateFlow`).
- `ui/DashboardScreen` — live watts/speed/current/temp, an honest **Wh-used** battery
  gauge (not the voltage bar), SOC bars, brake/error, and the raw frame for bring-up.
- `data/` — Room (`rides` table) for ride logging.

## Things to verify on first run

- **Pairing dialog**: the `BluetoothLeDeviceFilter` name pattern must match the
  advertised name `EBIKE-X-CLASS`. The firmware uses a stable public address (CDM
  cannot filter random addresses).
- **Foreground service start from background**: declared with
  `REQUEST_COMPANION_START_FOREGROUND_SERVICES_FROM_BACKGROUND`; confirm the service
  promotes to foreground from `onDeviceAppeared`.
- **Ride logging** is wired (DAO + DB) but not yet started/stopped from the service —
  add the start/stop + row insert once live data is confirmed.
