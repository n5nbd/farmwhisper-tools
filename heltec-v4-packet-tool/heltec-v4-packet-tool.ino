/*
  FarmWhisper Tools
  Heltec V4 Packet Tool
  Version 0.1.0

  Board:
    Official Heltec WiFi LoRa 32 V4
    ESP32-S3R2 + SX1262 + onboard 128x64 OLED

  Purpose:
    This is a dead-simple LoRa packet monitor.

    It does not act as a FarmWhisper station.
    It does not transmit.
    It has no buttons, menus, config screens, BLE, Wi-Fi, or state machine.

    It listens on the configured LoRa P2P settings and prints every valid packet
    it receives to both:
      - USB serial
      - Built-in OLED display

  Important:
    This is not a true wideband RF sniffer. LoRa receive must match the sender's
    frequency, bandwidth, spreading factor, coding rate, sync word, header mode,
    and CRC behavior. If the tool is quiet, match those settings first.

  Required Arduino libraries:
    - RadioLib
    - Adafruit GFX Library
    - Adafruit SSD1306

  Arduino IDE notes:
    - Board should be the official Heltec WiFi LoRa 32 V4 or matching ESP32-S3 target.
    - Serial Monitor: 115200 baud.
    - If serial is silent on native USB, enable "USB CDC On Boot" in Tools.
    - For Heltec V4 hardware revision, select the correct LoRa FEM option in Tools
      when using the Heltec board package.
*/

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include <RadioLib.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// -----------------------------------------------------------------------------
// Tool identity
// -----------------------------------------------------------------------------

static const char *TOOL_NAME    = "FW Heltec V4 Tool";
static const char *TOOL_VERSION = "0.1.0";

// -----------------------------------------------------------------------------
// Serial configuration
// -----------------------------------------------------------------------------

static constexpr uint32_t SERIAL_BAUD = 115200;

// -----------------------------------------------------------------------------
// Heltec WiFi LoRa 32 V4 pin map
// -----------------------------------------------------------------------------

// SX1262 LoRa radio SPI/control pins.
static constexpr int PIN_LORA_NSS  = 8;
static constexpr int PIN_LORA_SCK  = 9;
static constexpr int PIN_LORA_MOSI = 10;
static constexpr int PIN_LORA_MISO = 11;
static constexpr int PIN_LORA_RST  = 12;
static constexpr int PIN_LORA_BUSY = 13;
static constexpr int PIN_LORA_DIO1 = 14;

// Onboard OLED display pins.
static constexpr int PIN_OLED_SDA = 17;
static constexpr int PIN_OLED_SCL = 18;
static constexpr int PIN_OLED_RST = 21;

// Heltec external power control.
// On many Heltec ESP32 LoRa boards Vext is active-low.
// This tool turns it on defensively because some display/peripheral examples
// require it. If the onboard OLED is separately powered on your board, this
// will not hurt anything.
static constexpr int PIN_VEXT_CTRL = 36;

// V4 front-end module power/control.
// The V4 high-power board has an external RF front end. For receive monitoring,
// keep the front end powered.
static constexpr int PIN_VFEM_CTRL = 7;

// Onboard LED from the V4 pin map.
static constexpr int PIN_LED = 35;

// -----------------------------------------------------------------------------
// OLED configuration
// -----------------------------------------------------------------------------

static constexpr int OLED_WIDTH  = 128;
static constexpr int OLED_HEIGHT = 64;
static constexpr int OLED_ADDR   = 0x3C;

// 128x64 OLED using the ESP32 I2C bus configured below.
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, PIN_OLED_RST);

static bool displayOk = false;

// -----------------------------------------------------------------------------
// LoRa P2P settings
// -----------------------------------------------------------------------------

/*
  These defaults are intended to match the first FarmWhisper RAK P2P bring-up:

    915 MHz
    BW 125 kHz
    SF7
    CR 4/5
    explicit header
    CRC on
    normal IQ
    private sync word

  If the Heltec shows "listening" but never prints packets, these are the
  first values to compare against the station firmware.
*/

static constexpr float   RF_FREQUENCY_MHZ = 915.0;
static constexpr float   LORA_BW_KHZ      = 125.0;
static constexpr uint8_t LORA_SF          = 7;

// RadioLib uses the denominator here.
// 5 means 4/5, 6 means 4/6, 7 means 4/7, 8 means 4/8.
static constexpr uint8_t LORA_CR          = 5;

// Private LoRa sync word. LoRaWAN/public networks normally use 0x34.
static constexpr uint8_t LORA_SYNC_WORD   = 0x12;

static constexpr int8_t  LORA_POWER_DBM   = 10;   // Not used for RX, but required by begin().
static constexpr uint16_t LORA_PREAMBLE   = 8;

