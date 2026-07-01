 /*
  FarmWhisper Site Scanner
  Version: 0.1.10-scan-complete

  Target hardware:
    - RAK4631 Core
    - RAK19003 base
    - External SH1106G OLED on I2C

  Display:
    - Bus: Wire
    - Address: 0x3C
    - Driver: Adafruit_SH110X / SH1106G

  Radio:
    - RAK4631 SX1262
    - LoRa P2P scan/survey tool
    - Uses SX126x-Arduino / SX126x-RAK4630 API, not generic RadioLib pin mapping

  Purpose:
    This is a practical field survey tool, not a lab spectrum analyzer.
    It sweeps a short list of 902-923 MHz slots, samples RSSI, runs a
    LoRa CAD check, watches for decodable packets, and recommends the
    quietest slot seen across the long-term survey run.

    Display behavior:
      - Stippled grey terrain shows accumulated ugliness points.
      - A 3-pixel-wide centered white vertical bar shows the latest RSSI level for each slot.
      - A bottom sweep marker shows the current scan slot.
      - A vertical center line marks the current best slot.
      - Text reports frequency, RSSI, activity hits, and samples.

  Required libraries:
    - Adafruit GFX Library
    - Adafruit SH110X
    - SX126x-Arduino / SX126x-RAK4630 support
*/

#include <Adafruit_TinyUSB.h>
#include <SX126x-RAK4630.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// -----------------------------------------------------------------------------
// Display configuration
// -----------------------------------------------------------------------------

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

Adafruit_SH1106G display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
bool displayOk = false;

// -----------------------------------------------------------------------------
// Battery voltage configuration
// -----------------------------------------------------------------------------

#ifdef WB_A0
#define PIN_VBAT WB_A0
#else
#define PIN_VBAT A0
#endif

#define VBAT_MV_PER_LSB (0.73242188F)
#define VBAT_DIVIDER_COMP (1.73F)
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

float batteryVolts = 0.0F;
uint32_t lastBatteryReadMs = 0;

// -----------------------------------------------------------------------------
// Radio configuration
// -----------------------------------------------------------------------------

// SX126x-Arduino bandwidth index:
//   0 = 125 kHz
//   1 = 250 kHz
//   2 = 500 kHz
#define LORA_BANDWIDTH 0
#define LORA_SPREADING_FACTOR 7

// SX126x-Arduino coding rate index:
//   1 = 4/5
//   2 = 4/6
//   3 = 4/7
//   4 = 4/8
#define LORA_CODINGRATE 1
#define LORA_PREAMBLE_LENGTH 8
#define LORA_SYMBOL_TIMEOUT 0
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false
#define RX_TIMEOUT_VALUE 0

