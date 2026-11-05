/* =====================================================================
 *  bargauge.ino — пример ST7735BarGauge: шкальный индикатор с порогом
 *
 *  Пины: CS=10, DC=9, RST=8 (Arduino Uno + ST7735 1.77"/1.8")
 *  Таб: ST7735R_BLACK (1.77" с RGB). Если цвета инвертированы — ST7735R_RED.
 * ===================================================================== */

#include "st7735.h"
#define ST7735_ENABLE_BARGAUGE 1
#include "st7735ui.h"

#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   8

ST7735 tft(TFT_CS, TFT_DC, TFT_RST, ST7735R_BLACK);

/* Полоса на всю ширину экрана, высота 16px.
 * Диапазон 0..30V, порог предупреждения 28V (выше — красный). */
ST7735BarGauge bar(tft, 5, 70, 118, 16,
                   0.0, 30.0,
                   ST7735_GREEN, ST7735_BLACK, ST7735_WHITE,
                   ST7735_RED, 28.0);

void setup() {
    Serial.begin(9600);
    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(ST7735_BLACK);

    bar.draw();
    bar.setValue(5.0);
    delay(500);
    bar.setValue(15.0);
    delay(500);
    bar.setValue(25.0);
    delay(500);
    bar.setValue(29.5);                /* выше 28 → красный */
    delay(1000);

    /* Смена диапазона на лету */
    bar.setRange(0.0, 100.0, 80.0);    /* warnThreshold=80 */
    bar.setValue(75.0);
    delay(1000);
    bar.setValue(95.0);                /* выше 80 → красный */
}

void loop() {
    /* Плавно растём и падаем */
    static double v = 0.0;
    static int8_t dir = 1;
    v += dir * 0.3;
    if (v > 100.0) { v = 100.0; dir = -1; }
    if (v < 0.0)   { v = 0.0;   dir =  1; }
    bar.setValue(v);
    delay(50);
}