// Heltec V3/V4 SX1262 designs normally use a TCXO. If radio init fails or RX is
// deaf, this is one of the few hardware-specific values worth testing.
static constexpr float LORA_TCXO_VOLTAGE  = 1.8;

// false = use DC-DC regulator where supported.
// true  = use LDO. Leave false unless the board/library combination requires it.
static constexpr bool LORA_USE_LDO        = false;

// Max LoRa payload for SX126x packet buffer.
static constexpr size_t MAX_PACKET_LEN = 255;

// RadioLib SX1262 object.
// Module() pin order is: NSS/CS, DIO1/IRQ, RESET, BUSY.
SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);

// -----------------------------------------------------------------------------
// Receive interrupt state
// -----------------------------------------------------------------------------

volatile bool packetReceived = false;
volatile bool radioInterruptsEnabled = true;

// -----------------------------------------------------------------------------
// Runtime counters
// -----------------------------------------------------------------------------

static uint32_t packetCount = 0;
static uint32_t crcErrorCount = 0;
static uint32_t readErrorCount = 0;
static uint32_t lastStatusDrawMs = 0;

// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------

void setupPowerPins();
void setupSerial();
void setupDisplay();
void setupRadio();

void drawBootScreen(const char *line);
void drawStatusScreen();
void drawPacketScreen(const uint8_t *data, size_t len, float rssi, float snr, long freqErrHz);

void addSerialBanner();
void printRadioConfig();
void printPacketSerial(const uint8_t *data, size_t len, float rssi, float snr, long freqErrHz);
void printHexSerial(const uint8_t *data, size_t len);
void printQuotedAsciiSerial(const uint8_t *data, size_t len);

String printableAscii(const uint8_t *data, size_t len);
String hexPreview(const uint8_t *data, size_t len, size_t maxBytes);
String clipped(String value, size_t maxChars);

void restartReceive();
void handleReceivedPacket();
void logRadioError(const char *where, int16_t state);

// -----------------------------------------------------------------------------
// Radio interrupt callback
// -----------------------------------------------------------------------------

void IRAM_ATTR onPacketReceived()
{
  if (radioInterruptsEnabled) {
    packetReceived = true;
  }
}

// -----------------------------------------------------------------------------
// Arduino setup
// -----------------------------------------------------------------------------

void setup()
{
  setupPowerPins();
  setupSerial();
  setupDisplay();

  addSerialBanner();
  drawBootScreen("Booting radio");

  setupRadio();

  drawStatusScreen();
}

// -----------------------------------------------------------------------------
// Arduino loop
// -----------------------------------------------------------------------------

void loop()
{
  if (packetReceived) {
    radioInterruptsEnabled = false;
    packetReceived = false;

    handleReceivedPacket();

    radioInterruptsEnabled = true;
  }

  // Keep the screen alive with a quiet status refresh.
  // Serial stays clean unless packets/errors happen.
  if (millis() - lastStatusDrawMs > 5000) {
    lastStatusDrawMs = millis();

    if (packetCount == 0) {
      drawStatusScreen();
    }
  }

  delay(2);
}

// -----------------------------------------------------------------------------
// Setup helpers
// -----------------------------------------------------------------------------

void setupPowerPins()
{
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  pinMode(PIN_VEXT_CTRL, OUTPUT);
  digitalWrite(PIN_VEXT_CTRL, LOW);

  pinMode(PIN_VFEM_CTRL, OUTPUT);
  digitalWrite(PIN_VFEM_CTRL, HIGH);
}

void setupSerial()
{
  Serial.begin(SERIAL_BAUD);

  // Do not wait forever for USB serial.
  // This tool should also work from battery with no computer connected.
  delay(500);
}

void setupDisplay()
{
  // Reset OLED explicitly. This makes startup more reliable across examples,
  // board packages, and warm resets.
  pinMode(PIN_OLED_RST, OUTPUT);
  digitalWrite(PIN_OLED_RST, LOW);
  delay(20);
  digitalWrite(PIN_OLED_RST, HIGH);
  delay(20);

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);

  displayOk = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);

  if (!displayOk) {
    Serial.println("[OLED] init failed");
    return;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.cp437(true);
  display.display();
}