// First-pass FarmWhisper survey slots.
// Kept to 902-923 MHz for a conservative RAK4631(H) first test.
// Add/remove slots later once we decide what the production channel plan is.
static const uint32_t scanFrequenciesHz[] = {
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

static const uint8_t scanSlotCount = sizeof(scanFrequenciesHz) / sizeof(scanFrequenciesHz[0]);

// Scan behavior.
static const uint16_t rssiSampleWindowMs = 240;
static const uint8_t rssiSampleDelayMs = 20;
static const uint16_t cadWaitMs = 120;
static const int16_t busyRssiThresholdDbm = -95;

// Display scale for latest-RSSI white level markers.
static const int16_t rssiScaleMinDbm = -130;
static const int16_t rssiScaleMaxDbm = -70;

// Display scale for the grey/stipple terrain.
// The terrain is intentionally not just long average RSSI. It is accumulated
// ugliness: repeated CAD, packet, busy, and elevated-RSSI events slowly build
// visible mass. This scale should not fill instantly; about an hour-long survey
// should still leave visible differences between slots.
static const uint32_t terrainFullScalePoints = 240;

// Simple scoring. Lower score is better.
// RSSI already trends lower when the slot is quiet.
static const int16_t cadPenaltyDb = 18;
static const int16_t packetPenaltyDb = 10;
static const int16_t busyPenaltyDb = 8;

static RadioEvents_t radioEvents;
bool radioOk = false;

// -----------------------------------------------------------------------------
// Scanner state
// -----------------------------------------------------------------------------

struct SlotStats {
  // Latest scan result. This keeps the display lively and useful while moving
  // the tool around, but it is not used by itself for the long-term best slot.
  int16_t rssiAvgDbm;
  int16_t rssiMinDbm;
  int16_t rssiMaxDbm;
  int16_t score;
  uint16_t samples;
  uint16_t packets;

  // Long-term survey result. These accumulate until reset or power cycle.
  // The best-slot marker is selected from these values.
  int32_t rssiLongSumDbm;
  int16_t rssiLongAvgDbm;
  int16_t longScore;
  uint32_t longSamples;
  uint32_t scans;
  uint32_t cadHits;
  uint32_t packetHits;
  uint32_t packetScanHits;
  uint32_t busyHits;

  // Display terrain accumulator. Higher means that slot has repeatedly shown
  // RF ugliness over time. It is separate from longScore because longScore is
  // used for picking the best slot, while uglyPoints is used to draw the grey
  // RF-terrain history.
  uint32_t uglyPoints;
};

SlotStats slotStats[scanSlotCount];

uint8_t currentSlot = 0;
uint8_t bestSlot = 0;
uint32_t sweepCount = 0;
uint32_t scanCount = 0;
bool scanComplete = false;

volatile bool scannerListening = false;
volatile bool cadDoneFlag = false;
volatile bool cadDetectedFlag = false;
volatile uint16_t slotPacketCount = 0;
volatile uint16_t slotErrorCount = 0;
volatile int16_t lastPacketRssi = 0;
volatile int8_t lastPacketSnr = 0;

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------

void serialBegin() {
  Serial.begin(115200);

  uint32_t start = millis();
  while (!Serial && millis() - start < 2500) {
    delay(10);
  }

  Serial.println();
  Serial.println("========================================");
  Serial.println(" FarmWhisper Site Scanner");
  Serial.println(" Version: 0.1.10-scan-complete");
  Serial.println(" Board : RAK4631 / RAK19003");
  Serial.println(" Radio : SX1262 LoRa slot scanner");
  Serial.println(" OLED  : SH1106G on Wire @ 0x3C");
  Serial.println(" Scan  : RSSI + CAD + packet hits");
  Serial.println("========================================");
  Serial.println();
}

void printCsvHeader() {
  Serial.println("[CSV] ms,scan,sweep,slot,freqMHz,rssiAvgDbm,rssiMinDbm,rssiMaxDbm,samples,cad,packets,busy,score,longSamples,rssiLongAvgDbm,cadPct,packetPct,busyPct,longScore,bestSlot,bestFreqMHz,batteryVolts");
}

void batteryBegin() {
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(5);
  analogRead(PIN_VBAT);
  delay(1);
  lastBatteryReadMs = 0;
}

float readBatteryVolts() {
  const uint8_t sampleCount = 8;
  uint32_t sum = 0;

  for (uint8_t i = 0; i < sampleCount; i++) {
    sum += analogRead(PIN_VBAT);
    delay(1);
  }

  float rawAverage = (float)sum / (float)sampleCount;
  float millivolts = rawAverage * REAL_VBAT_MV_PER_LSB;
  return millivolts / 1000.0F;
}

void updateBatteryVoltage(bool force) {
  if (!force && millis() - lastBatteryReadMs < 5000) {
    return;
  }

  lastBatteryReadMs = millis();
  batteryVolts = readBatteryVolts();
}

float freqMHz(uint32_t freqHz) {
  return (float)freqHz / 1000000.0F;
}

int16_t clampRssi(int16_t rssi) {
  if (rssi < rssiScaleMinDbm) {
    return rssiScaleMinDbm;
  }

  if (rssi > rssiScaleMaxDbm) {
    return rssiScaleMaxDbm;
  }

  return rssi;
}

void resetStats() {
  for (uint8_t i = 0; i < scanSlotCount; i++) {
    slotStats[i].rssiAvgDbm = rssiScaleMinDbm;
    slotStats[i].rssiMinDbm = rssiScaleMinDbm;
    slotStats[i].rssiMaxDbm = rssiScaleMinDbm;
    slotStats[i].score = 32767;
    slotStats[i].samples = 0;
    slotStats[i].packets = 0;
    slotStats[i].rssiLongSumDbm = 0;
    slotStats[i].rssiLongAvgDbm = rssiScaleMinDbm;
    slotStats[i].longScore = 32767;
    slotStats[i].longSamples = 0;
    slotStats[i].scans = 0;
    slotStats[i].cadHits = 0;
    slotStats[i].packetHits = 0;
    slotStats[i].packetScanHits = 0;
    slotStats[i].busyHits = 0;
    slotStats[i].uglyPoints = 0;
  }
}

// -----------------------------------------------------------------------------
// Display helpers
// -----------------------------------------------------------------------------

void drawBatteryTopRight() {
  if (!displayOk) {
    return;
  }

  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(98, 0);
  display.print(batteryVolts, 2);
  display.print("v");
}

void displayBegin() {
  Wire.begin();
  Wire.setClock(100000);

  displayOk = display.begin(OLED_ADDR, true);
  if (!displayOk) {
    Serial.println("[OLED] Init failed");
    return;
  }

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("fwSiteScanner");
  drawBatteryTopRight();
  display.setCursor(0, 10);
  display.println("v0.1.10");
  display.println();
  display.println("OLED OK");
  display.display();
  delay(3000);

  Serial.println("[OLED] SH1106G initialized on Wire @ 0x3C");
}

void drawBootStatus(const char *line) {
  if (!displayOk) {
    return;
  }

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("fwSiteScanner");
  drawBatteryTopRight();
  display.setCursor(0, 12);
  display.println(line);
  display.println();
  display.print("Slots: ");
  display.println(scanSlotCount);
  display.print("Mode : SF");
  display.print(LORA_SPREADING_FACTOR);
  display.println(" BW125");
  display.display();
  delay(3000);
}

int graphHeightFromValue(int16_t value, int graphH) {
  int16_t scaled = clampRssi(value);
  return map(scaled, rssiScaleMinDbm, rssiScaleMaxDbm, 1, graphH);
}

int graphHeightFromUglyPoints(uint32_t uglyPoints, int graphH) {
  if (uglyPoints == 0) {
    return 0;
  }

  if (uglyPoints >= terrainFullScalePoints) {
    return graphH;
  }

  int h = (int)((uglyPoints * (uint32_t)graphH) / terrainFullScalePoints);
  if (h < 1) {
    h = 1;
  }
  return h;
}

bool anyTerrainFull() {
  for (uint8_t i = 0; i < scanSlotCount; i++) {
    if (slotStats[i].uglyPoints >= terrainFullScalePoints) {
      return true;
    }
  }
  return false;
}

uint16_t uglyPointsForScan(int16_t rssiAvgDbm, bool cadDetected, uint16_t packetCount, bool busyDetected) {
  uint16_t points = 0;

  // RSSI contributes only when it is meaningfully above the quiet floor.
  // This prevents every normal quiet scan from immediately painting the
  // whole display grey, while still letting noisier slots slowly rise.
  int16_t rssiAboveFloor = rssiAvgDbm - rssiScaleMinDbm;
  if (rssiAboveFloor > 8) {
    points += (uint16_t)((rssiAboveFloor - 8) / 4);
  }

  if (cadDetected) {
    points += 8;
  }

  if (packetCount > 0) {
    points += 12;
  }

  if (busyDetected) {
    points += 10;
  }

  return points;
}

void drawDitheredRect(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) {
    return;
  }

  // The SH1106G is monochrome, so "grey" is a checkerboard/stipple.
  // This is the long-term terrain layer and is drawn first. We only draw
  // the white pixels in the pattern; the display was cleared before this,
  // and the current RSSI bars are drawn solid white over the top afterward.
  for (int py = y; py < y + h; py++) {
    for (int px = x; px < x + w; px++) {
      if (((px + py) & 0x01) == 0) {
        display.drawPixel(px, py, SH110X_WHITE);
      }
    }
  }
}

