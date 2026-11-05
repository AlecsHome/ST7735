/* =====================================================================
 *  ui_demo.ino — полный демо всех UI-виджетов на ST7735 1.77" 128×160
 *
 *  Пины: CS=10, DC=9, RST=8 (Arduino Uno + ST7735 1.77"/1.8")
 *  Таб: ST7735R_BLACK (1.77" с RGB). Если цвета инвертированы — ST7735R_RED.
 *
 *  ЗАНЯТЫЕ ПИНЫ Arduino Uno/Nano/Pro Mini (ATmega328):
 *      11  MOSI  ── TFT SDA / DIN
 *      13  SCK   ── TFT SCK / CLK
 *      10  TFT_CS   (также SS AVR: держать OUTPUT!)
 *       9  TFT_DC
 *       8  TFT_RST
 *       0,1 — Serial RX/TX (USB-монитор)
 *
 *  tft.begin() без аргумента = 8 МГц SPI (быстро).
 * ===================================================================== */

#include <math.h>                      /* sin() для Plotter */
#include "st7735.h"

/* === Включаем только нужные виджеты (экономия flash/RAM на AVR) ===
 * В этом скетче используются: Checkbox, Value, BarGauge, Plotter.
 * Button и Toggle выключаем — их код не попадёт в бинарник.
 * На ATmega328 экономия ~0.6 КБ flash + ~20 Б RAM. */
#define ST7735_ENABLE_BUTTON   0
#define ST7735_ENABLE_TOGGLE   0
#define ST7735_ENABLE_CHECKBOX 1
#define ST7735_ENABLE_VALUE    1
#define ST7735_ENABLE_BARGAUGE 1
#define ST7735_ENABLE_PLOTTER  1

#include "st7735ui.h"

#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   8

ST7735 tft(TFT_CS, TFT_DC, TFT_RST, ST7735R_BLACK);

/* === Глобальные виджеты — позиции для ПОРТРЕТА 128×160 === */
ST7735Checkbox cb(tft, 5, 5, 16, ST7735_WHITE, ST7735_GREEN, ST7735_BLACK,
                  "Option", ST7735_YELLOW, 1);

ST7735Value volt(tft, 5, 25, 118, ST7735_ALIGN_RIGHT,
                 ST7735_GREEN, ST7735_BLACK, 2, "V");
ST7735Value temp(tft, 5, 47, 118, ST7735_ALIGN_RIGHT,
                 ST7735_YELLOW, ST7735_BLACK, 2, "\xB0""C");
ST7735Value raw(tft, 5, 69, 118, ST7735_ALIGN_RIGHT,
                ST7735_WHITE, ST7735_BLACK, 2, "");
ST7735Value v(tft, 5, 91, 118, ST7735_ALIGN_RIGHT,
              ST7735_CYAN, ST7735_BLACK, 2, "V");

ST7735BarGauge bar(tft, 5, 115, 118, 12,
                   0.0, 30.0,
                   ST7735_GREEN, ST7735_BLACK, ST7735_WHITE,
                   ST7735_RED, 28.0);

ST7735Plotter plot(tft, 5, 130, 118, 28,
                   0.0, 30.0,
                   ST7735_YELLOW, ST7735_BLACK,
                   0x2104, ST7735_WHITE);

void setup() {
    Serial.begin(9600);
    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(ST7735_BLACK);
    tft.setFont(&font7x11);

    cb.draw();
    cb.setChecked(true);

    volt.set(12.345, 2);
    temp.set(25.5, 1);
    raw.set(123.456, 2);
    v.set(1.234, 3);

    delay(2000);

    volt.set(12.34567, 4);
    delay(1000);

    temp.setUnit("\xB0""F");
    temp.set(77.9, 1);
    delay(1000);

    v.setError("ERR");
    delay(1000);
    v.set(99.9);
    delay(1000);

    v.setRaw("READY");
    delay(1000);
    v.set(1.234);
    delay(1000);

    v.set(INFINITY);
    delay(1000);

    v.set(5.0, 3);
    volt.set(12.5, 2);
    temp.setUnit("\xB0""C");
    temp.set(25.0, 1);

    bar.draw();
    bar.setValue(15.0);
    delay(500);
    bar.setValue(25.0);
    delay(500);
    bar.setValue(29.5);
    delay(1000);

    plot.draw();
    for (uint8_t i = 0; i < 120; i++) {
        double val = 15.0 + 10.0 * sin(i * 0.1);
        plot.addValue(val);
        delay(150);
    }

    /* Геттеры Plotter — вывод пиков в Serial (оверлей вынесен из класса) */
    Serial.print("peakMin="); Serial.print(plot.peakMin());
    Serial.print("  peakMax="); Serial.print(plot.peakMax());
    Serial.print("  P-P="); Serial.print(plot.peakToPeak());
    Serial.print("  effRange="); Serial.print(plot.effMinVal());
    Serial.print(".."); Serial.println(plot.effMaxVal());
}

void loop() {
    /* Статичный тест — loop пустой. RESET для перезапуска. */
}