void setupRadio()
{
  Serial.println("[RADIO] SPI begin");
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);

  Serial.println("[RADIO] SX1262 begin");

  int16_t state = radio.begin(
    RF_FREQUENCY_MHZ,
    LORA_BW_KHZ,
    LORA_SF,
    LORA_CR,
    LORA_SYNC_WORD,
    LORA_POWER_DBM,
    LORA_PREAMBLE,
    LORA_TCXO_VOLTAGE,
    LORA_USE_LDO
  );

  if (state != RADIOLIB_ERR_NONE) {
    logRadioError("radio.begin", state);
    drawBootScreen("RADIO FAIL");
    while (true) {
      digitalWrite(PIN_LED, !digitalRead(PIN_LED));
      delay(250);
    }
  }

  // Let SX1262 DIO2 control the RF switch path.
  state = radio.setDio2AsRfSwitch(true);
  if (state != RADIOLIB_ERR_NONE) {
    logRadioError("setDio2AsRfSwitch", state);
  }

  // Enable 2-byte LoRa CRC. This matches the RAK/Semtech P2P style config.
  state = radio.setCRC(2);
  if (state != RADIOLIB_ERR_NONE) {
    logRadioError("setCRC", state);
  }

  // Normal IQ. LoRaWAN downlinks and some special links may invert IQ;
  // FarmWhisper P2P should not.
  state = radio.invertIQ(false);
  if (state != RADIOLIB_ERR_NONE) {
    logRadioError("invertIQ", state);
  }

  radio.setPacketReceivedAction(onPacketReceived);

  printRadioConfig();
  restartReceive();
}

// -----------------------------------------------------------------------------
// Receive path
// -----------------------------------------------------------------------------

void handleReceivedPacket()
{
  uint8_t data[MAX_PACKET_LEN + 1];
  memset(data, 0, sizeof(data));

  size_t len = radio.getPacketLength();

  if (len > MAX_PACKET_LEN) {
    len = MAX_PACKET_LEN;
  }

  int16_t state = radio.readData(data, len);

  if (state == RADIOLIB_ERR_NONE) {
    packetCount++;

    float rssi = radio.getRSSI();
    float snr = radio.getSNR();

    // Useful during antenna/radio bring-up. It is not required for FW logic.
    long freqErrHz = (long)radio.getFrequencyError();

    digitalWrite(PIN_LED, HIGH);

    printPacketSerial(data, len, rssi, snr, freqErrHz);
    drawPacketScreen(data, len, rssi, snr, freqErrHz);

    delay(25);
    digitalWrite(PIN_LED, LOW);
  } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
    crcErrorCount++;
    logRadioError("readData CRC", state);
  } else {
    readErrorCount++;
    logRadioError("readData", state);
  }

  restartReceive();
}

void restartReceive()
{
  int16_t state = radio.startReceive();

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("[RADIO] RX listening");
  } else {
    logRadioError("startReceive", state);
  }
}

// -----------------------------------------------------------------------------
// Serial output
// -----------------------------------------------------------------------------

void addSerialBanner()
{
  Serial.println();
  Serial.println("========================================");
  Serial.println(" FarmWhisper");
  Serial.println(" heltec-v4-packet-tool");
  Serial.print(" Version ");
  Serial.println(TOOL_VERSION);
  Serial.println();
  Serial.println(" Board : Heltec WiFi LoRa 32 V4");
  Serial.println(" Role  : Packet monitor");
  Serial.println(" TX    : disabled");
  Serial.println("========================================");
  Serial.println();
}

void printRadioConfig()
{
  Serial.println("[RADIO] config");
  Serial.print("  freq MHz : ");
  Serial.println(RF_FREQUENCY_MHZ, 3);
  Serial.print("  bw kHz   : ");
  Serial.println(LORA_BW_KHZ, 1);
  Serial.print("  sf       : ");
  Serial.println(LORA_SF);
  Serial.print("  cr       : 4/");
  Serial.println(LORA_CR);
  Serial.print("  sync     : 0x");
  if (LORA_SYNC_WORD < 16) Serial.print('0');
  Serial.println(LORA_SYNC_WORD, HEX);
  Serial.print("  preamble : ");
  Serial.println(LORA_PREAMBLE);
  Serial.print("  tcxo V   : ");
  Serial.println(LORA_TCXO_VOLTAGE, 1);
  Serial.println();
}

void printPacketSerial(const uint8_t *data, size_t len, float rssi, float snr, long freqErrHz)
{
  Serial.print("[RX] #");
  Serial.print(packetCount);
  Serial.print(" ms=");
  Serial.print(millis());
  Serial.print(" len=");
  Serial.print(len);
  Serial.print(" rssi=");
  Serial.print(rssi, 1);
  Serial.print("dBm snr=");
  Serial.print(snr, 1);
  Serial.print("dB ferr=");
  Serial.print(freqErrHz);
  Serial.println("Hz");

  Serial.print("[RX] ascii=\"");
  printQuotedAsciiSerial(data, len);
  Serial.println("\"");

  Serial.print("[RX] hex=");
  printHexSerial(data, len);
  Serial.println();

  Serial.println();
}

