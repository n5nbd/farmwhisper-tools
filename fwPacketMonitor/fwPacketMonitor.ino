/*
  FarmWhisper RF Monitor Tool
  Version: 0.1.1-monitor-test

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
    - Passive receive-only LoRa P2P monitor
    - Prints packets to USB serial
    - Shows last packet summary on OLED

  Purpose:
    This is a bench/survey/monitor tool, not a full FarmWhisper app.
    It intentionally stays in one .ino file for now.

  Required libraries:
    - Adafruit GFX Library
    - Adafruit SH110X
    - SX126x-Arduino / SX126x-RAK4630 support
*/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#include <SX126x-RAK4630.h>

// -----------------------------------------------------------------------------
// Display configuration
// -----------------------------------------------------------------------------

#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_RESET      -1
#define OLED_ADDR       0x3C

Adafruit_SH1106G display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

bool displayOk = false;

// -----------------------------------------------------------------------------
// Radio configuration
// -----------------------------------------------------------------------------

#define RF_FREQUENCY                915000000UL

// SX126x-Arduino bandwidth index:
//   0 = 125 kHz
//   1 = 250 kHz
//   2 = 500 kHz
#define LORA_BANDWIDTH              0

#define LORA_SPREADING_FACTOR       7

// SX126x-Arduino coding rate index:
//   1 = 4/5
//   2 = 4/6
//   3 = 4/7
//   4 = 4/8
#define LORA_CODINGRATE             1

#define LORA_PREAMBLE_LENGTH        8
#define LORA_SYMBOL_TIMEOUT         0
#define LORA_FIX_LENGTH_PAYLOAD_ON  false
#define LORA_IQ_INVERSION_ON        false
#define RX_TIMEOUT_VALUE            0

static RadioEvents_t RadioEvents;

bool radioOk = false;

// -----------------------------------------------------------------------------
// SNR gauge configuration
//
// Display scale:
//   -50 dB = far left
//     0 dB = center
//   +30 dB = far right
//
// This is intentionally piecewise so 0 dB stays visually centered.
// -----------------------------------------------------------------------------

#define SNR_GAUGE_MIN_DB   -50
#define SNR_GAUGE_ZERO_DB    0
#define SNR_GAUGE_MAX_DB    30

// -----------------------------------------------------------------------------
// Packet state
// -----------------------------------------------------------------------------

#define PACKET_BUF_SIZE 256
#define TEXT_PREVIEW_SIZE 96

volatile bool packetPending = false;
volatile bool rxTimeoutSeen = false;
volatile bool rxErrorSeen = false;

uint8_t packetBuf[PACKET_BUF_SIZE];
uint16_t packetLen = 0;
int16_t packetRssi = 0;
int8_t packetSnr = 0;

uint32_t rxCount = 0;
uint32_t rxErrorCount = 0;
uint32_t rxTimeoutCount = 0;
uint32_t loopTick = 0;
uint32_t lastRxMs = 0;

char lastText[TEXT_PREVIEW_SIZE];

// -----------------------------------------------------------------------------
// Serial helpers
// -----------------------------------------------------------------------------

void serialBegin()
{
  Serial.begin(115200);

  uint32_t start = millis();
  while (!Serial && millis() - start < 2500)
  {
    delay(10);
  }

  Serial.println();
  Serial.println("========================================");
  Serial.println(" FarmWhisper RF Monitor Tool");
  Serial.println(" Version: 0.1.1-monitor-test");
  Serial.println(" Board  : RAK4631 / RAK19003");
  Serial.println(" Radio  : SX1262 passive RX");
  Serial.println(" OLED   : SH1106G on Wire @ 0x3C");
  Serial.println(" Gauge  : SNR -50..0..+30 dB");
  Serial.println("========================================");
  Serial.println();
}

// -----------------------------------------------------------------------------
// Display helpers
// -----------------------------------------------------------------------------

void displayBegin()
{
  Wire.begin();
  Wire.setClock(100000);

  displayOk = display.begin(OLED_ADDR, true);

  if (!displayOk)
  {
    Serial.println("[OLED] Init failed");
    return;
  }

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);

  display.println("FarmWhisper");
  display.println("RF Monitor");
  display.println();
  display.println("OLED OK");
  display.println("Waiting radio...");

  display.display();

  Serial.println("[OLED] SH1106G initialized on Wire @ 0x3C");
}

void drawBootStatus(const char *line)
{
  if (!displayOk)
  {
    return;
  }

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);

  display.println("FarmWhisper");
  display.println("RF Monitor");
  display.println();

  display.println(line);
  display.println();

  display.print("Freq ");
  display.print(RF_FREQUENCY / 1000000.0, 3);
  display.println(" MHz");

  display.print("SF");
  display.print(LORA_SPREADING_FACTOR);
  display.print(" BW125 CR4/5");

  display.display();
}

