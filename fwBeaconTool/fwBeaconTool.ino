/*
  FarmWhisper Beacon Tool
  Version: 0.1.3

  Target hardware:
  - RAK4631 Core
  - RAK19003 or RAK5005 base

  Radio:
  - SX1262 LoRa P2P beacon transmitter
  - Uses shared/fwRadioInfo-US.h for channel list and LoRa profile

  Purpose:
  Simple FarmWhisper diagnostic beacon. This tool transmits periodic beacon
  packets on one selected FarmWhisper channel.

  Top-of-script knob:
  - beaconChannel selects the FarmWhisper user-facing channel.
*/

/*
  FarmWhisper Beacon Tool
  File: fwBeaconTool.ino

  Board:
  - RAK4631 on WisBlock base
  - Tested target: same hardware family as the working monitor/station

  Purpose:
  - Transmit a FarmWhisper-compatible HELLO beacon
  - Include battery voltage in the payload
  - No display
  - Serial logging only
  - Sequence number wraps from 9999 back to 1

  Important:
  - This uses SX126x-Arduino / SX126x-RAK4630 style radio init,
    not generic RadioLib pin mapping.
  - The packet is encoded with the same FarmWhisper wire format
    used by the station repo: magic bytes, version, header, payload,
    and CRC16.
*/

#include <Arduino.h>
#include <SPI.h>
#include <string.h>
#include <SX126x-RAK4630.h>



#include "fwRadioInfo-US.h"

static const char fwBeaconToolVersion[] = "0.1.1";

// -----------------------------------------------------------------------------
// FarmWhisper beacon radio selection
// -----------------------------------------------------------------------------
//
// Change this one value to rebuild the beacon for a different FarmWhisper
// user-facing channel. Valid US channels are defined in shared/fwRadioInfo-US.h.
//
// Default is fwDefaultChannel from the shared radio plan.
//
static const uint8_t beaconChannel = fwDefaultChannel;

// SX126x-Arduino bandwidth index:
//   0 = 125 kHz
//   1 = 250 kHz
//   2 = 500 kHz
//
// shared/fwRadioInfo-US.h stores bandwidth in Hz. This maps it to the
// SX126x-Arduino API value.
static uint8_t fwSx126xBandwidthIndex()
{
  if (fwDefaultBandwidthHz() >= 500000UL)
  {
    return 2;
  }

  if (fwDefaultBandwidthHz() >= 250000UL)
  {
    return 1;
  }

  return 0;
}

static uint8_t activeBeaconChannel()
{
  return fwSafeChannel(beaconChannel);
}

static uint32_t activeBeaconFrequencyHz()
{
  return fwChannelToHz(activeBeaconChannel());
}

static float activeBeaconFrequencyMHz()
{
  return fwChannelToMHz(activeBeaconChannel());
}


// -----------------------------------------------------------------------------
// FarmWhisper radio settings
// -----------------------------------------------------------------------------
//
// These match the current station radio settings:
//
//   Frequency:       915.000 MHz
//   LoRa bandwidth:  0   = 125 kHz in SX126x-Arduino
//   Spreading factor: 7
//   Coding rate:     1   = 4/5 in SX126x-Arduino
//   Preamble:        8
//   Fixed length:    false
//   IQ inversion:    false
//   TX power:        14 dBm
//

static const uint32_t rfFrequencyHz = activeBeaconFrequencyHz();
static const uint8_t loraBandwidth = fwSx126xBandwidthIndex();
static const uint8_t loraSpreadingFactor = fwDefaultSpreadingFactor();
static const uint8_t loraCodingRate = fwDefaultCodingRate();
static const uint16_t loraPreambleLength = fwDefaultPreambleLength();
static const bool loraFixedLengthPayload = false;
static const bool loraIqInversion = false;
static const int8_t txOutputPowerDbm = fwDefaultTxPowerDbm();
static const uint32_t txTimeoutMs = 3000UL;

// -----------------------------------------------------------------------------
// FarmWhisper protocol settings
// -----------------------------------------------------------------------------

static const uint16_t networkId = 0xF011;
static const uint16_t localNodeId = 0x1309;
static const uint16_t broadcastNode = 0xFFFF;

static const uint8_t packetTypeHello = 0x01;

static const uint8_t maxPayloadLength = 64;
static const uint8_t maxWireLength = 80;

static const uint8_t magic0 = 'F';
static const uint8_t magic1 = 'W';
static const uint8_t protocolVersion = 1;

static const size_t headerLength = 14;
static const size_t crcLength = 2;

// -----------------------------------------------------------------------------
// Beacon behavior
// -----------------------------------------------------------------------------

static const unsigned long beaconIntervalMs = 15000UL;

uint16_t sequenceNumber = 1;
unsigned long lastBeaconMs = 0;

