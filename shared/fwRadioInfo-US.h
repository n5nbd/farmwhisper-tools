#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// FarmWhisper US Radio Info
// -----------------------------------------------------------------------------
//
// File:
//   shared/fwRadioInfo-US.h
//
// Purpose:
//   One shared place for the FarmWhisper US 902-928 MHz radio plan.
//
// This file owns:
//   - user-facing FarmWhisper channel numbers
//   - RF center frequencies
//   - default LoRa modem profile
//   - helper methods used by tools and station firmware
//
// Important regulatory note:
//
//   This profile is intended for a fixed-channel, digitally modulated 902-928 MHz
//   Part 15.247-style design path, not a frequency-hopping design.
//
//   FCC 15.247 requires digitally modulated systems in 902-928 MHz to use a
//   minimum 6 dB bandwidth of at least 500 kHz. It also includes conducted power,
//   power spectral density, antenna gain, and out-of-band emissions limits.
//
//   These constants are sane engineering defaults for development. They do not
//   certify a final product by themselves. Final hardware still needs proper
//   module/certification/legal review.
//
// Design choice:
//
//   We are intentionally not frequency hopping here. Each FarmWhisper channel is
//   a fixed RF center frequency. The scanner can recommend the cleanest channel,
//   and all tools/stations can use the same channel/profile helpers.
//
// -----------------------------------------------------------------------------
// US band constants
// -----------------------------------------------------------------------------

static const uint32_t fwUsBandMinHz = 902000000UL;
static const uint32_t fwUsBandMaxHz = 928000000UL;

// With a 500 kHz signal, keep centers at least 250 kHz from the nominal band edge.
static const uint32_t fwUsMinCenterHz = 902250000UL;
static const uint32_t fwUsMaxCenterHz = 927750000UL;

// -----------------------------------------------------------------------------
// LoRa profile
// -----------------------------------------------------------------------------

struct FwLoRaProfile
{
  uint32_t bandwidthHz;
  uint8_t spreadingFactor;
  uint8_t codingRate;
  uint16_t preambleLength;
  uint8_t syncWord;
  int8_t txPowerDbm;
};

// Default FarmWhisper US development profile.
//
// bandwidthHz:
//   500 kHz target for the 902-928 MHz digitally modulated / DTS-style path.
//
// spreadingFactor:
//   SF7 keeps packets short. Good default for local farm telemetry.
//
// codingRate:
//   Stored as a simple FarmWhisper value here.
//   Map this to the exact radio-library API in the sketch/module using it.
//
// syncWord:
//   0x12 is the common public LoRa sync word. We can change this later once the
//   whole toolchain is under our control.
//
// txPowerDbm:
//   Conservative tool default. Final allowed power depends on radio module,
//   antenna gain, certification path, and final emissions testing.

static const FwLoRaProfile fwLoRaProfileDefault =
{
  500000UL,  // bandwidthHz
  7,         // spreadingFactor
  1,         // codingRate: library-specific mapping
  8,         // preambleLength
  0x12,      // syncWord
  14         // txPowerDbm
};

// -----------------------------------------------------------------------------
// Channel plan
// -----------------------------------------------------------------------------
//
// Centers are kept well inside the 902-928 MHz band edge for a 500 kHz signal.
// Current spacing is 1 MHz. That gives simple user-facing channels and leaves
// breathing room between adjacent FarmWhisper channels.
//
// CH 01 = 903.000 MHz
// CH 09 = 911.000 MHz
// CH 13 = 915.000 MHz default
// CH 25 = 927.000 MHz

static const uint8_t fwDefaultChannel = 13;

static const uint32_t fwChannelFrequenciesHz[] =
{
  903000000UL,
  904000000UL,
  905000000UL,
  906000000UL,
  907000000UL,
  908000000UL,
  909000000UL,
  910000000UL,
  911000000UL,
  912000000UL,
  913000000UL,
  914000000UL,
  915000000UL,
  916000000UL,
  917000000UL,
  918000000UL,
  919000000UL,
  920000000UL,
  921000000UL,
  922000000UL,
  923000000UL,
  924000000UL,
  925000000UL,
  926000000UL,
  927000000UL
};

static const uint8_t fwChannelCount =
  sizeof(fwChannelFrequenciesHz) / sizeof(fwChannelFrequenciesHz[0]);

// -----------------------------------------------------------------------------
// Channel helpers
// -----------------------------------------------------------------------------

static uint8_t fwChannelToIndex(uint8_t channel)
{
  if (channel < 1)
  {
    return fwDefaultChannel - 1;
  }

  if (channel > fwChannelCount)
  {
    return fwDefaultChannel - 1;
  }

  return channel - 1;
}

static uint8_t fwIndexToChannel(uint8_t index)
{
  if (index >= fwChannelCount)
  {
    return fwDefaultChannel;
  }

  return index + 1;
}

static uint32_t fwChannelToHz(uint8_t channel)
{
  return fwChannelFrequenciesHz[fwChannelToIndex(channel)];
}

static float fwChannelToMHz(uint8_t channel)
{
  return fwChannelToHz(channel) / 1000000.0f;
}

static uint32_t fwIndexToHz(uint8_t index)
{
  if (index >= fwChannelCount)
  {
    return fwChannelToHz(fwDefaultChannel);
  }

  return fwChannelFrequenciesHz[index];
}

static float fwIndexToMHz(uint8_t index)
{
  return fwIndexToHz(index) / 1000000.0f;
}

static bool fwChannelIsValid(uint8_t channel)
{
  return channel >= 1 && channel <= fwChannelCount;
}

static uint8_t fwSafeChannel(uint8_t channel)
{
  if (!fwChannelIsValid(channel))
  {
    return fwDefaultChannel;
  }

  return channel;
}

static uint32_t fwDefaultFrequencyHz()
{
  return fwChannelToHz(fwDefaultChannel);
}

static float fwDefaultFrequencyMHz()
{
  return fwChannelToMHz(fwDefaultChannel);
}

// -----------------------------------------------------------------------------
// Profile helpers
// -----------------------------------------------------------------------------

static uint32_t fwDefaultBandwidthHz()
{
  return fwLoRaProfileDefault.bandwidthHz;
}

static uint8_t fwDefaultSpreadingFactor()
{
  return fwLoRaProfileDefault.spreadingFactor;
}

static uint8_t fwDefaultCodingRate()
{
  return fwLoRaProfileDefault.codingRate;
}

static uint16_t fwDefaultPreambleLength()
{
  return fwLoRaProfileDefault.preambleLength;
}

static uint8_t fwDefaultSyncWord()
{
  return fwLoRaProfileDefault.syncWord;
}

static int8_t fwDefaultTxPowerDbm()
{
  return fwLoRaProfileDefault.txPowerDbm;
}

// -----------------------------------------------------------------------------
// Sanity helpers
// -----------------------------------------------------------------------------

static bool fwFrequencyIsInsideUsBand(uint32_t frequencyHz)
{
  return frequencyHz >= fwUsBandMinHz && frequencyHz <= fwUsBandMaxHz;
}

static bool fwCenterFrequencyIsSafeForDefaultBandwidth(uint32_t frequencyHz)
{
  return frequencyHz >= fwUsMinCenterHz && frequencyHz <= fwUsMaxCenterHz;
}
