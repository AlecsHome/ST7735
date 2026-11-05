/* =====================================================================
 *  plotter.ino — пример ST7735Plotter: осциллограф на ST7735
 *
 *  Пины: CS=10, DC=9, RST=8 (Arduino Uno + ST7735 1.77"/1.8")
 *  Таб: ST7735R_BLACK (1.77" с RGB). Если цвета инвертированы — ST7735R_RED.
 *
 *  Оверлей (пики, AUTO-диапазон) ВЫНЕСЕН из Plotter — рисуйте его
 *  отдельными ST7735Value над/под окном графика через геттеры:
 *    plot.peakMin(), plot.peakMax(), plot.peakToPeak()
 *    plot.effMinVal(), plot.effMaxVal()
 * ===================================================================== */

#include <math.h>                      /* sin() */
#include "st7735.h"
#define ST7735_ENABLE_VALUE    1       /* для оверлея пиков */
#define ST7735_ENABLE_PLOTTER  1
#include "st7735ui.h"

#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   8

ST7735 tft(TFT_CS, TFT_DC, TFT_RST, ST7735R_BLACK);

/* График 118×60px в центре экрана.
 * Фиксированный диапазон 0..30V, сетка тёмно-серая (0x2104). */
ST7735Plotter plot(tft, 5, 50, 118, 60,
                   0.0, 30.0,
                   ST7735_YELLOW, ST7735_BLACK,
                   0x2104, ST7735_WHITE);

/* Внешний оверлей: максимум и минимум пиков над графиком */
ST7735Value peakMax(tft, 5, 30, 58, ST7735_ALIGN_LEFT,
                    ST7735_MAGENTA, ST7735_BLACK, 1, "Mx");
ST7735Value peakMin(tft, 65, 30, 58, ST7735_ALIGN_RIGHT,
                    ST7735_CYAN, ST7735_BLACK, 1, "Mn");

void setup() {
    Serial.begin(9600);
    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(ST7735_BLACK);
    tft.setFont(&font7x11);

    /* Заголовок */
    tft.drawStringAligned("OSCILLOSCOPE", 5, 5, 118,
                          ST7735_ALIGN_CENTER, ST7735_WHITE, ST7735_BLACK, 1);

    plot.draw();
    /* Инициализируем оверлейные Value через set() — это сразу рисует значение.
     * draw() тоже работает (теперь публичный, как у других виджетов), но он
     * рисует пустую ячейку (т.к. _cur ещё пустой). set() рисует сразу. */
    peakMax.set(0.0, 2);
    peakMin.set(0.0, 2);
}

void loop() {
    /* Синусоида 0.5 Гц + медленный дрейф */
    static uint32_t t = 0;
    double v = 15.0 + 10.0 * sin(t * 0.05) + 3.0 * sin(t * 0.013);

    plot.addValue(v);

    /* Обновляем внешний оверлей пиков */
    peakMax.set(plot.peakMax(), 2);
    peakMin.set(plot.peakMin(), 2);

    /* В Serial — для отладки */
    if ((t & 0x3F) == 0) {
        Serial.print("v="); Serial.print(v);
        Serial.print("  P-P="); Serial.print(plot.peakToPeak());
        Serial.print("  eff="); Serial.print(plot.effMinVal());
        Serial.print(".."); Serial.println(plot.effMaxVal());
    }

    t++;
    delay(50);
}
