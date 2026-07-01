#ifndef FW_CHANNEL_PLAN_H
#define FW_CHANNEL_PLAN_H

/*
  FarmWhisper Standard Channel Plan
  ---------------------------------

  This file defines the standard FarmWhisper LoRa channel table for US
  902-928 MHz operation.  These are FarmWhisper channels, not a generic
  LoRa channel plan.

  Design notes:
    - Intended for 500 kHz LoRa bandwidth.
    - Centers are spaced 1 MHz apart.
    - The plan avoids the lower and upper edges of the 902-928 MHz band.
    - User-facing channel numbers are 1-based.
    - Array indexes are 0-based because C/C++.

  Example:
    User-facing FW channel 1  = fwChannelsHz[0]  = 905.5 MHz
    User-facing FW channel 9  = fwChannelsHz[8]  = 913.5 MHz
    User-facing FW channel 13 = fwChannelsHz[12] = 917.5 MHz
*/

#include <Arduino.h>

static const uint8_t fwChannelCount = 13;
static const uint8_t fwDefaultChannel = 9;  // User-facing channel number, not array index.

static const uint32_t fwChannelsHz[fwChannelCount] = {
  905500000UL,
  906500000UL,
  907500000UL,
  908500000UL,
  909500000UL,
  910500000UL,
  911500000UL,
  912500000UL,
  913500000UL,
  914500000UL,
  915500000UL,
  916500000UL,
  917500000UL
};

static inline bool fwIsValidChannel(uint8_t channel) {
  return channel >= 1 && channel <= fwChannelCount;
}

static inline uint8_t fwChannelToIndex(uint8_t channel) {
  if (!fwIsValidChannel(channel)) {
    return fwDefaultChannel - 1;
  }

  return channel - 1;
}

static inline uint8_t fwIndexToChannel(uint8_t index) {
  if (index >= fwChannelCount) {
    return fwDefaultChannel;
  }

  return index + 1;
}

static inline uint32_t fwChannelToHz(uint8_t channel) {
  return fwChannelsHz[fwChannelToIndex(channel)];
}

static inline float fwChannelToMHz(uint8_t channel) {
  return fwChannelToHz(channel) / 1000000.0f;
}

static inline uint8_t fwHzToNearestChannel(uint32_t frequencyHz) {
  uint8_t bestChannel = fwDefaultChannel;
  uint32_t bestDelta = 0xFFFFFFFFUL;

  for (uint8_t i = 0; i < fwChannelCount; i++) {
    uint32_t channelHz = fwChannelsHz[i];
    uint32_t delta = frequencyHz > channelHz
      ? frequencyHz - channelHz
      : channelHz - frequencyHz;

    if (delta < bestDelta) {
      bestDelta = delta;
      bestChannel = i + 1;
    }
  }

  return bestChannel;
}

static inline uint8_t fwMHzToNearestChannel(float frequencyMHz) {
  return fwHzToNearestChannel((uint32_t)(frequencyMHz * 1000000.0f + 0.5f));
}

#endif
