# LYWSD03MMC 2025 B1.5 (HW B1.1) BTHome v2 beacon + LCD + OTA

Port of the nRF52840/Zephyr SHT41 beacon to the Xiaomi LYWSD03MMC
(Telink TLSR8251). Same behaviour:

- every 60 s: measure temperature/humidity, read the battery right after the
  sensor load, update the advertisement
- BTHome v2 advertisement every 10 s, public (factory) address,
  objects 0x0C voltage (mV), 0x2E humidity (%), 0x45 temperature (0.1 °C)
- name `LYWSD03MMC` in the advertisement and the scan response, so passive
  scanners (ESPHome `active: false`) see it too (`ADV_NAME_IN_ADV 0` keeps
  it in the scan response only, as pvvx does)
- a failed sensor read keeps advertising the last good T/RH; the voltage is
  updated on every measurement
- Telink OTA service for over-the-air firmware updates, reachable only
  during the first 2 minutes after the battery is inserted (see Flashing)

plus the display: temperature (0.1 °C, `°C`), humidity (`%`), battery icon
below 2.4 V, `-- --` until the first good reading. The LCD controller holds
the image itself and is only written when a value changes.

## Power saving (same methods as pvvx/ATC_MiThermometer)

- No wake-ups of its own: a measurement starts right after every 6th
  advertising event, while the chip is awake anyway.
- The CPU sleeps during the ~12 ms sensor conversion (pvvx
  `SENSOR_SLEEP_MEASURE`); an app wake-up timer collects the result.
- VDD is sampled while the sensor converts, i.e. under load.
- Deep-sleep retention between events, also while connected: the device
  requests 20 ms interval / latency 49, so it wakes once per second.
- PB0 (battery probe) is driven high permanently; sensor I2C at 700 kHz;
  RF power restored after every suspend.
- Battery protection: below 2.2 V at power-on, or below 2.0 V while running,
  the LCD shows the voltage and `Lo` and the device deep-sleeps for 3 min
  before any flash write or radio use, then retries.

Outside the 2-minute OTA window the advertising is non-connectable, as in
the original build: the radio does not listen after each packet.

## Hardware: 2025 "B1.5" only

This build supports **only the LYWSD03MMC sold since 2025 with "B1.5"
printed on the PCB** (next to the battery holder), whose LCD controller is
driven over bit-banged SPI. pvvx firmware / TelinkMiFlasher report this
board as **HW B1.1**, which is the reliable way to tell it apart.

| Board | Sensor             | LCD controller                    |
|-------|--------------------|-----------------------------------|
| "B1.5" 2025 (pvvx B1.1) | SHTV3 (SHTC3) 0x70 | SPI, PD7 CLK / PB7 SDI, write-only |

There is no auto-detection. On any other unit (original UART-LCD B1.5,
B1.4, B1.6, B1.7, B1.9, B2.0) the display stays blank and/or the sensor read
fails: do not flash it there.

Pinout, segment map and the LCD frame format come from
pvvx/ATC_MiThermometer.

## Differences from the nRF build

- The sensor is wired straight to the coin cell, so there is no power gating;
  the SHTV3 is put to sleep explicitly after every measurement.
- Voltage is VDD (= CR2032), measured by driving the unbonded PB0 pad high
  and sampling it with the ADC. Uses the typical 1175 mV reference, so expect
  a few % absolute error; adjust `ADC_VREF_MV` against a meter if needed.

## Build

```sh
git clone https://github.com/pvvx/ATC_MiThermometer ../ATC_MiThermometer
# TC32 toolchain (Linux): e.g. https://github.com/flyskywhy/tc32 branch "linux"
ln -s /path/to/tc32 ../ATC_MiThermometer/SDK/tools/linux/tc32
make
```

Produces `lywsd03mmc_b15_bthome.bin` (with the Telink CRC appended).

## Flashing

- **First install** (from stock Xiaomi or pvvx firmware, which have OTA):
  https://pvvx.github.io/ATC_MiThermometer/TelinkMiFlasher.html → connect →
  "Select Firmware" → custom file. Units running the previous build of this
  firmware (no OTA) need one wired flash: USB-UART adapter on the SWS pad,
  `TlsrComProg`/`ATCtelink.py` from the pvvx repo.
- **Updates** (this firmware → any Telink image ≤ 128 KiB, including back to
  pvvx or stock), with https://pvvx.github.io/ATC_MiThermometer/TelinkOTA.html:
  1. Take the battery out, wait ~5 s, put it back. The BLE symbol on the
     LCD means the OTA window is open: for 2 minutes the device accepts
     connections and advertises every second.
  2. TelinkOTA.html → connect → pick `LYWSD03MMC` → select the `.bin` →
     start. The upload must start within the 2 minutes; once started it may
     run past them.
  3. The device reboots into the new firmware. If the window closes or the
     upload fails, start again from step 1.

  All units are named `LYWSD03MMC`; only the unit you just power-cycled is
  connectable, so it is the only one the page can connect to.

  The new image is written to the inactive flash bank and only activated
  after its CRC checks out, so an interrupted upload leaves the current
  firmware running. The transfer aborts after 16 s without data.

  The window opens only on a real power cycle. Reboots without one
  (low-battery sleep, OTA reboot, crash) keep it closed. After installing
  this firmware by OTA from pvvx firmware, power-cycle once before the next
  update.

**OTA itself is unauthenticated**: anyone in Bluetooth range can upload
firmware while the window is open, so it is limited to 2 minutes after a
battery swap, which needs physical access.