// -----------------------------------------------------------------------------
// Battery measurement
// -----------------------------------------------------------------------------
//
// RAK4631/WisBlock battery sense is available on WB_A0.
//
// The multiplier below follows the common RAK4631 battery-read example pattern:
// - internal 3.0V ADC reference
// - 12-bit ADC
// - board voltage divider compensation
//

static const uint32_t batteryPin = WB_A0;

static const float batteryMvPerLsb = 0.73242188F;
static const float batteryDividerCompensation = 1.73F;
static const float batteryRealMvPerLsb = batteryMvPerLsb * batteryDividerCompensation;

// -----------------------------------------------------------------------------
// Beacon shared radio compatibility constants
// -----------------------------------------------------------------------------
#define RF_FREQUENCY activeBeaconFrequencyHz()
#define LORA_BANDWIDTH fwSx126xBandwidthIndex()
#define LORA_SPREADING_FACTOR fwDefaultSpreadingFactor()
#define LORA_CODINGRATE fwDefaultCodingRate()
#define LORA_PREAMBLE_LENGTH fwDefaultPreambleLength()
#define TX_OUTPUT_POWER fwDefaultTxPowerDbm()

// -----------------------------------------------------------------------------
// Radio event state
// -----------------------------------------------------------------------------

RadioEvents_t radioEvents;

volatile bool txDoneFlag = false;
volatile bool txTimeoutFlag = false;

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------

void writeU16(uint8_t* buffer, size_t offset, uint16_t value)
{
  buffer[offset] = static_cast<uint8_t>((value >> 8) & 0xFF);
  buffer[offset + 1] = static_cast<uint8_t>(value & 0xFF);
}

uint16_t crc16Ccitt(const uint8_t* data, size_t length)
{
  uint16_t crc = 0xFFFF;

  for (size_t i = 0; i < length; ++i)
  {
    crc ^= static_cast<uint16_t>(data[i]) << 8;

    for (uint8_t bit = 0; bit < 8; ++bit)
    {
      if ((crc & 0x8000) != 0)
      {
        crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
      }
      else
      {
        crc = static_cast<uint16_t>(crc << 1);
      }
    }
  }

  return crc;
}

size_t encodeFarmWhisperPacket(
  uint8_t packetType,
  uint16_t source,
  uint16_t destination,
  uint16_t sequence,
  const uint8_t* payload,
  uint8_t payloadLength,
  uint8_t* outBuffer,
  size_t outCapacity
)
{
  if (payloadLength > maxPayloadLength)
  {
    return 0;
  }

  const size_t wireLength = headerLength + payloadLength + crcLength;

  if (wireLength > outCapacity || wireLength > maxWireLength)
  {
    return 0;
  }

  outBuffer[0] = magic0;
  outBuffer[1] = magic1;
  outBuffer[2] = protocolVersion;
  outBuffer[3] = packetType;
  outBuffer[4] = 0;

  writeU16(outBuffer, 5, networkId);
  writeU16(outBuffer, 7, source);
  writeU16(outBuffer, 9, destination);
  writeU16(outBuffer, 11, sequence);

  outBuffer[13] = payloadLength;

  for (uint8_t i = 0; i < payloadLength; ++i)
  {
    outBuffer[headerLength + i] = payload[i];
  }

  const uint16_t crc = crc16Ccitt(outBuffer, headerLength + payloadLength);
  writeU16(outBuffer, headerLength + payloadLength, crc);

  return wireLength;
}

float readBatteryVolts()
{
  const float rawAdc = analogRead(batteryPin);
  const float millivolts = rawAdc * batteryRealMvPerLsb;

  return millivolts / 1000.0F;
}

void incrementSequence()
{
  if (sequenceNumber >= 9999)
  {
    sequenceNumber = 1;
  }
  else
  {
    sequenceNumber++;
  }
}

// -----------------------------------------------------------------------------
// Radio callbacks
// -----------------------------------------------------------------------------

void onTxDone()
{
  txDoneFlag = true;
}

void onTxTimeout()
{
  txTimeoutFlag = true;
}

// -----------------------------------------------------------------------------
// Logging
// -----------------------------------------------------------------------------

void printBanner()
{
  Serial.println();
  Serial.println("========================================");
  Serial.println(" FarmWhisper Beacon Tool");
  Serial.println(" Board : RAK4631");
  Serial.println(" Radio : SX1262");
  Serial.println(" Mode  : FarmWhisper HELLO beacon");
  Serial.println(" UI    : Serial only");
  Serial.println("========================================");
  Serial.println();
}