int snrToGaugeX(int snr, int x, int w)
{
  int center = x + (w / 2);

  if (snr <= SNR_GAUGE_ZERO_DB)
  {
    snr = constrain(snr, SNR_GAUGE_MIN_DB, SNR_GAUGE_ZERO_DB);
    return map(snr, SNR_GAUGE_MIN_DB, SNR_GAUGE_ZERO_DB, x, center);
  }

  snr = constrain(snr, SNR_GAUGE_ZERO_DB, SNR_GAUGE_MAX_DB);
  return map(snr, SNR_GAUGE_ZERO_DB, SNR_GAUGE_MAX_DB, center, x + w - 1);
}

void drawSnrGauge(int x, int y, int w, int h, int snr)
{
  int center = x + (w / 2);
  int marker = snrToGaugeX(snr, x, w);

  // Outer gauge box
  display.drawRect(x, y, w, h, SH110X_WHITE);

  // 0 dB center tick
  display.drawLine(center, y - 1, center, y + h, SH110X_WHITE);

  // Fill from 0 toward current SNR.
  // Negative SNR fills left from center.
  // Positive SNR fills right from center.
  if (marker < center)
  {
    display.fillRect(marker, y + 2, center - marker, h - 4, SH110X_WHITE);
  }
  else if (marker > center)
  {
    display.fillRect(center, y + 2, marker - center, h - 4, SH110X_WHITE);
  }

  // Current SNR marker
  display.drawLine(marker, y, marker, y + h - 1, SH110X_WHITE);
}

void drawMonitorScreen()
{
  if (!displayOk)
  {
    return;
  }

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("FW MON ");
  display.print(radioOk ? "RX" : "NO RADIO");
  display.print(" #");
  display.println(rxCount);

  display.setCursor(0, 10);
  display.print("RSSI ");
  display.print(packetRssi);
  display.print(" SNR ");
  display.print(packetSnr);
  display.print(" L");
  display.println(packetLen);

  display.setCursor(0, 20);
  display.print("Err ");
  display.print(rxErrorCount);
  display.print(" Tmo ");
  display.println(rxTimeoutCount);

  // SNR gauge replaces the old dashed separator line.
  // Scale: -50 dB left, 0 dB center, +30 dB right.
  drawSnrGauge(0, 31, 128, 8, packetSnr);

  char line1[22];
  char line2[22];

  memset(line1, 0, sizeof(line1));
  memset(line2, 0, sizeof(line2));

  strncpy(line1, lastText, 21);

  if (strlen(lastText) > 21)
  {
    strncpy(line2, lastText + 21, 21);
  }

  display.setCursor(0, 43);
  display.println(line1);

  display.setCursor(0, 54);
  display.println(line2);

  display.display();
}

void drawIdleScreen()
{
  if (!displayOk)
  {
    return;
  }

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);

  display.println("FW RF MONITOR");
  display.println("Listening...");
  display.println();

  display.print("RX count: ");
  display.println(rxCount);

  display.print("Errors  : ");
  display.println(rxErrorCount);

  display.print("Uptime  : ");
  display.print(millis() / 1000);
  display.println("s");

  display.println();
  display.print("915 SF");
  display.print(LORA_SPREADING_FACTOR);
  display.println(" BW125");

  display.display();
}

// -----------------------------------------------------------------------------
// Payload formatting
// -----------------------------------------------------------------------------

bool isPrintableByte(uint8_t b)
{
  return b >= 32 && b <= 126;
}

void makeTextPreview()
{
  uint16_t out = 0;

  for (uint16_t i = 0; i < packetLen && out < TEXT_PREVIEW_SIZE - 1; i++)
  {
    uint8_t b = packetBuf[i];

    if (isPrintableByte(b))
    {
      lastText[out++] = (char)b;
    }
    else
    {
      lastText[out++] = '.';
    }
  }

  lastText[out] = '\0';

  if (out == 0)
  {
    strcpy(lastText, "(empty)");
  }
}

void printHexPayload()
{
  Serial.print("[HEX] ");

  for (uint16_t i = 0; i < packetLen; i++)
  {
    if (packetBuf[i] < 16)
    {
      Serial.print("0");
    }

    Serial.print(packetBuf[i], HEX);

    if (i + 1 < packetLen)
    {
      Serial.print(" ");
    }
  }

  Serial.println();
}