void drawScannerScreen() {
  if (!displayOk) {
    return;
  }

  SlotStats best = slotStats[bestSlot];
  SlotStats current = slotStats[currentSlot];

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("fwSiteScanner");
  drawBatteryTopRight();

  display.setCursor(0, 10);
  display.print(">Use ");
  display.print(freqMHz(scanFrequenciesHz[bestSlot]), 1);
  display.print(" MHz ");
  display.print(best.rssiLongAvgDbm);
  display.print("db");

  const int graphX = 0;
  const int graphY = 20;
  const int graphW = 128;
  const int graphH = 29;
  const int slotW = graphW / scanSlotCount;
  const int baselineY = graphY + graphH;

  // First layer: long-term cumulative terrain. This is the stippled/grey
  // history layer. Repeated CAD, packet, busy, or elevated RSSI results lift
  // this mass over time. It must be drawn before the white sample bars.
  for (uint8_t i = 0; i < scanSlotCount; i++) {
    // Spread the channel columns across the whole graph width. Integer division
    // with a fixed slotW left unused pixels on the right side, which made the
    // last channel look like it was never being written.
    int x = graphX + ((int)i * graphW) / scanSlotCount;
    int nextX = graphX + ((int)(i + 1) * graphW) / scanSlotCount;
    int barW = nextX - x - 1;
    if (barW < 2) {
      barW = 2;
    }

    if (slotStats[i].longSamples > 0) {
      int terrainH = graphHeightFromUglyPoints(slotStats[i].uglyPoints, graphH);
      int terrainY = graphY + graphH - terrainH;
      drawDitheredRect(x, terrainY, barW, terrainH);
    }
  }

  // Second layer: latest RSSI samples, drawn as narrow centered vertical
  // white bars. The full-width stippled terrain remains visible on both sides
  // of each current-RSSI bar while still showing the current signal height.
  for (uint8_t i = 0; i < scanSlotCount; i++) {
    int x = graphX + ((int)i * graphW) / scanSlotCount;
    int nextX = graphX + ((int)(i + 1) * graphW) / scanSlotCount;
    int barW = nextX - x - 1;
    if (barW < 2) {
      barW = 2;
    }

    int centerX = x + (barW / 2);
    int markerW = 3;
    int markerX = centerX - 1;

    if (markerX < x) {
      markerX = x;
    }
    if (markerX + markerW > x + barW) {
      markerX = x + barW - markerW;
    }
    if (markerX < x) {
      markerX = x;
      markerW = barW;
    }

    if (slotStats[i].samples == 0) {
      display.drawFastVLine(centerX, baselineY - 2, 2, SH110X_WHITE);
    } else {
      int currentBarH = graphHeightFromValue(slotStats[i].rssiAvgDbm, graphH);
      int currentY = graphY + graphH - currentBarH;

      if (currentY < graphY) {
        currentY = graphY;
      }
      if (currentY > baselineY - 1) {
        currentY = baselineY - 1;
      }

      display.fillRect(markerX, currentY, markerW, baselineY - currentY, SH110X_WHITE);
    }

    // Activity dot stays near the top of the graph area. It is still useful
    // for spotting LoRa-like activity even when a packet was not decoded.
    if (slotStats[i].cadHits > 0 || slotStats[i].packetScanHits > 0) {
      display.drawPixel(centerX, graphY - 1, SH110X_WHITE);
    }
  }

  display.drawLine(graphX, baselineY, graphX + graphW - 1, baselineY, SH110X_WHITE);

  // Sweep indicator: bottom tick under the channel being scanned now. This
  // keeps the top of the graph clean and stops it from looking like another
  // signal/activity marker.
  int currentX = graphX + ((int)currentSlot * graphW) / scanSlotCount;
  int currentNextX = graphX + ((int)(currentSlot + 1) * graphW) / scanSlotCount;
  int currentBarW = currentNextX - currentX - 1;
  if (currentBarW < 2) {
    currentBarW = 2;
  }
  display.drawLine(currentX, baselineY + 2, currentX + currentBarW - 1, baselineY + 2, SH110X_WHITE);

  // Best slot marker: centered vertical line, drawn last so it is visible
  // over both the grey terrain and the white RSSI bar. The numeric frequency
  // remains in the Best text line.
  int bestX = graphX + ((int)bestSlot * graphW) / scanSlotCount;
  int bestNextX = graphX + ((int)(bestSlot + 1) * graphW) / scanSlotCount;
  bestX += (bestNextX - bestX) / 2;
  display.drawFastVLine(bestX, graphY, graphH, SH110X_WHITE);

  display.setCursor(0, 55);
  if (scanComplete) {
    display.print("Scan Complete!");
  } else {
    display.print(freqMHz(scanFrequenciesHz[currentSlot]), 1);
    display.print(" ");
    display.print(current.rssiAvgDbm);
    display.print(" P");
    display.print(current.cadHits + current.packetScanHits);
    display.print(" #");
    display.print(current.longSamples);
  }

  display.display();
}

