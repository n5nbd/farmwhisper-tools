# FarmWhisper Site Scanner

Standalone RAK4631 / RAK19003 survey firmware.

This is a practical field scanner, not a true spectrum analyzer. It sweeps a short set of 902-923 MHz LoRa slots, samples RSSI, runs LoRa CAD, counts decodable packet hits, shows a terrain-style channel view on the SH1106G OLED, and prints CSV-style scan data to USB serial.

## Version

`0.1.4-display-layer-fix`

This version keeps the long-term accumulated survey scoring, but fixes the OLED graph draw order so the current RSSI sample is visible over the cumulative terrain.

## Target hardware

- RAK4631 Core
- RAK19003 base
- External SH1106G OLED
  - I2C bus: `Wire`
  - Address: `0x3C`
- Battery input read from `WB_A0`

## Required Arduino libraries

- Adafruit GFX Library
- Adafruit SH110X
- SX126x-Arduino / SX126x-RAK4630 support

## Arduino IDE target

Use the same RAK4631 board setup already used for the working FarmWhisper monitor and beacon tools.

## What the screen means

- The stippled/grey terrain is the long-term cumulative score by slot.
- The solid white bars are the latest RSSI samples by slot, drawn over the grey terrain.
  - RSSI raises it.
  - CAD hits raise it.
  - Decoded packet hits raise it.
  - Busy-RSSI readings raise it.
- The bottom tick marks the slot being scanned right now.
- The single vertical line marks the current long-term best slot.
- `Best` still shows the actual recommended frequency, long-term RSSI average, and sample count.
- `Now` shows the slot being scanned right now, the latest RSSI sample, the long sample count for that slot, and a simple accumulated activity percentage.
- Shoot for the valleys. A low grey/stippled valley is usually a better FarmWhisper candidate than a tall grey hill.
- This is for choosing a relatively quiet FarmWhisper channel, not for FCC-grade measurement.

## Letting it settle

Because LoRa traffic is intermittent, let the scanner run for a while at the actual mounting location. For a serious site check, an hour is a reasonable first survey window.

The scan history accumulates until reset or power cycle. To clear the history without reflashing, open Serial Monitor and send `R`.

## Serial output

The sketch prints human-readable `[SCAN]` lines and `[CSV]` lines like:

```text
[CSV] ms,scan,sweep,slot,freqMHz,rssiAvgDbm,rssiMinDbm,rssiMaxDbm,samples,cad,packets,busy,score,longSamples,rssiLongAvgDbm,cadPct,packetPct,busyPct,longScore,bestSlot,bestFreqMHz,batteryVolts
```

Those lines can be captured later for mapping or comparison between locations.

## Install

Copy the `fwSiteScanner` folder into your Arduino sketch directory, open `fwSiteScanner.ino`, compile, and upload to the RAK4631.

## Display draw order in 0.1.4

The graph is drawn in this order:

1. Clear the screen.
2. Draw cumulative long-term score as stippled/grey terrain.
3. Draw latest RSSI sample bars in solid white over the terrain.
4. Draw the bottom sweep marker under the current channel.
5. Draw the vertical best-slot marker last.

That keeps the current sample visible while still showing the long-term valleys.


## Version 0.1.4 display cleanup

- Bottom status line now starts with the current frequency.
- Removed the `Now` label and activity-percent field so the line fits the 128x64 OLED.
- Bottom status line is now: `freq rssi H# S#`.
  - `H` = cumulative CAD-or-packet activity hits for the current slot.
  - `S` = long-term samples for the current slot.
- Channel bars are now spread across the full graph width so the final slot gets drawn instead of leaving unused pixels at the right edge.