void printAsciiPayload()
{
  Serial.print("[ASCII] \"");

  for (uint16_t i = 0; i < packetLen; i++)
  {
    uint8_t b = packetBuf[i];

    if (isPrintableByte(b))
    {
      Serial.print((char)b);
    }
    else
    {
      Serial.print(".");
    }
  }

  Serial.println("\"");
}

void printPacket()
{
  Serial.println();
  Serial.println("----------------------------------------");

  Serial.print("[RX] count=");
  Serial.print(rxCount);

  Serial.print(" len=");
  Serial.print(packetLen);

  Serial.print(" rssi=");
  Serial.print(packetRssi);

  Serial.print(" snr=");
  Serial.print(packetSnr);

  Serial.print(" uptime=");
  Serial.print(millis() / 1000);
  Serial.println("s");

  printAsciiPayload();
  printHexPayload();

  Serial.println("----------------------------------------");
}

// -----------------------------------------------------------------------------
// Radio callbacks
// -----------------------------------------------------------------------------

void onRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr)
{
  uint16_t copyLen = size;

  if (copyLen > PACKET_BUF_SIZE)
  {
    copyLen = PACKET_BUF_SIZE;
  }

  memcpy(packetBuf, payload, copyLen);

  packetLen = copyLen;
  packetRssi = rssi;
  packetSnr = snr;
  packetPending = true;

  // Re-arm receive immediately.
  Radio.Rx(RX_TIMEOUT_VALUE);
}

void onRxTimeout()
{
  rxTimeoutSeen = true;

  // Continuous RX should normally not timeout with RX_TIMEOUT_VALUE = 0,
  // but re-arm anyway so the monitor is self-healing.
  Radio.Rx(RX_TIMEOUT_VALUE);
}

void onRxError()
{
  rxErrorSeen = true;

  // Re-arm receive after malformed/noisy packets.
  Radio.Rx(RX_TIMEOUT_VALUE);
}

// -----------------------------------------------------------------------------
// Radio setup
// -----------------------------------------------------------------------------

void radioBegin()
{
  Serial.println("[RADIO] Hardware init");

  drawBootStatus("Radio init...");

  lora_rak4630_init();

  RadioEvents.RxDone = onRxDone;
  RadioEvents.RxTimeout = onRxTimeout;
  RadioEvents.RxError = onRxError;

  Radio.Init(&RadioEvents);

  Serial.println("[RADIO] Setting channel/config");

  Radio.SetChannel(RF_FREQUENCY);

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

  radioOk = true;

  Serial.println("[RADIO] Configured for LoRa P2P passive RX");

  Serial.print("[RADIO] Frequency: ");
  Serial.println(RF_FREQUENCY);

  Serial.print("[RADIO] BW index : ");
  Serial.println(LORA_BANDWIDTH);

  Serial.print("[RADIO] SF       : ");
  Serial.println(LORA_SPREADING_FACTOR);

  Serial.print("[RADIO] CR index : ");
  Serial.println(LORA_CODINGRATE);

  drawBootStatus("Radio RX active");

  Radio.Rx(RX_TIMEOUT_VALUE);
}

// -----------------------------------------------------------------------------
// Arduino setup / loop
// -----------------------------------------------------------------------------

void setup()
{
#ifdef LED_BUILTIN
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
#endif

  serialBegin();

  displayBegin();
  drawBootStatus("Booting...");

  radioBegin();

  strcpy(lastText, "waiting for packets");

  Serial.println();
  Serial.println("[MONITOR] Listening for FarmWhisper packets...");
  Serial.println();

  drawIdleScreen();
}

void loop()
{
  Radio.IrqProcess();

  if (packetPending)
  {
    packetPending = false;

    rxCount++;
    lastRxMs = millis();

#ifdef LED_BUILTIN
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
#endif

    makeTextPreview();
    printPacket();
    drawMonitorScreen();
  }

  if (rxErrorSeen)
  {
    rxErrorSeen = false;
    rxErrorCount++;

    Serial.print("[RADIO] RX error count=");
    Serial.println(rxErrorCount);

    drawMonitorScreen();
  }

  if (rxTimeoutSeen)
  {
    rxTimeoutSeen = false;
    rxTimeoutCount++;

    Serial.print("[RADIO] RX timeout count=");
    Serial.println(rxTimeoutCount);
  }

  // Lightweight idle refresh so the OLED does not look frozen before packets.
  if (millis() - loopTick >= 5000)
  {
    loopTick = millis();

    Serial.print("[MONITOR] listening uptime=");
    Serial.print(millis() / 1000);
    Serial.print("s rx=");
    Serial.print(rxCount);
    Serial.print(" err=");
    Serial.println(rxErrorCount);

    if (rxCount == 0)
    {
      drawIdleScreen();
    }
  }
}
