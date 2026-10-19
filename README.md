# LYWSD03MMC 2025 B1.5 (pvvx B1.1) BTHome v2 beacon + LCD + OTA

Port of the nRF52840/Zephyr SHT41 beacon to the Xiaomi LYWSD03MMC
(Telink TLSR8251), built on the official Telink BLE SDK
([tc_ble_single_sdk](https://github.com/telink-semi/tc_ble_single_sdk)
V3.4.3.0). Same behaviour:

- every 60 s: measure temperature/humidity, read the battery right after the
  sensor load, update the advertisement
- BTHome v2 advertisement every 10 s, public (factory) address,
  objects 0x0C voltage (mV), 0x2E humidity (%), 0x45 temperature (0.1 °C)
- name `LYWSD03MMC` in the advertisement and the scan response, so passive
  scanners (ESPHome `active: false`) see it too (`ADV_NAME_IN_ADV 0` keeps
  it in the scan response only, as pvvx does)
- a failed sensor read keeps advertising the last good T/RH; the voltage is
  updated on every measurement
- Telink OTA service (the SDK's OTA server) for over-the-air firmware
  updates, reachable only during the first 2 minutes after the battery is
  inserted (see Flashing)

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
  and sampling it with the ADC. The ADC reference is the chip's factory
  calibration, which the SDK loads from the calibration sector or eFuse
  (typical 1175 mV when there is none). To trim against a meter, set
  `ADC_VREF_MV` to a fixed value in mV.

## Build

Builds on Linux x86-64 only (the SDK's post-build tool is a Linux x86-64
binary). Official Telink SDK, used unmodified, plus the TC32 toolchain:

```sh
git clone -b release/V3.4.3.0 https://github.com/telink-semi/tc_ble_single_sdk ../tc_ble_single_sdk
# apply the official patches as tc_ble_single_sdk_v3.4.3.0_Patch/patch_*/Patch_Guide.txt
# says (copy each patch folder over tc_ble_single_sdk/, 0001 then 0002)
# TC32 toolchain (Linux): e.g. https://github.com/flyskywhy/tc32 branch "linux"
make TC32_PATH=/path/to/tc32/bin/
```

`SDK_PATH` defaults to `../tc_ble_single_sdk/tc_ble_single_sdk`. The
makefile uses the same compiler flags, startup code
(`boot/B85/cstartup_825x.S`, `MCU_STARTUP_8251`: 32 KiB SRAM), linker
script (`project/tlsr_tc32/B85/boot.link`), libraries (`lt_825x`,
`lt_general_stack`) and post-build tool (`script/tl_check_fw/check_fw`)
as the SDK's own `825x_ble_sample` project. Because the SDK only
looks up `app_config.h` for its own demo projects, `src/app_config.h` is
force-included (`-include`) into every file, SDK sources included.

Produces `LYWSD03MMC-BTHome.bin` (with the Telink CRC appended) and prints
the memory use: about 54 KiB of image, 17.8 KiB of RAM code + retained data
(so 32 KiB deep-sleep retention, as before), 21 KiB of the 32 KiB SRAM.

### Changes from the pvvx-SDK build

- RF/clock bring-up, calibration loading
  (`blc_app_loadCustomizedParameters_normal()` / `_deepRetn()`), retention
  size selection and the OTA server API are the official ones.
- The power-on marker moved from `DEEP_ANA_REG0` to `DEEP_ANA_REG1`: the
  official SDK reserves bit 1 of `DEEP_ANA_REG0` for its crystal start-up
  failure flag.
- OTA: inactivity timeout 16 s (`OTA_PACKET_TIMEOUT_S`), whole upload at
  most 1000 s (`OTA_PROCESS_TIMEOUT_S`; the SDK default would abort after
  30 s); images up to 124 KiB (SDK limit). The OTA characteristic gained
  NOTIFY and a CCC descriptor, as in the SDK sample, because the official
  server answers the extended OTA protocol on it.
- Flash write protection is still cleared once per power-on (status
  register := 0, as pvvx), now with the SDK's flash driver and only when a
  protection bit is set.

## Flashing

- **First install** (from stock Xiaomi or pvvx firmware, which have OTA):
  https://pvvx.github.io/ATC_MiThermometer/TelinkMiFlasher.html → connect →
  "Select Firmware" → custom file. Units running the previous build of this
  firmware (no OTA) need one wired flash: USB-UART adapter on the SWS pad,
  `TlsrComProg`/`ATCtelink.py` from the pvvx repo.
- **Updates** (this firmware → any Telink image ≤ 124 KiB, including back to
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
  firmware running. The transfer aborts after 16 s without data or after
  1000 s in total.

  The window opens only on a real power cycle. Reboots without one
  (low-battery sleep, OTA reboot, crash) keep it closed. Exception: the
  first boot after installing this build by OTA from other firmware (stock,
  pvvx, or the earlier pvvx-SDK build of this firmware) opens the window
  once, because the marker register was never set.

**OTA itself is unauthenticated**: anyone in Bluetooth range can upload
firmware while the window is open, so it is limited to 2 minutes after a
battery swap, which needs physical access.
