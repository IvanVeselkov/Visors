// Visors — M2/M3: живой поток OV7670 -> TFT 128x128 в реальном времени
//
// Arduino IDE. Плата: ESP32 Dev Module, ядро ESP32 2.0.17
// (в нём esp32-camera уже встроена, отдельно ставить не нужно).
// Драйвер экрана выбирается в tft_setup.h (DISPLAY_VARIANT).
//
// Тракт:
//   OV7670 YUV422 QQVGA (160x120)
//     -> Y-плоскость (яркость), без конвертации RGB->Y
//     -> nearest-neighbour в 128x128
//     -> LUT: гамма + подъём теней (профили day / night / night+)
//     -> RGB565 (r=g=b)
//     -> SPI на ST7735/ST7789
//
// Камера отдаёт кадры в фоне (DMA + 2 буфера), CPU занят только
// преобразованием и выводом. Захват и вывод идут параллельно.
//
// Кнопки:
//   MODE  (GPIO16) — профиль яркости: день / ночь / ночь+
//   ZOOM  (GPIO17) — цифровой зум 1x / 2x
//   IR    (GPIO33) — ИК-подсветка вкл/выкл (PWM на GPIO12)
//
// Serial 115200: PID/VER камеры, FPS, состояние.

#include "tft_setup.h"      // должен идти ПЕРЕД TFT_eSPI.h
#include <TFT_eSPI.h>
#include "esp_camera.h"

// ---------------------------------------------------------------------------
// Пины камеры. На классическом ESP32 фиксированы (I2S0), менять нельзя.
// ---------------------------------------------------------------------------
#define CAM_PIN_PWDN  -1   // физически на GND
#define CAM_PIN_RESET -1   // физически на 3V3
#define CAM_PIN_XCLK   0
#define CAM_PIN_SIOD  26
#define CAM_PIN_SIOC  27
#define CAM_PIN_D7    35
#define CAM_PIN_D6    34
#define CAM_PIN_D5    39
#define CAM_PIN_D4    36
#define CAM_PIN_D3    21
#define CAM_PIN_D2    19
#define CAM_PIN_D1    18
#define CAM_PIN_D0     5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF  23
#define CAM_PIN_PCLK  22

// Периферия
#define PIN_BTN_MODE 16
#define PIN_BTN_ZOOM 17
#define PIN_BTN_IR   33
#define PIN_IR_PWM   12
#define PIN_TFT_BL   32

// LEDC-каналы: 0 занят камерой (XCLK), поэтому 4 и 5 (таймер 2).
#define LEDC_CH_BL 4
#define LEDC_CH_IR 5

// ---------------------------------------------------------------------------
// Выходной кадр
// ---------------------------------------------------------------------------
static const int OUT_W = 128;
static const int OUT_H = 128;

static uint16_t outBuf[OUT_W * OUT_H];      // 32 КБ
static uint16_t srcX[OUT_W];                // смещение внутри строки, в байтах
static uint32_t srcRow[OUT_H];              // смещение строки, в байтах
static uint8_t  lut[256];

static uint16_t srcW = 160;
static uint16_t srcH = 120;
static uint8_t  zoom = 1;

TFT_eSPI tft;

// ---------------------------------------------------------------------------
// Кривая яркости
// ---------------------------------------------------------------------------
static void buildLut(float gamma, float gain, float black) {
  for (int i = 0; i < 256; i++) {
    float v = (i / 255.0f - black) / (1.0f - black);
    if (v < 0) v = 0;
    v = powf(v, gamma) * gain * 255.0f;
    lut[i] = (uint8_t)(v > 255.0f ? 255.0f : v);
  }
}

struct Profile {
  const char *name;
  float gamma;
  float gain;
  float black;
};

static const Profile PROFILES[] = {
    {"day", 1.00f, 1.00f, 0.00f},
    {"night", 0.60f, 1.35f, 0.02f},
    {"night+", 0.45f, 1.70f, 0.05f},
};
static const int PROFILE_COUNT = sizeof(PROFILES) / sizeof(PROFILES[0]);
static int profileIdx = 1;
static bool irOn = false;

