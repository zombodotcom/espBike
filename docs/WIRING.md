# Protoboard wiring

How to build the sniffer on a protoboard. Consistent with the firmware as written:
**ESP32 DevKitC**, UART2 **RX on GPIO16**, passive (RX-only) — we never transmit.

> ⚠️ Unverified for this bike. The 5-pin pinout/colors below are from the *generic*
> APT 500S datasheet — **meter every pin on your own loom before connecting anything**
> (see the main [README](../README.md) disclaimer).

## Bill of materials

- ESP32 DevKitC (classic ESP32 — the firmware uses `UART_NUM_2`/GPIO16, which the C3 lacks)
- 10 kΩ + 20 kΩ resistors — **only if the signal line is 5 V** (see decision below)
- Protoboard + a 3-pin header/JST to mate the spare branch of the display Y-splitter
- Permanent install only: buck converter rated **≥75 V input → 5 V**
  (52 V packs peak ~58.8 V, so a 60 V-max LM2596HV is too marginal; use a Mornsun
  K78xx-2000 or an XL7005-based module)

## Decide this first, with a multimeter

Probe the **Green** wire (controller→display) to GND with the bike powered on:

| Measured swing | Do this |
|---|---|
| ~5 V    | Use the **10k/20k divider** (drops 5 V → 3.3 V; ESP32 RX is not 5 V-tolerant) |
| ~3.3 V  | **Skip the divider**, wire Green straight to GPIO16 (a divider would sag it to ~2.2 V, marginal for logic-high) |

## 5-pin connector (generic APT 500S datasheet §11 — verify!)

| Pin | Color  | Function                         | Wire to ESP32?              |
|----:|--------|----------------------------------|-----------------------------|
| 1   | Red    | battery+ (~52 V)                 | **NO — never** |
| 2   | Blue   | switched power to controller (KSI) | no |
| 3   | Black  | GND                              | yes → ESP32 GND |
| 4   | Green  | RxD (controller → display)       | yes → GPIO16 (via divider if 5 V) |
| 5   | Yellow | TxD (display → controller)       | no (not needed for telemetry) |

## Bench setup (USB-powered)

```
   Bike Y-splitter (spare branch)              ESP32 DevKitC
   ┌───────────────────────────┐              ┌──────────────────────┐
   │ Red    +52V  ── DO NOT connect on bench   │ [USB to PC/charger]  │
   │ Blue   KSI   ── leave open                │                      │
   │ Yellow disp→ctrl ── leave open            │                      │
   │                                           │                      │
   │ Green  ctrl→display ──┐                   │                      │
   │                     [10kΩ]   (5V case)    │                      │
   │                       ├───────────────────● GPIO16 (RXD2)        │
   │                     [20kΩ]                 │                     │
   │                       │                    │                     │
   │ Black  GND ───────────┴───────────────────● GND                 │
   └───────────────────────────┘              └──────────────────────┘

   3.3V case: Green ──────────────────────────► GPIO16 directly (no resistors)
```

- **RX only** — ESP32 TX stays disconnected, so there is no risk of contending with
  the real display on the bus.
- **Common ground is mandatory:** bike Black → ESP32 GND, or the UART reads nothing.

## Permanent install (powered from the pack)

Same signal/GND wiring; replace USB with a buck converter:

```
   Red (+52V) ──► [BUCK  ≥75V in → 5V out] ──► ESP32 5V (VIN)
   Black (GND) ─┬──────────────────────────► BUCK GND
                └──────────────────────────► ESP32 GND (and divider GND)
```

## After wiring

1. Power the ESP32 (USB on bench), confirm `EBIKE-X-CLASS` in nRF Connect.
2. Power the bike, subscribe to char `0xEB0A`, read the **first frame byte** to identify
   the dialect (`0x41`/`0x46`/`0x3A`/`0x02`) — see the [README](../README.md) protocol table.
3. Garbage at 9600? Set `UART_INVERT_RX 1` in `main/main.c` before suspecting baud.
