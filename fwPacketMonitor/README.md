# fwPacketMonitor

FarmWhisper LoRa packet monitor for RAK4631.

This tool parks on the shared FarmWhisper default channel, listens for FarmWhisper packets, prints packet details to USB serial, and shows packet status on the SH1106G OLED.

## Version

Current working version: `0.1.8`

## Purpose

Use this tool to:

- Confirm a beacon is transmitting
- Decode FarmWhisper `HELLO` payloads
- Verify shared radio settings
- Watch RSSI/SNR for received packets
- Test battery voltage display
- Monitor packets without a computer display once flashed

This is a diagnostic monitor, not full FarmWhisper station firmware.

## Target hardware

- RAK4631 Core
- RAK19003 or RAK5005 base
- SX1262 LoRa radio
- SH1106G OLED on `Wire` at `0x3C`
- Battery voltage read from `WB_A0`
- USB serial output at 115200 baud

## Radio profile

Radio settings come from:

```cpp
#include "fwRadioInfo-US.h"
```

Current shared default:

```text
Channel   : CH09
Frequency : 913.500 MHz
BW Hz     : 500000
BW index  : 2
SF        : 7
CR index  : 1
CR        : 4/5
Preamble  : 8
Sync word : 0x12
```

## Display behavior

The OLED shows:

- Tool/version header
- Shared channel/profile info
- Battery voltage in `x.xxv` format
- RX count
- RSSI/SNR for the most recent packet
- Error/timeout counts
- SNR gauge
- Decoded FarmWhisper payload text

Long payloads scroll on the bottom line.

## Serial output

Startup prints the radio config, including:

```text
[RADIO] Plan      : shared/fwRadioInfo-US.h
[RADIO] Channel   : 09
[RADIO] Frequency : 913500000
[RADIO] Freq MHz  : 913.500
[RADIO] BW Hz     : 500000
[RADIO] BW index  : 2
[RADIO] SF        : 7
[RADIO] CR index  : 1
[RADIO] Preamble  : 8
[RADIO] Sync word : 0x12
```

Packet output includes decoded text, ASCII view, and hex bytes.

## Build with Arduino CLI

From the repository root:

```bash
arduino-cli compile \
  --fqbn rakwireless:nrf52:WisCoreRAK4631Board \
  --build-property "compiler.cpp.extra_flags=-I/home/zim/Arduino/farmwhisper-tools/shared" \
  fwPacketMonitor
```

## Upload

```bash
arduino-cli upload \
  --fqbn rakwireless:nrf52:WisCoreRAK4631Board \
  --port /dev/ttyACM0 \
  fwPacketMonitor
```

## Serial monitor

```bash
arduino-cli monitor \
  --port /dev/ttyACM0 \
  --config baudrate=115200
```

## Notes

- If the display is correct but no packets arrive, confirm the beacon startup radio config.
- The monitor is a parked receiver. It is the correct tool for reliable packet decoding.
- The site scanner only catches packet decodes opportunistically while sweeping.
