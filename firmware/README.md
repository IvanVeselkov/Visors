# Прошивка (Arduino IDE)

Оба скетча — обычные Arduino-проекты: папка + `.ino` + `tft_setup.h`.
Править файлы библиотеки TFT_eSPI **не нужно**, вся конфигурация экрана
лежит рядом со скетчем.

```
firmware/
  display_test/   M1: тест и идентификация экрана
    display_test.ino
    tft_setup.h
  live_view/      M2/M3: живое видео OV7670 -> TFT
    live_view.ino
    tft_setup.h
```

## 1. Что установить

| Что | Версия | Зачем |
|---|---|---|
| Arduino IDE | 2.x | среда |
| Ядро **esp32** (Espressif) | **2.0.17** | плата + встроенная esp32-camera |
| Библиотека **TFT_eSPI** (Bodmer) | 2.5.44 | экран |
| esp32-camera | **не ставить** | уже внутри ядра 2.0.17 |

### Ядро ESP32

`Tools → Board → Boards Manager` → найди **esp32 by Espressif Systems** →
в выпадающем списке выбери **2.0.17** → Install.

> **Версия важна.** В ядре 3.x переделали API LEDC (`ledcSetup`/`ledcAttachPin`
> убраны), и скетч не соберётся. Плюс TFT_eSPI 2.5.44 проверялся именно
> с 2.0.x.

Если список плат пустой, добавь URL в `File → Preferences → Additional
boards manager URLs`:

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

### TFT_eSPI

`Sketch → Include Library → Manage Libraries` → **TFT_eSPI** → версия 2.5.44 →
Install. Если раньше ставил вручную ZIP-ом — удали старую, чтобы не было
двух копий.

### Драйвер USB

Если порт не появляется: поставь драйвер **CP210x** (Silicon Labs) или
**CH340** (WCH) — зависит от платы. В диспетчере устройств будет
«неизвестное устройство» или COM-порт с восклицательным знаком.

## 2. Настройки платы

`Tools`:

| Параметр | Значение |
|---|---|
| Board | **ESP32 Dev Module** |
| Upload Speed | 921600 |
| CPU Frequency | 240 MHz |
| Flash Frequency | 80 MHz |
| Flash Mode | QIO |
| Flash Size | 4 MB (32 Mb) |
| **Partition Scheme** | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| PSRAM | Disabled |
| Core Debug Level | None |
| Port | COM-порт платы |

`Huge APP` нужен, потому что камера + графика занимают больше 1.2 МБ.
Если выбрать дефолтную схему — будет «Sketch too big».

## 3. `display_test` — сначала экран

1. `File → Open…` → `firmware/display_test/display_test.ino`.
2. Открой `tft_setup.h`, найди строку `#define DISPLAY_VARIANT 1`.
3. Загрузи (стрелка Upload), открой `Serial Monitor` на **115200**.

Варианты:

| DISPLAY_VARIANT | Что это |
|---|---|
| **1** | ST7735S, смещение 0,32 — начинать с него |
| 2 | ST7735S, смещение 2,3 |
| 3 | ST7735S, без смещения |
| 4 | ST7789 128×128 |

Подбирай по картинке:

| Что видно | Вывод |
|---|---|
| Картинка есть, цвета правильные, рамка по всем 4 краям | **это твой вариант** |
| Экран чёрный, подсветка горит | другой драйвер → 4 |
| Картинка сдвинута / обрезана / «заворачивается» | другое смещение → 2, затем 3 |
| Красный и синий поменяны местами | раскомментируй `#define TFT_RGB_ORDER 1` |
| «Снег», полосы, дёрганье | снизь `SPI_FREQUENCY` до 15000000, проверь пайку |
| Совсем ничего, подсветки нет | проверь `BLK` и питание 3.3 В |

Порядок перебора: **1 → 2 → 3 → 4 → TFT_RGB_ORDER**.

### Подключение экрана (напоминание)

