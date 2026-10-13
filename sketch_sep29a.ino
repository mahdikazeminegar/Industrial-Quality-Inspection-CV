/*
  Industrial Vision QC - Arduino UNO + Waveshare 0.96" 160x80 ST7735S
  Lightweight HMI companion for Python V15.

  LCD wiring:
    VCC  -> 5V
    GND  -> GND
    DIN  -> D11 (MOSI)
    CLK  -> D13 (SCK)
    CS   -> D10
    DC   -> D7
    RST  -> D8
    BL   -> D9

  Optional outputs:
    RED LED   -> D5 through 220R
    GREEN LED -> D6 through 220R
    BUZZER    -> D4 (active buzzer; use a transistor/driver for larger loads)

  USB serial:
    115200 baud

  Python V15 protocol (ASCII lines):
    HELLO
    RESULT|PASS|PRODUCT|GOOD|1234
    RESULT|FAIL|PRODUCT|NO_CAP|1450
    STATS|128|121|7|1420
    RESET
    HB

  Design goal:
    - No image transfer.
    - No CRC.
    - No dynamic String objects.
    - No EEPROM dependency.
    - Tiny fixed-size buffers.
    - The PC remains the source of truth for persistent statistics in SQLite.
*/

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <string.h>
#include <stdlib.h>

// --------------------------- Pins -------------------------------------------
const uint8_t TFT_CS = 10;
const uint8_t TFT_DC = 7;
const uint8_t TFT_RST = 8;
const uint8_t TFT_BL = 9;
const uint8_t RED_LED = 5;
const uint8_t GREEN_LED = 6;
const uint8_t BUZZER = 4;

// --------------------------- LCD --------------------------------------------
const uint16_t LCD_W = 160;
const uint16_t LCD_H = 80;
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

// --------------------------- Parser -----------------------------------------
char lineBuf[128];
uint8_t lineLen = 0;

char currentProduct[43] = "NO PRODUCT";
char currentDefect[43] = "GOOD";
char currentStatus[8] = "READY";
uint32_t currentCycleMs = 0;

uint32_t totalCount = 0;
uint32_t passCount = 0;
uint32_t failCount = 0;
uint32_t avgCycleMs = 0;

unsigned long resultScreenUntil = 0;
unsigned long buzzerUntil = 0;
unsigned long lastActivityMs = 0;

const unsigned long RESULT_SCREEN_MS = 4500UL;
const unsigned long BUZZER_MS = 300UL;

// --------------------------- Helpers ----------------------------------------
void copyToken(char* dst, size_t dstSize, const char* src) {
  if (!dst || dstSize == 0) return;
  if (!src) src = "";
  size_t i = 0;
  while (src[i] != '\0' && i + 1 < dstSize) {
    char c = src[i];
    dst[i] = (c >= 32 && c <= 126) ? c : '_';
    ++i;
  }
  dst[i] = '\0';
}

void trimTail(char* s) {
  if (!s) return;
  int n = (int)strlen(s);
  while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
    s[--n] = '\0';
  }
}

void drawHeader(const char* title, uint16_t color) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setTextColor(color);
  tft.setCursor(4, 3);
  tft.print(title);
  tft.drawFastHLine(4, 15, LCD_W - 8, color);
}

void setReadyOutputs() {
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);
  noTone(BUZZER);
  buzzerUntil = 0;
}

void setPassOutputs() {
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);
  noTone(BUZZER);
  buzzerUntil = 0;
}

void setFailOutputs() {
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);
  tone(BUZZER, 2200);
  buzzerUntil = millis() + BUZZER_MS;
}

void serviceOutputs() {
  if (buzzerUntil != 0 && (long)(millis() - buzzerUntil) >= 0) {
    buzzerUntil = 0;
    noTone(BUZZER);
  }
}

void showIdleScreen() {
  drawHeader("VISION QC", ST77XX_CYAN);

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, 22);
  tft.print("SYSTEM: READY");

  tft.setCursor(4, 36);
  tft.print("TOTAL: ");
  tft.print(totalCount);

  tft.setCursor(84, 36);
  tft.print("PASS: ");
  tft.print(passCount);

  tft.setCursor(4, 50);
  tft.print("FAIL: ");
  tft.print(failCount);

  tft.setCursor(84, 50);
  tft.print("RATE: ");
  uint16_t rate10 = (totalCount > 0) ? (uint16_t)((passCount * 1000UL) / totalCount) : 0;
  tft.print(rate10 / 10);
  tft.print('.');
  tft.print(rate10 % 10);
  tft.print('%');

  tft.setCursor(4, 65);
  tft.setTextColor(ST77XX_GREEN);
  tft.print("PC ONLINE");
  tft.setCursor(84, 65);
  tft.setTextColor(ST77XX_WHITE);
  tft.print("AVG ");
  tft.print(avgCycleMs);
  tft.print("ms");
}