// -----------------------------------------------------------------------------
// Radio callbacks
// -----------------------------------------------------------------------------

void onRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  (void)payload;
  (void)size;

  slotPacketCount++;
  lastPacketRssi = rssi;
  lastPacketSnr = snr;

  if (scannerListening) {
    Radio.Rx(RX_TIMEOUT_VALUE);
  }
}

void onRxTimeout() {
  if (scannerListening) {
    Radio.Rx(RX_TIMEOUT_VALUE);
  }
}

void onRxError() {
  slotErrorCount++;

  if (scannerListening) {
    Radio.Rx(RX_TIMEOUT_VALUE);
  }
}

void onCadDone(bool channelActivityDetected) {
  cadDetectedFlag = channelActivityDetected;
  cadDoneFlag = true;
}

// -----------------------------------------------------------------------------
// Radio setup and scanning
// -----------------------------------------------------------------------------

void applyRxConfig() {
  Radio.SetRxConfig(
    MODEM_LORA,
    LORA_BANDWIDTH,
    LORA_SPREADING_FACTOR,
    LORA_CODINGRATE,
    0,
    LORA_PREAMBLE_LENGTH,
    LORA_SYMBOL_TIMEOUT,
    LORA_FIX_LENGTH_PAYLOAD_ON,
    0,
    true,
    0,
    0,
    LORA_IQ_INVERSION_ON,
    true
  );
}

