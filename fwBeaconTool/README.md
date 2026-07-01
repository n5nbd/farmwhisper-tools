# fwBeaconTool

FarmWhisper LoRa beacon transmitter for RAK4631.

This tool sends periodic FarmWhisper `HELLO` packets so scanners and monitors have a known signal to find.

## Version

Current working version: `0.1.3`

## Purpose

Use this tool to:

- Generate known FarmWhisper LoRa traffic
- Test `fwPacketMonitor`
- Verify `fwSiteScanner` packet-hit behavior
- Check antenna placement and range
- Confirm shared radio settings across tools

This is not full FarmWhisper station firmware. It is a field/debug beacon.

## Target hardware

- RAK4631 Core
- RAK19003 or RAK5005 base
- SX1262 LoRa radio
- USB serial for status output

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
TX power  : 14 dBm
```

The beacon must not keep old hardcoded radio settings. It should use the shared helpers for channel, frequency, bandwidth, spreading factor, coding rate, preamble, and TX power.

## Beacon behavior

Current default behavior:

```text
Node name : COOP
Packet    : FarmWhisper HELLO
Interval  : 15000 ms
Payload   : name=COOP bat=x.xxv up=seconds
```

Example serial output:

```text
[RADIO] TX type=HELLO src=4873 dst=65535 seq=1 len=25 payload="name=COOP bat=4.06v up=16"
[INFO] Radio TX done
```

## Startup serial config

The beacon prints its radio configuration at startup. This is important because a beacon can appear to be working while transmitting on the wrong channel or bandwidth.

Expected startup config includes:

```text
[RADIO] Configured for LoRa P2P TX
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
[RADIO] TX power  : 14 dBm
[RADIO] Interval  : 15000 ms
```

## Build with Arduino CLI

From the repository root:

```bash
arduino-cli compile \
  --fqbn rakwireless:nrf52:WisCoreRAK4631Board \
  --build-property "compiler.cpp.extra_flags=-I/home/zim/Arduino/farmwhisper-tools/shared" \
  fwBeaconTool
```

## Upload

```bash
arduino-cli upload \
  --fqbn rakwireless:nrf52:WisCoreRAK4631Board \
  --port /dev/ttyACM0 \
  fwBeaconTool
```

## Serial monitor

```bash
arduino-cli monitor \
  --port /dev/ttyACM0 \
  --config baudrate=115200
```

## Notes

- If `fwPacketMonitor` hears nothing, first confirm the beacon startup radio config.
- If `fwSiteScanner` shows no spike from the beacon, confirm the beacon is on the shared channel/profile.
- `CR index : 1` is correct for `CR4/5` in SX126x-Arduino.