// ---------------------------------------------------------------------------
// Карта выборки: nearest-neighbour из srcW x srcH в OUT_W x OUT_H
// ---------------------------------------------------------------------------
static void rebuildMap() {
  // YUV422 (YUYV): Y лежит в чётных байтах пары, поэтому *2.
  for (int x = 0; x < OUT_W; x++) {
    int sx;
    if (zoom == 1) {
      sx = x * srcW / OUT_W;
    } else {
      // центр кадра, шаг 2 -> поле зрения вдвое уже
      sx = (int)srcW / 2 + (x - OUT_W / 2) * (int)srcW / (OUT_W * 2);
      if (sx < 0) sx = 0;
      if (sx >= srcW) sx = srcW - 1;
    }
    srcX[x] = (uint16_t)(sx * 2);
  }
  for (int y = 0; y < OUT_H; y++) {
    int sy = y * srcH / OUT_H;
    srcRow[y] = (uint32_t)sy * srcW * 2;
  }
}

// ---------------------------------------------------------------------------
// YUV422/YUYV -> grayscale -> RGB565
// ---------------------------------------------------------------------------
static void renderFrame(const uint8_t *src) {
  for (int y = 0; y < OUT_H; y++) {
    const uint8_t *row = src + srcRow[y];
    uint16_t *dst = outBuf + y * OUT_W;
    for (int x = 0; x < OUT_W; x++) {
      uint8_t g = lut[row[srcX[x]]];
      // r5<<11 | g6<<5 | b5, где r=g=b
      dst[x] = (uint16_t)(((g & 0xF8) << 8) | ((g & 0xFC) << 3) | (g >> 3));
    }
  }
}

static void pushFrame() {
  tft.startWrite();
  tft.setAddrWindow(0, 0, OUT_W, OUT_H);
  tft.pushColors(outBuf, OUT_W * OUT_H, true);  // swap: host -> big-endian
  tft.endWrite();
}

// ---------------------------------------------------------------------------
// Периферия
// ---------------------------------------------------------------------------
static void irSet(bool on) {
  irOn = on;
  // 20 кГц, чтобы не мерцало в кадре
  ledcWrite(LEDC_CH_IR, on ? 255 : 0);
}

static void applyProfile() {
  const Profile &p = PROFILES[profileIdx];
  buildLut(p.gamma, p.gain, p.black);
  Serial.printf("profile: %s (gamma=%.2f gain=%.2f)\n", p.name, p.gamma, p.gain);
}

static bool pressed(int pin) {
  static uint32_t last[3] = {0, 0, 0};
  static bool state[3] = {false, false, false};
  int idx = (pin == PIN_BTN_MODE) ? 0 : (pin == PIN_BTN_ZOOM) ? 1 : 2;

  bool now = digitalRead(pin) == LOW;
  if (now != state[idx]) {
    if (millis() - last[idx] > 40) {
      last[idx] = millis();
      state[idx] = now;
      return now;  // срабатывание на нажатие
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Камера
// ---------------------------------------------------------------------------
static esp_err_t cameraStart() {
  camera_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));

  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer = LEDC_TIMER_0;
  cfg.pin_pwdn = CAM_PIN_PWDN;
  cfg.pin_reset = CAM_PIN_RESET;
  cfg.pin_xclk = CAM_PIN_XCLK;
  cfg.pin_sccb_sda = CAM_PIN_SIOD;
  cfg.pin_sccb_scl = CAM_PIN_SIOC;
  cfg.pin_d0 = CAM_PIN_D0;
  cfg.pin_d1 = CAM_PIN_D1;
  cfg.pin_d2 = CAM_PIN_D2;
  cfg.pin_d3 = CAM_PIN_D3;
  cfg.pin_d4 = CAM_PIN_D4;
  cfg.pin_d5 = CAM_PIN_D5;
  cfg.pin_d6 = CAM_PIN_D6;
  cfg.pin_d7 = CAM_PIN_D7;
  cfg.pin_vsync = CAM_PIN_VSYNC;
  cfg.pin_href = CAM_PIN_HREF;
  cfg.pin_pclk = CAM_PIN_PCLK;

  cfg.xclk_freq_hz = 8000000;  // начни с 8 МГц, потом можно 16-20
  cfg.pixel_format = PIXFORMAT_YUV422;
  cfg.frame_size = FRAMESIZE_QQVGA;  // 160x120
  cfg.jpeg_quality = 12;             // для OV7670 не используется
  cfg.fb_count = 2;
  cfg.fb_location = CAMERA_FB_IN_DRAM;  // PSRAM нет
  cfg.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("esp_camera_init failed: 0x%x\n", err);
    return err;
  }

  sensor_t *s = esp_camera_sensor_get();
  Serial.printf("sensor PID=0x%02X VER=0x%02X\n", s->id.PID, s->id.VER);

  // Реализовано в драйвере OV7670; остальные настройки (brightness,
  // contrast, AGC gain) — заглушки, поэтому проверяем указатели.
  if (s->set_gain_ctrl) s->set_gain_ctrl(s, 1);
  if (s->set_exposure_ctrl) s->set_exposure_ctrl(s, 1);
  if (s->set_whitebal) s->set_whitebal(s, 1);

  return ESP_OK;
}