void radioBegin() {
  Serial.println("[RADIO] Hardware init");
  drawBootStatus("Radio init...");

  lora_rak4630_init();

  radioEvents.TxDone = NULL;
  radioEvents.TxTimeout = NULL;
  radioEvents.RxDone = onRxDone;
  radioEvents.RxTimeout = onRxTimeout;
  radioEvents.RxError = onRxError;
  radioEvents.PreAmpDetect = NULL;
  radioEvents.FhssChangeChannel = NULL;
  radioEvents.CadDone = onCadDone;

  Radio.Init(&radioEvents);
  Radio.SetChannel(scanFrequenciesHz[0]);
  applyRxConfig();
  Radio.Standby();

  radioOk = true;

  Serial.println("[RADIO] Configured for LoRa P2P slot scanning");
  Serial.print("[RADIO] Slots: ");
  Serial.println(scanSlotCount);
  Serial.print("[RADIO] SF: ");
  Serial.print(LORA_SPREADING_FACTOR);
  Serial.print(" BW index: ");
  Serial.print(LORA_BANDWIDTH);
  Serial.print(" CR index: ");
  Serial.println(LORA_CODINGRATE);

  drawBootStatus("Scanner ready");
}

bool runCad(uint32_t freqHz) {
  cadDoneFlag = false;
  cadDetectedFlag = false;

  Radio.Standby();
  Radio.SetChannel(freqHz);
  applyRxConfig();

  // SX126x CAD parameters. Numeric values keep this sketch independent of
  // enum names that may vary across SX126x-Arduino versions.
  // cadSymbolNum: 0x04 = 8 symbols
  // cadExitMode : 0x00 = CAD only
  Radio.SetCadParams(0x04, 22, 10, 0x00, 0);
  Radio.StartCad();

  uint32_t startMs = millis();
  while (!cadDoneFlag && millis() - startMs < cadWaitMs) {
    Radio.IrqProcess();
    delay(1);
  }

  Radio.Standby();
  return cadDoneFlag && cadDetectedFlag;
}