void showResultScreen() {
  bool isPass = (strcmp(currentStatus, "PASS") == 0);
  drawHeader(isPass ? "PASS" : "FAIL", isPass ? ST77XX_GREEN : ST77XX_RED);

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, 22);
  tft.print("PRODUCT:");
  tft.setCursor(4, 33);
  tft.print(currentProduct);

  tft.setCursor(4, 46);
  tft.setTextColor(isPass ? ST77XX_GREEN : ST77XX_RED);
  tft.print("DEFECT:");
  tft.setCursor(4, 57);
  tft.print(currentDefect);

  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(4, 70);
  tft.print("CYCLE ");
  tft.print(currentCycleMs);
  tft.print(" ms");
}

void showMessage(const char* line1, const char* line2, uint16_t color) {
  drawHeader("VISION QC", color);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, 28);
  tft.print(line1);
  tft.setCursor(4, 44);
  tft.print(line2);
}

void handleResult(char* line) {
  // RESULT|STATUS|PRODUCT|DEFECT|CYCLE_MS
  char* save = nullptr;
  char* token = strtok_r(line, "|", &save); // RESULT
  if (!token || strcmp(token, "RESULT") != 0) return;

  char* status = strtok_r(nullptr, "|", &save);
  char* product = strtok_r(nullptr, "|", &save);
  char* defect = strtok_r(nullptr, "|", &save);
  char* cycle = strtok_r(nullptr, "|", &save);
  if (!status || !product || !defect || !cycle) return;

  copyToken(currentStatus, sizeof(currentStatus), status);
  copyToken(currentProduct, sizeof(currentProduct), product);
  copyToken(currentDefect, sizeof(currentDefect), defect);
  currentCycleMs = (uint32_t)strtoul(cycle, nullptr, 10);

  totalCount++;
  if (strcmp(currentStatus, "PASS") == 0) {
    passCount++;
    setPassOutputs();
  } else {
    failCount++;
    setFailOutputs();
  }

  // Running average using integer arithmetic only.
  if (totalCount == 1) {
    avgCycleMs = currentCycleMs;
  } else {
    avgCycleMs = (uint32_t)(((uint64_t)avgCycleMs * (totalCount - 1UL) + currentCycleMs) / totalCount);
  }

  showResultScreen();
  resultScreenUntil = millis() + RESULT_SCREEN_MS;
  lastActivityMs = millis();
  Serial.println(F("RESULT_OK"));
}

void handleStats(char* line) {
  // STATS|TOTAL|PASS|FAIL|AVG_CYCLE_MS
  char* save = nullptr;
  char* token = strtok_r(line, "|", &save); // STATS
  if (!token || strcmp(token, "STATS") != 0) return;

  char* total = strtok_r(nullptr, "|", &save);
  char* pass = strtok_r(nullptr, "|", &save);
  char* fail = strtok_r(nullptr, "|", &save);
  char* avg = strtok_r(nullptr, "|", &save);
  if (!total || !pass || !fail || !avg) return;

  totalCount = strtoul(total, nullptr, 10);
  passCount = strtoul(pass, nullptr, 10);
  failCount = strtoul(fail, nullptr, 10);
  avgCycleMs = strtoul(avg, nullptr, 10);
  showIdleScreen();
  lastActivityMs = millis();
  Serial.println(F("STATS_OK"));
}

void resetStats() {
  totalCount = 0;
  passCount = 0;
  failCount = 0;
  avgCycleMs = 0;
  strncpy(currentStatus, "READY", sizeof(currentStatus));
  strncpy(currentDefect, "GOOD", sizeof(currentDefect));
  showIdleScreen();
  Serial.println(F("RESET_OK"));
}

void handleCommand(char* line) {
  trimTail(line);
  if (line[0] == '\0') return;
  lastActivityMs = millis();

  if (strcmp(line, "HELLO") == 0) {
    showMessage("SYSTEM READY", "PC CONNECTED", ST77XX_GREEN);
    setReadyOutputs();
    Serial.println(F("ARDUINO_READY"));
    return;
  }

  if (strcmp(line, "HB") == 0) {
    Serial.println(F("HB_OK"));
    return;
  }

  if (strcmp(line, "RESET") == 0) {
    resetStats();
    return;
  }

  if (strncmp(line, "RESULT|", 7) == 0) {
    handleResult(line);
    return;
  }

  if (strncmp(line, "STATS|", 6) == 0) {
    handleStats(line);
    return;
  }

  Serial.println(F("UNKNOWN_CMD"));
}

void readSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      lineBuf[lineLen] = '\0';
      handleCommand(lineBuf);
      lineLen = 0;
    } else if (c != '\r') {
      if (lineLen < sizeof(lineBuf) - 1) {
        lineBuf[lineLen++] = c;
      } else {
        lineLen = 0;
        Serial.println(F("LINE_TOO_LONG"));
      }
    }
  }
}

// --------------------------- Arduino lifecycle -------------------------------
void setup() {
  pinMode(TFT_BL, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(TFT_BL, HIGH);
  Serial.begin(115200);

  tft.initR(INITR_MINI160x80);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);

  setReadyOutputs();
  showIdleScreen();
  lastActivityMs = millis();
}

void loop() {
  readSerial();
  serviceOutputs();

  if (resultScreenUntil != 0 && (long)(millis() - resultScreenUntil) >= 0) {
    resultScreenUntil = 0;
    showIdleScreen();
    if (strcmp(currentStatus, "FAIL") == 0) {
      setReadyOutputs();
    }
  }
}