// ---------------------------------------------------------------------------
// Экран
// ---------------------------------------------------------------------------
static void displayStart() {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  ledcSetup(LEDC_CH_BL, 20000, 8);
  ledcAttachPin(PIN_TFT_BL, LEDC_CH_BL);
  ledcWrite(LEDC_CH_BL, 255);
}

// ---------------------------------------------------------------------------
// Ошибка камеры: чтобы не было «чёрного ящика», показываем причину
// ---------------------------------------------------------------------------
static void showCameraError(esp_err_t err) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString("CAMERA FAIL", 4, 20, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("err 0x" + String((long)err, HEX), 4, 48, 2);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString("check SCCB, XCLK, 3V3", 4, 80, 1);
  tft.drawString("see docs/06-wiring.md", 4, 96, 1);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== Visors live view ===");

  pinMode(PIN_BTN_MODE, INPUT_PULLUP);
  pinMode(PIN_BTN_ZOOM, INPUT_PULLUP);
  pinMode(PIN_BTN_IR, INPUT_PULLUP);

  ledcSetup(LEDC_CH_IR, 20000, 8);
  ledcAttachPin(PIN_IR_PWM, LEDC_CH_IR);
  irSet(false);

  displayStart();
  rebuildMap();
  applyProfile();

  esp_err_t err = cameraStart();
  if (err != ESP_OK) {
    showCameraError(err);
    while (true) {
      delay(500);
      ledcWrite(LEDC_CH_BL, (millis() / 500) % 2 ? 255 : 16);
    }
  }

  // Первый кадр: узнать реальные размеры (должно быть 160x120)
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb) {
    srcW = fb->width;
    srcH = fb->height;
    Serial.printf("frame: %ux%u, %u bytes, format=%d\n", fb->width, fb->height,
                  (unsigned)fb->len, (int)fb->format);
    esp_camera_fb_return(fb);
    rebuildMap();
  }
}

void loop() {
  // --- кнопки -------------------------------------------------------------
  if (pressed(PIN_BTN_MODE)) {
    profileIdx = (profileIdx + 1) % PROFILE_COUNT;
    applyProfile();
  }
  if (pressed(PIN_BTN_ZOOM)) {
    zoom = (zoom == 1) ? 2 : 1;
    rebuildMap();
    Serial.printf("zoom: %dx\n", zoom);
  }
  if (pressed(PIN_BTN_IR)) {
    irSet(!irOn);
    Serial.printf("IR: %s\n", irOn ? "on" : "off");
  }

  // --- кадр ---------------------------------------------------------------
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("fb_get failed");
    delay(10);
    return;
  }

  // Ожидаем YUV422; при другом формате просто не рисуем мусор
  if (fb->format == PIXFORMAT_YUV422 &&
      fb->len >= (size_t)fb->width * fb->height * 2) {
    renderFrame(fb->buf);
    pushFrame();
  }

  esp_camera_fb_return(fb);

  // --- FPS ----------------------------------------------------------------
  static uint32_t lastMs = 0;
  static uint32_t frames = 0;
  frames++;
  uint32_t now = millis();
  if (now - lastMs >= 1000) {
    Serial.printf("FPS: %lu\n", (unsigned long)frames);
    frames = 0;
    lastMs = now;
  }
}