uint8_t hitPercent(uint32_t hits, uint32_t scans) {
  if (scans == 0) {
    return 0;
  }

  uint32_t pct = (hits * 100UL + (scans / 2UL)) / scans;
  if (pct > 100UL) {
    pct = 100UL;
  }

  return (uint8_t)pct;
}

void updateLongScore(SlotStats &s) {
  if (s.longSamples == 0 || s.scans == 0) {
    s.rssiLongAvgDbm = rssiScaleMinDbm;
    s.longScore = 32767;
    return;
  }

  s.rssiLongAvgDbm = (int16_t)(s.rssiLongSumDbm / (int32_t)s.longSamples);

  int32_t cadPenalty = ((int32_t)cadPenaltyDb * (int32_t)s.cadHits + ((int32_t)s.scans / 2)) / (int32_t)s.scans;
  int32_t packetPenalty = ((int32_t)packetPenaltyDb * (int32_t)s.packetScanHits + ((int32_t)s.scans / 2)) / (int32_t)s.scans;
  int32_t busyPenalty = ((int32_t)busyPenaltyDb * (int32_t)s.busyHits + ((int32_t)s.scans / 2)) / (int32_t)s.scans;

  s.longScore = (int16_t)((int32_t)s.rssiLongAvgDbm + cadPenalty + packetPenalty + busyPenalty);
}

void findBestSlot() {
  int16_t bestScore = 32767;
  uint8_t chosen = bestSlot;

  for (uint8_t i = 0; i < scanSlotCount; i++) {
    if (slotStats[i].longSamples == 0) {
      continue;
    }

    if (slotStats[i].longScore < bestScore) {
      bestScore = slotStats[i].longScore;
      chosen = i;
    }
  }

  bestSlot = chosen;
}

void handleSerialCommands() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == 'r' || c == 'R') {
      scanComplete = false;
      resetStats();
      currentSlot = 0;
      bestSlot = 0;
      sweepCount = 0;
      scanCount = 0;
      Serial.println("[SCANNER] Survey stats reset");
      drawScannerScreen();
    }
  }
}

