# shared

Shared FarmWhisper tool headers.

This directory holds small shared definitions used by multiple standalone tools.

## fwRadioInfo-US.h

Shared FarmWhisper US channel list and LoRa profile helpers.

The purpose of this file is to keep beacon, monitor, scanner, and future tools from drifting apart.

## Current shared defaults

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

## Channel list

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

## Include style

FarmWhisper tool sketches should include the shared radio info header as:

```cpp
#include "fwRadioInfo-US.h"
```

or, when using the Arduino-library shim:

```cpp
#include <fwRadioInfo-US.h>
```

Use the style that compiles cleanly for the tool and environment.

## Arduino CLI

When building from the repo root, add the shared include path:

```bash
--build-property "compiler.cpp.extra_flags=-I/home/zim/Arduino/farmwhisper-tools/shared"
```

## Arduino IDE

Arduino IDE can find the header through the local shim library:

```text
~/Arduino/libraries/FarmWhisperRadioInfo/
  library.properties
  src/fwRadioInfo-US.h -> /home/zim/Arduino/farmwhisper-tools/shared/fwRadioInfo-US.h
```

This avoids changing sketches back and forth between VSCode/Arduino CLI and Arduino IDE.
