// Visors — M1: тест дисплея 1.44" 128x128 (ST7735S / ST7789)
//
// Arduino IDE. Плата: ESP32 Dev Module, ядро ESP32 2.0.17.
// Драйвер и смещение экрана выбираются в tft_setup.h (DISPLAY_VARIANT).
//
// Экран 8-пиновый, MISO не выведен -> прочитать ID контроллера нельзя.
// Поэтому прошивка показывает набор диагностических экранов, по которым
// видно, угадали ли драйвер и смещение памяти.
//
// Что смотреть:
//   1. Красный/зелёный/синий/белый — если красный стал синим, включи
//      TFT_RGB_ORDER 1 в tft_setup.h.
//   2. Рамка 128x128 — все 4 края должны быть видны. Если верхняя или
//      левая линия обрезана/сдвинута — неверный DISPLAY_VARIANT.
//   3. Шахматка — не должно быть «плывущих» полос и разрывов.
//   4. Градиент — 16 ровных ступеней серого (важно для ночного режима).

#include "tft_setup.h"      // должен идти ПЕРЕД TFT_eSPI.h
#include <TFT_eSPI.h>

TFT_eSPI tft;

#if defined(ST7789_DRIVER)
static const char *DRIVER_NAME = "ST7789";
#elif defined(ST7735_DRIVER)
static const char *DRIVER_NAME = "ST7735";
#else
static const char *DRIVER_NAME = "UNKNOWN";
#endif

#if DISPLAY_VARIANT == 1
static const char *TAB_NAME = "variant 1: 0,32";
#elif DISPLAY_VARIANT == 2
static const char *TAB_NAME = "variant 2: 2,3";
#elif DISPLAY_VARIANT == 3
static const char *TAB_NAME = "variant 3: 0,0";
#else
static const char *TAB_NAME = "variant 4: ST7789";
#endif

static void backlight(bool on) {
#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, on ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
#else
  (void)on;
#endif
}

// 1. Чистые цвета: проверка драйвера и порядка RGB/BGR
static void screenColors() {
  struct { uint16_t color; const char *name; } steps[] = {
      {TFT_RED, "RED"},     {TFT_GREEN, "GREEN"}, {TFT_BLUE, "BLUE"},
      {TFT_WHITE, "WHITE"}, {TFT_BLACK, "BLACK"},
  };
  for (auto &s : steps) {
    Serial.printf("  color: %s\n", s.name);
    tft.fillScreen(s.color);
    delay(700);
  }
}

// 2. Рамка и оси: проверка смещения и ориентации
static void screenEdges() {
  tft.fillScreen(TFT_BLACK);
  tft.drawRect(0, 0, tft.width(), tft.height(), TFT_WHITE);
  tft.drawRect(1, 1, tft.width() - 2, tft.height() - 2, TFT_DARKGREY);
  tft.drawLine(0, 0, tft.width() - 1, tft.height() - 1, TFT_RED);
  tft.drawLine(tft.width() - 1, 0, 0, tft.height() - 1, TFT_BLUE);
  tft.drawFastHLine(0, tft.height() / 2, tft.width(), TFT_GREEN);
  tft.drawFastVLine(tft.width() / 2, 0, tft.height(), TFT_GREEN);
}

// 3. Шахматка: артефакты, разрывы, частота SPI
static void screenChecker() {
  const int cell = 8;
  for (int y = 0; y < tft.height(); y += cell) {
    for (int x = 0; x < tft.width(); x += cell) {
      bool odd = ((x / cell) + (y / cell)) & 1;
      tft.fillRect(x, y, cell, cell, odd ? TFT_WHITE : TFT_BLACK);
    }
  }
}

// 4. Текст: кто мы и что настроено
static void screenInfo() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Visors M1", 4, 4, 2);

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(DRIVER_NAME, 4, 26, 2);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString(TAB_NAME, 4, 48, 1);

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(String(tft.width()) + "x" + String(tft.height()), 4, 64, 2);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("frame must be visible", 4, 92, 1);
  tft.drawString("on all 4 sides", 4, 104, 1);
}

// 5. Градиент: 16 ступеней серого (база ночного режима)
static void screenGray() {
  tft.fillScreen(TFT_BLACK);
  const int steps = 16;
  const int barW = tft.width() / steps;
  for (int i = 0; i < steps; i++) {
    uint8_t v = (uint8_t)(i * 255 / (steps - 1));
    uint16_t c = tft.color565(v, v, v);
    tft.fillRect(i * barW, 0, barW, tft.height() / 2, c);
  }
  // тот же градиент в «зелёном» ночном варианте
  for (int i = 0; i < steps; i++) {
    uint8_t v = (uint8_t)(i * 255 / (steps - 1));
    tft.fillRect(i * barW, tft.height() / 2, barW, tft.height() / 2,
                 tft.color565(0, v, 0));
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== Visors M1: display test ===");
  Serial.printf("driver=%s tab=%s\n", DRIVER_NAME, TAB_NAME);

  tft.init();
  tft.setRotation(0);
  backlight(true);

  Serial.printf("width=%d height=%d\n", tft.width(), tft.height());
  if (tft.width() != 128 || tft.height() != 128) {
    Serial.println("!! ожидалось 128x128 - проверь TFT_WIDTH/TFT_HEIGHT");
  }
}

void loop() {
  Serial.println("[1/5] colors");
  screenColors();
  Serial.println("[2/5] frame + axes");
  screenEdges();
  delay(1500);
  Serial.println("[3/5] checkerboard");
  screenChecker();
  delay(1500);
  Serial.println("[4/5] info");
  screenInfo();
  delay(2000);
  Serial.println("[5/5] gray ramp");
  screenGray();
  delay(2500);
}