void printSlotResult(uint8_t slot, bool cadDetected, bool busyDetected) {
  SlotStats s = slotStats[slot];

  Serial.print("[SCAN] slot=");
  Serial.print(slot);
  Serial.print(" freq=");
  Serial.print(freqMHz(scanFrequenciesHz[slot]), 3);
  Serial.print("MHz rssiAvg=");
  Serial.print(s.rssiAvgDbm);
  Serial.print(" min=");
  Serial.print(s.rssiMinDbm);
  Serial.print(" max=");
  Serial.print(s.rssiMaxDbm);
  Serial.print(" cad=");
  Serial.print(cadDetected ? "Y" : "N");
  Serial.print(" packets=");
  Serial.print(s.packets);
  Serial.print(" busy=");
  Serial.print(busyDetected ? "Y" : "N");
  Serial.print(" score=");
  Serial.print(s.score);
  Serial.print(" longAvg=");
  Serial.print(s.rssiLongAvgDbm);
  Serial.print(" longScore=");
  Serial.print(s.longScore);
  Serial.print(" cadPct=");
  Serial.print(hitPercent(s.cadHits, s.scans));
  Serial.print(" pktPct=");
  Serial.print(hitPercent(s.packetScanHits, s.scans));
  Serial.print(" busyPct=");
  Serial.print(hitPercent(s.busyHits, s.scans));
  Serial.print(" best=");
  Serial.print(freqMHz(scanFrequenciesHz[bestSlot]), 3);
  Serial.println("MHz");

  Serial.print("[CSV] ");
  Serial.print(millis());
  Serial.print(",");
  Serial.print(scanCount);
  Serial.print(",");
  Serial.print(sweepCount);
  Serial.print(",");
  Serial.print(slot);
  Serial.print(",");
  Serial.print(freqMHz(scanFrequenciesHz[slot]), 3);
  Serial.print(",");
  Serial.print(s.rssiAvgDbm);
  Serial.print(",");
  Serial.print(s.rssiMinDbm);
  Serial.print(",");
  Serial.print(s.rssiMaxDbm);
  Serial.print(",");
  Serial.print(s.samples);
  Serial.print(",");
  Serial.print(cadDetected ? 1 : 0);
  Serial.print(",");
  Serial.print(s.packets);
  Serial.print(",");
  Serial.print(busyDetected ? 1 : 0);
  Serial.print(",");
  Serial.print(s.score);
  Serial.print(",");
  Serial.print(s.longSamples);
  Serial.print(",");
  Serial.print(s.rssiLongAvgDbm);
  Serial.print(",");
  Serial.print(hitPercent(s.cadHits, s.scans));
  Serial.print(",");
  Serial.print(hitPercent(s.packetScanHits, s.scans));
  Serial.print(",");
  Serial.print(hitPercent(s.busyHits, s.scans));
  Serial.print(",");
  Serial.print(s.longScore);
  Serial.print(",");
  Serial.print(bestSlot);
  Serial.print(",");
  Serial.print(freqMHz(scanFrequenciesHz[bestSlot]), 3);
  Serial.print(",");
  Serial.println(batteryVolts, 2);
}