void printRadioConfig()
{
  Serial.println("[RADIO] Configured for LoRa P2P TX");
  Serial.println("[RADIO] Plan      : shared/fwRadioInfo-US.h");

  Serial.print("[RADIO] Channel   : ");
  if (activeBeaconChannel() < 10)
  {
    Serial.print("0");
  }
  Serial.println(activeBeaconChannel());

  Serial.print("[RADIO] Frequency : ");
  Serial.println((uint32_t)rfFrequencyHz);

  Serial.print("[RADIO] Freq MHz  : ");
  Serial.println(activeBeaconFrequencyMHz(), 3);

  Serial.print("[RADIO] BW Hz     : ");
  Serial.println(fwDefaultBandwidthHz());

  Serial.print("[RADIO] BW index  : ");
  Serial.println(loraBandwidth);

  Serial.print("[RADIO] SF        : ");
  Serial.println(loraSpreadingFactor);

  Serial.print("[RADIO] CR index  : ");
  Serial.println(loraCodingRate);

  Serial.print("[RADIO] Preamble  : ");
  Serial.println(loraPreambleLength);

  Serial.print("[RADIO] Sync word : 0x");
  if (fwDefaultSyncWord() < 16)
  {
    Serial.print("0");
  }
  Serial.println(fwDefaultSyncWord(), HEX);

  Serial.print("[RADIO] TX power  : ");
  Serial.print(txOutputPowerDbm);
  Serial.println(" dBm");

  Serial.print("[RADIO] Interval  : ");
  Serial.print(beaconIntervalMs);
  Serial.println(" ms");
}

// -----------------------------------------------------------------------------
// Radio setup
// -----------------------------------------------------------------------------

void beginRadio()
{
  Serial.println("[INFO] Radio begin");

  lora_rak4630_init();

  radioEvents.TxDone = onTxDone;
  radioEvents.TxTimeout = onTxTimeout;
  radioEvents.RxDone = NULL;
  radioEvents.RxTimeout = NULL;
  radioEvents.RxError = NULL;

  Radio.Init(&radioEvents);

  Radio.SetChannel(rfFrequencyHz);

  Radio.SetTxConfig(
    MODEM_LORA,
    txOutputPowerDbm,
    0,
    loraBandwidth,
    loraSpreadingFactor,
    loraCodingRate,
    loraPreambleLength,
    loraFixedLengthPayload,
    true,
    0,
    0,
    loraIqInversion,
    txTimeoutMs
  );

  printRadioConfig();
}

// -----------------------------------------------------------------------------
// Beacon TX
// -----------------------------------------------------------------------------

void transmitBeacon()
{
  const float batteryVolts = readBatteryVolts();

  char payloadText[maxPayloadLength + 1];

  snprintf(
    payloadText,
    sizeof(payloadText),
    "name=COOP bat=%.2fv up=%lu",
    batteryVolts,
    millis() / 1000UL
  );

  const uint8_t payloadLength = strlen(payloadText);

  uint8_t wireBuffer[maxWireLength];

  const size_t wireLength = encodeFarmWhisperPacket(
    packetTypeHello,
    localNodeId,
    broadcastNode,
    sequenceNumber,
    reinterpret_cast<const uint8_t*>(payloadText),
    payloadLength,
    wireBuffer,
    sizeof(wireBuffer)
  );

  if (wireLength == 0)
  {
    Serial.println("[ERROR] Packet encode failed");
    return;
  }

  Serial.print("[RADIO] TX type=HELLO src=");
  Serial.print(localNodeId);
  Serial.print(" dst=");
  Serial.print(broadcastNode);
  Serial.print(" seq=");
  Serial.print(sequenceNumber);
  Serial.print(" len=");
  Serial.print(payloadLength);
  Serial.print(" payload=\"");
  Serial.print(payloadText);
  Serial.println("\"");

  txDoneFlag = false;
  txTimeoutFlag = false;

  Radio.Send(wireBuffer, wireLength);

  incrementSequence();
}

void handleRadioEvents()
{
  Radio.IrqProcess();

  if (txDoneFlag)
  {
    txDoneFlag = false;
    Serial.println("[INFO] Radio TX done");
  }

  if (txTimeoutFlag)
  {
    txTimeoutFlag = false;
    Serial.println("[WARN] Radio TX timeout");
  }
}

// -----------------------------------------------------------------------------
// Arduino setup / loop
// -----------------------------------------------------------------------------

void setup()
{
  Serial.begin(115200);

  unsigned long serialStartMs = millis();

  while (!Serial && millis() - serialStartMs < 3000UL)
  {
    delay(10);
  }

  printBanner();

  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(1);

  readBatteryVolts();

  beginRadio();

  lastBeaconMs = millis();

  Serial.println("[INFO] Beacon loop starting");
  Serial.println();
}

void loop()
{
  handleRadioEvents();

  const unsigned long nowMs = millis();

  if (nowMs - lastBeaconMs >= beaconIntervalMs)
  {
    lastBeaconMs = nowMs;
    transmitBeacon();
  }
}
