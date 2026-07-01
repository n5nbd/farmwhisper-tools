# fwSiteScanner

FarmWhisper site scanner for RAK4631 / RAK19003.

This is a practical field scanner, not a true spectrum analyzer. It sweeps the shared FarmWhisper US LoRa channel list, samples RSSI, runs LoRa CAD, counts decodable packet hits, shows a terrain-style channel view on the SH1106G OLED, and prints scan data to USB serial.

## Version

Current working version: `0.1.13`

## Purpose

Use this tool to:

- Survey a site before choosing a FarmWhisper channel
- Visualize noisy channels
- Spot LoRa-shaped activity through CAD hits
- Opportunistically detect decodable FarmWhisper packets
- Recommend the least-ugly channel after enough terrain history accumulates

This is for practical FarmWhisper placement and channel selection, not FCC-grade measurement.

## Target hardware

- RAK4631 Core
- RAK19003 base
- SX1262 LoRa radio
- SH1106G OLED on `Wire` at `0x3C`
- Battery voltage read from `WB_A0`
- USB serial output at 115200 baud

## Radio profile

Radio settings come from:

```cpp
#include <fwRadioInfo-US.h>
```

Current shared default/profile:

```text
Slots          : 13
First channel  : CH01 905.500 MHz
Default channel: CH09 913.500 MHz
Bandwidth      : 500 kHz
BW index       : 2
SF             : 7
CR index       : 1
CR             : 4/5
Preamble       : 8
Sync word      : 0x12
```

## Channel list

The scanner uses the shared FarmWhisper US channel list from `shared/fwRadioInfo-US.h`.

Current channels:

```text
CH01 905.500 MHz
CH02 906.500 MHz
CH03 907.500 MHz
CH04 908.500 MHz
CH05 909.500 MHz
CH06 910.500 MHz
CH07 911.500 MHz
CH08 912.500 MHz
CH09 913.500 MHz
CH10 914.500 MHz
CH11 915.500 MHz
CH12 916.500 MHz
CH13 917.500 MHz
```

## What the screen means

- Grey/stipple terrain shows accumulated RF ugliness by channel.
- Narrow white bars show the latest RSSI level for each channel.
- The bottom tick marks the channel being scanned right now.
- The best marker appears only after a terrain bin reaches full scale.
- The final recommendation is calculated after scan completion.
- Low valleys are better candidates than tall hills.

## Ugliness scoring

The scanner intentionally does not use only average RSSI. It accumulates practical RF ugliness over time:

- Elevated RSSI adds small terrain growth.
- Busy RSSI adds more.
- CAD hits add more because they look LoRa-shaped.
- Clean packet decodes add a visible jump because they are confirmed LoRa traffic.

Packet decodes are rare while sweeping because the scanner is only briefly on each channel. They are treated as jackpot evidence, not the main detection method.

Current packet-decode tuning uses a fraction of full-scale terrain:

```cpp
terrainFullScalePoints / 5 = 20%
terrainFullScalePoints / 4 = 25%
terrainFullScalePoints / 3 = 33%
terrainFullScalePoints / 2 = 50%
```

The currently tested behavior makes one decode visibly move the terrain without instantly ending the scan.

## Scan completion

The scanner runs until any accumulated terrain bin reaches full scale. At that point:

- Scanning stops.
- The graph remains on screen.
- The recommendation is calculated from the accumulated terrain.
- The bottom status line changes to scan-complete behavior.

Reset, power cycle, or send `R` in Serial Monitor to clear the survey and start over.

## Serial output

Startup prints:

```text
FarmWhisper Site Scanner
Version: 0.1.13
Plan  : shared/fwRadioInfo-US.h
Scan  : RSSI + CAD + packet hits
Best  : calculated after terrain bin fills
```

Radio startup prints:

```text
[RADIO] Slots    : 13
[RADIO] First CH : 01 905.500 MHz
[RADIO] SF       : 7
[RADIO] BW Hz    : 500000
[RADIO] BW index : 2
[RADIO] CR index : 1
```

The scanner also prints human-readable scan lines and CSV-style scan data for later capture or comparison.

## Build with Arduino CLI

From the repository root:

```bash
arduino-cli compile \
  --fqbn rakwireless:nrf52:WisCoreRAK4631Board \
  --build-property "compiler.cpp.extra_flags=-I/home/zim/Arduino/farmwhisper-tools/shared" \
  fwSiteScanner
```

## Upload

```bash
arduino-cli upload \
  --fqbn rakwireless:nrf52:WisCoreRAK4631Board \
  --port /dev/ttyACM0 \
  fwSiteScanner
```

## Serial monitor

```bash
arduino-cli monitor \
  --port /dev/ttyACM0 \
  --config baudrate=115200
```

## Notes

- Do not slow the scanner just to catch beacons. Longer dwell helps only the channel currently being scanned and hurts the other channels.
- Use `fwPacketMonitor` when reliable packet decoding is the goal.
- Use `fwSiteScanner` when broad channel/noise survey is the goal.