void scanOneSlot(uint8_t slot) {
  const uint32_t freqHz = scanFrequenciesHz[slot];

  slotPacketCount = 0;
  slotErrorCount = 0;
  lastPacketRssi = 0;
  lastPacketSnr = 0;

  Radio.Standby();
  Radio.SetChannel(freqHz);
  applyRxConfig();

  int32_t rssiSum = 0;
  int16_t rssiMin = 32767;
  int16_t rssiMax = -32768;
  uint16_t rssiSamples = 0;

  scannerListening = true;
  Radio.RxBoosted(RX_TIMEOUT_VALUE);

  const uint32_t startMs = millis();
  while (millis() - startMs < rssiSampleWindowMs) {
    Radio.IrqProcess();

    int16_t rssi = Radio.Rssi(MODEM_LORA);
    if (rssi > -160 && rssi < 10) {
      rssiSum += rssi;
      if (rssi < rssiMin) {
        rssiMin = rssi;
      }
      if (rssi > rssiMax) {
        rssiMax = rssi;
      }
      rssiSamples++;
    }

    delay(rssiSampleDelayMs);
  }

  scannerListening = false;
  Radio.Standby();

  bool cadDetected = runCad(freqHz);
  bool busyDetected = false;

  SlotStats &s = slotStats[slot];
  s.scans++;
  s.samples = rssiSamples;
  s.packets = slotPacketCount;
  s.packetHits += slotPacketCount;

  if (rssiSamples > 0) {
    s.rssiAvgDbm = (int16_t)(rssiSum / (int32_t)rssiSamples);
    s.rssiMinDbm = rssiMin;
    s.rssiMaxDbm = rssiMax;
    s.rssiLongSumDbm += s.rssiAvgDbm;
    s.longSamples++;
    busyDetected = s.rssiAvgDbm >= busyRssiThresholdDbm;
  } else {
    s.rssiAvgDbm = rssiScaleMinDbm;
    s.rssiMinDbm = rssiScaleMinDbm;
    s.rssiMaxDbm = rssiScaleMinDbm;
    busyDetected = false;
  }

  if (cadDetected) {
    s.cadHits++;
  }

  if (slotPacketCount > 0) {
    s.packetScanHits++;
  }

  if (busyDetected) {
    s.busyHits++;
  }

  s.uglyPoints += uglyPointsForScan(s.rssiAvgDbm, cadDetected, slotPacketCount, busyDetected);
  if (s.uglyPoints >= terrainFullScalePoints) {
    s.uglyPoints = terrainFullScalePoints;
    if (!scanComplete) {
      scanComplete = true;
      Serial.println("[SCANNER] Scan Complete!");
    }
  }

  s.score = s.rssiAvgDbm;
  if (cadDetected) {
    s.score += cadPenaltyDb;
  }
  if (slotPacketCount > 0) {
    s.score += packetPenaltyDb;
  }
  if (busyDetected) {
    s.score += busyPenaltyDb;
  }

  updateLongScore(s);

  scanCount++;
  findBestSlot();
  printSlotResult(slot, cadDetected, busyDetected);
  drawScannerScreen();
}

// -----------------------------------------------------------------------------
// Arduino setup / loop
// -----------------------------------------------------------------------------

void setup() {
#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
#endif

  serialBegin();
  batteryBegin();
  updateBatteryVoltage(true);
  displayBegin();
  resetStats();
  radioBegin();
  printCsvHeader();

  Serial.println();
  Serial.println("[SCANNER] Running. Narrow white RSSI bars draw over accumulated grey terrain; bottom tick marks current slot; vertical line marks best slot.");
  Serial.println("[SCANNER] Let it run until Scan Complete or long enough for the site; power-cycle or press R in Serial Monitor to reset.");
  Serial.println("[SCANNER] Rotate the tool, move it around, and compare relative readings.");
  Serial.println();
}

void loop() {
  updateBatteryVoltage(false);
  handleSerialCommands();

  if (scanComplete) {
    Radio.Standby();
    delay(1000);
    return;
  }

  if (!radioOk) {
    delay(1000);
    return;
  }

  currentSlot = currentSlot % scanSlotCount;
  scanOneSlot(currentSlot);

  if (scanComplete) {
    Radio.Standby();
    return;
  }

#ifdef LED_BUILTIN
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
#endif

  currentSlot++;
  if (currentSlot >= scanSlotCount) {
    currentSlot = 0;
    sweepCount++;

    Serial.print("[SWEEP] ");
    Serial.print(sweepCount);
    Serial.print(" bestSlot=");
    Serial.print(bestSlot);
    Serial.print(" bestFreq=");
    Serial.print(freqMHz(scanFrequenciesHz[bestSlot]), 3);
    Serial.print("MHz longScore=");
    Serial.print(slotStats[bestSlot].longScore);
    Serial.print(" longAvg=");
    Serial.print(slotStats[bestSlot].rssiLongAvgDbm);
    Serial.print(" samples=");
    Serial.print(slotStats[bestSlot].longSamples);
    Serial.print(" batt=");
    Serial.print(batteryVolts, 2);
    Serial.println("v");
  }
}