void printHexSerial(const uint8_t *data, size_t len)
{
  for (size_t i = 0; i < len; i++) {
    if (i > 0) {
      Serial.print(' ');
    }

    if (data[i] < 0x10) {
      Serial.print('0');
    }

    Serial.print(data[i], HEX);
  }
}

void printQuotedAsciiSerial(const uint8_t *data, size_t len)
{
  for (size_t i = 0; i < len; i++) {
    uint8_t b = data[i];

    if (b == '\\') {
      Serial.print("\\\\");
    } else if (b == '"') {
      Serial.print("\\\"");
    } else if (b == '\r') {
      Serial.print("\\r");
    } else if (b == '\n') {
      Serial.print("\\n");
    } else if (b == '\t') {
      Serial.print("\\t");
    } else if (b >= 32 && b <= 126) {
      Serial.write(b);
    } else {
      Serial.print("\\x");
      if (b < 0x10) {
        Serial.print('0');
      }
      Serial.print(b, HEX);
    }
  }
}

void logRadioError(const char *where, int16_t state)
{
  Serial.print("[RADIO][ERR] ");
  Serial.print(where);
  Serial.print(" state=");
  Serial.println(state);

  if (displayOk) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Radio error");
    display.setCursor(0, 12);
    display.print(where);
    display.setCursor(0, 24);
    display.print("state=");
    display.print(state);
    display.display();
  }
}

// -----------------------------------------------------------------------------
// Display output
// -----------------------------------------------------------------------------

void drawBootScreen(const char *line)
{
  if (!displayOk) {
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("FarmWhisper");

  display.setCursor(0, 12);
  display.print("Heltec V4 Tool");

  display.setCursor(0, 24);
  display.print("v");
  display.print(TOOL_VERSION);

  display.setCursor(0, 42);
  display.print(line);

  display.display();
}

void drawStatusScreen()
{
  if (!displayOk) {
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("FW Heltec V4 Tool");

  display.setCursor(0, 12);
  display.print("Listening...");

  display.setCursor(0, 24);
  display.print(RF_FREQUENCY_MHZ, 1);
  display.print("MHz SF");
  display.print(LORA_SF);
  display.print(" BW");
  display.print((int)LORA_BW_KHZ);

  display.setCursor(0, 36);
  display.print("RX:");
  display.print(packetCount);
  display.print(" CRC:");
  display.print(crcErrorCount);

  display.setCursor(0, 48);
  display.print("USB serial ");
  display.print(SERIAL_BAUD);

  display.display();
}

void drawPacketScreen(const uint8_t *data, size_t len, float rssi, float snr, long freqErrHz)
{
  if (!displayOk) {
    return;
  }

  String text = printableAscii(data, len);
  String hex  = hexPreview(data, len, 8);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("RX #");
  display.print(packetCount);
  display.print(" len=");
  display.print(len);

  display.setCursor(0, 12);
  display.print("R:");
  display.print(rssi, 0);
  display.print(" S:");
  display.print(snr, 1);
  display.print(" F:");
  display.print(freqErrHz);

  display.setCursor(0, 24);
  display.print(clipped(text, 21));

  display.setCursor(0, 36);
  if (text.length() > 21) {
    display.print(clipped(text.substring(21), 21));
  } else {
    display.print(clipped(hex, 21));
  }

  display.setCursor(0, 48);
  display.print(clipped(hex, 21));

  display.display();
}

// -----------------------------------------------------------------------------
// Formatting helpers
// -----------------------------------------------------------------------------

String printableAscii(const uint8_t *data, size_t len)
{
  String out;
  out.reserve(len);

  for (size_t i = 0; i < len; i++) {
    uint8_t b = data[i];

    if (b >= 32 && b <= 126) {
      out += (char)b;
    } else if (b == '\r' || b == '\n' || b == '\t') {
      out += ' ';
    } else {
      out += '.';
    }
  }

  return out;
}

String hexPreview(const uint8_t *data, size_t len, size_t maxBytes)
{
  String out;

  size_t count = len;
  if (count > maxBytes) {
    count = maxBytes;
  }

  for (size_t i = 0; i < count; i++) {
    if (i > 0) {
      out += ' ';
    }

    if (data[i] < 0x10) {
      out += '0';
    }

    out += String(data[i], HEX);
  }

  out.toUpperCase();

  if (len > maxBytes) {
    out += " ...";
  }

  return out;
}

String clipped(String value, size_t maxChars)
{
  if (value.length() <= maxChars) {
    return value;
  }

  return value.substring(0, maxChars);
}
