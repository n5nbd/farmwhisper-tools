# FarmWhisper Tools

Standalone diagnostic and field utility firmware for FarmWhisper development.

These tools are intentionally simple, practical, and mostly self-contained. They are not full FarmWhisper station firmware. They exist to help test radios, survey sites, verify packets, and support early bring-up work.

## Current tools

### fwBeaconTool

RAK4631 LoRa beacon transmitter.

It sends periodic FarmWhisper `HELLO` packets containing node name, battery voltage, and uptime. It is useful for testing receivers, site scanning, packet decoding, antenna placement, and field range checks.

Current tested behavior:

- Board: RAK4631
- Radio: SX1262 LoRa P2P TX
- Default node name: `COOP`
- Beacon interval: 15000 ms
- Shared radio profile from `shared/fwRadioInfo-US.h`
- Startup serial config print shows channel, frequency, bandwidth, spreading factor, coding rate, preamble, sync word, TX power, and interval

### fwPacketMonitor

RAK4631 LoRa packet monitor.

It parks on the shared FarmWhisper default channel, listens for FarmWhisper packets, prints received packet details to USB serial, and displays packet status on the SH1106G OLED.

Current tested behavior:

- Board: RAK4631 / RAK19003 or RAK5005
- Radio: SX1262 passive RX
- Display: SH1106G OLED on `Wire` at `0x3C`
- Shows battery voltage in `x.xxv` format
- Shows shared radio config on screen and serial
- Displays decoded FarmWhisper payload text
- Long payloads scroll on the bottom OLED line

### fwSiteScanner

RAK4631 LoRa slot scanner and field survey tool.

It sweeps the shared FarmWhisper US channel list, samples RSSI, runs CAD, watches for packet decodes, and builds a cumulative terrain-style ugliness map on the OLED. It recommends the least-ugly slot after a terrain bin reaches full scale.

Current tested behavior:

- Board: RAK4631 / RAK19003
- Radio: SX1262 LoRa slot scanner
- Display: SH1106G OLED on `Wire` at `0x3C`
- Uses shared channel/profile helpers from `shared/fwRadioInfo-US.h`
- Packet decodes add a visible terrain jump because a clean decode is confirmed LoRa traffic

## Shared radio plan

Shared channel and LoRa profile information lives in:

```text
shared/fwRadioInfo-US.h
```

The tools should use this shared file instead of duplicating channel/frequency/modem constants.

Current shared default:

```text
Default channel : CH09
Frequency       : 913.500 MHz
Bandwidth       : 500 kHz
Spreading factor: SF7
Coding rate     : CR4/5
Preamble        : 8
Sync word       : 0x12
TX power        : 14 dBm
```

In the SX126x-Arduino API, coding rate is passed as an index:

```text
1 = CR4/5
2 = CR4/6
3 = CR4/7
4 = CR4/8
```

So serial output showing `CR index : 1` means `CR4/5`.

## Arduino CLI build pattern

From the repository root:

```bash
arduino-cli compile \
  --fqbn rakwireless:nrf52:WisCoreRAK4631Board \
  --build-property "compiler.cpp.extra_flags=-I/home/zim/Arduino/farmwhisper-tools/shared" \
  fwBeaconTool
```

Replace `fwBeaconTool` with `fwPacketMonitor` or `fwSiteScanner` as needed.

## Arduino IDE include shim

Arduino IDE 1.8.x does not automatically know about the repo `shared/` directory when sketches include:

```cpp
#include "fwRadioInfo-US.h"
```

The current local workaround is to expose the shared header as a tiny local Arduino library:

```text
~/Arduino/libraries/FarmWhisperRadioInfo/
  library.properties
  src/fwRadioInfo-US.h -> /home/zim/Arduino/farmwhisper-tools/shared/fwRadioInfo-US.h
```

This keeps the sketch include clean and avoids editing RAK board package files.

## Repository notes

- Keep README files updated when tool behavior changes.
- Prefer shared radio helpers over duplicated constants.
- Keep the tools simple and readable.
- These tools are allowed to be brute-force and diagnostic-oriented.
- Product firmware can be cleaner and more abstract later.