| Экран | ESP32 |
|---|---|
| GND | GND |
| VCC | 3V3 |
| SCL | GPIO14 |
| SDA | GPIO13 |
| RES | GPIO4 |
| DC | GPIO2 |
| CS | GPIO15 |
| BLK | GPIO32 |

Полная схема — [../docs/06-wiring.md](../docs/06-wiring.md).

## 4. `live_view` — живое видео

1. `File → Open…` → `firmware/live_view/live_view.ino`.
2. В `tft_setup.h` поставь **тот же** `DISPLAY_VARIANT`, который подошёл.
3. Upload, Serial Monitor 115200.

В логе должно быть:

```
=== Visors live view ===
sensor PID=0x76 VER=0x73
frame: 160x120, 38400 bytes, format=3
profile: night (gamma=0.60 gain=1.35)
FPS: 23
```

`PID=0x76` — это OV7670. `format=3` — YUV422. Если `esp_camera_init failed`
или `PID=0x00` — нет связи по SCCB: проверь SDA/SCL, подтяжки 4.7 кОм,
питание модуля и что XCLK реально генерируется (на GPIO0 ~1.6 В среднего).

### Кнопки

| Кнопка | GPIO | Действие |
|---|---|---|
| MODE | 16 | профиль яркости: day → night → night+ |
| ZOOM | 17 | зум 1× / 2× |
| IR | 33 | ИК-подсветка (PWM на GPIO12) |

### Если картинка не такая

| Симптом | Причина |
|---|---|
| Полосы, сдвиг строк, рваный кадр | длинные провода или высокая XCLK → 4–6 МГц |
| Всё зелёное/фиолетовое | не YUV422, проверь `format=3` в логе |
| Слишком темно днём | профиль `night`, нажми MODE до `day` |
| FPS < 5 | мало света, длинная выдержка; включи ИК |
| ESP32 перезагружается при включении ИК | boost не тянет ток, добавь 470 мкФ |

### Ограничение драйвера

В esp32-camera для OV7670 `set_brightness`, `set_contrast`, `set_agc_gain`,
`set_aec_value` — **заглушки**. Автоэкспозиция работает, ручной контроль нет.
Для M3 это значит прямые записи в регистры SCCB (AEC/AGC/COM9).

## 5. Типовые проблемы Arduino IDE

| Ошибка / симптом | Что делать |
|---|---|
| `Connecting........_____` и не шьётся | зажми **BOOT**, нажми **EN**, отпусти BOOT, повтори Upload |
| Порт не виден | драйвер CP210x/CH340, другой кабель (не «только зарядка») |
| `TFT_eSPI has not been configured` | `tft_setup.h` должен лежать рядом с `.ino`, а в `.ino` первой строкой `#include "tft_setup.h"` |
| `'ledcSetup' was not declared` | стоит ядро 3.x → поставь 2.0.17 |
| `esp_camera.h: No such file` | ядро не 2.0.17, камера не встроена |
| `Sketch too big` | Partition Scheme → Huge APP |
| `Multiple libraries were found for TFT_eSPI` | удали лишнюю копию библиотеки |
| Изменения не применяются | проверь, что открыт нужный `.ino` из нужной папки; `Sketch → Verify` |
| `Brownout detector was triggered` | просадка питания, добавь конденсатор, питай от 5 В, не от слабого USB |

## 6. Если плата ESP32-S3

`Board → ESP32S3 Dev Module`, `PSRAM → Enabled` (если модуль с PSRAM).
Пины камеры у S3 другие — см. [../docs/06-wiring.md](../docs/06-wiring.md),
раздел 8. Скетч придётся поправить в блоке `CAM_PIN_*`.

## 7. Чего в этой среде не проверено

Скетчи написаны под ядро 2.0.17 и TFT_eSPI 2.5.44, но собрать их здесь
не на чем (Arduino IDE в этой среде нет). Если появится ошибка компиляции —
пришли текст, поправлю.
