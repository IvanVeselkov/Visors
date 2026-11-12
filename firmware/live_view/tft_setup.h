#pragma once

// Конфигурация TFT_eSPI для Arduino IDE.
// Подключается первой строкой .ino, библиотеку править не нужно.

#define USER_SETUP_LOADED
#define USER_SETUP_ID 102

// ===========================================================================
//  ВАРИАНТ ДРАЙВЕРА: 1, 2, 3 или 4.  Меняй только эту цифру.
// ===========================================================================
//  1 — ST7735S, смещение 0,32   (самый вероятный для 1.44" 128x128)
//  2 — ST7735S, смещение 2,3
//  3 — ST7735S, без смещения
//  4 — ST7789 128x128
#define DISPLAY_VARIANT 1
// ===========================================================================

#if DISPLAY_VARIANT == 1
  #define ST7735_DRIVER
  #define TFT_WIDTH  128
  #define TFT_HEIGHT 128
  #define ST7735_GREENTAB128
#elif DISPLAY_VARIANT == 2
  #define ST7735_DRIVER
  #define TFT_WIDTH  128
  #define TFT_HEIGHT 128
  #define ST7735_GREENTAB3
#elif DISPLAY_VARIANT == 3
  #define ST7735_DRIVER
  #define TFT_WIDTH  128
  #define TFT_HEIGHT 128
  #define ST7735_BLACKTAB
#elif DISPLAY_VARIANT == 4
  #define ST7789_DRIVER
  #define TFT_WIDTH  128
  #define TFT_HEIGHT 128
  #define CGRAM_OFFSET
#else
  #error "DISPLAY_VARIANT должен быть 1..4"
#endif

// ---------------------------------------------------------------------------
// Пины (см. docs/06-wiring.md)
// ---------------------------------------------------------------------------
#define TFT_MISO -1
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4
#define TFT_BL   32
#define TFT_BACKLIGHT_ON HIGH

// Красный и синий поменяны местами? Раскомментируй (1 = RGB, 0 = BGR):
// #define TFT_RGB_ORDER 1

// ---------------------------------------------------------------------------
// Шрифты и скорость
// ---------------------------------------------------------------------------
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_GFXFF

#define SPI_FREQUENCY 27000000
