/* =====================================================================
 *  volt.ino — пример ST7735Value: вывод напряжения с автопереформатированием
 *
 *  Пины: CS=10, DC=9, RST=8 (Arduino Uno + ST7735 1.77"/1.8")
 *  Таб: ST7735R_BLACK (1.77" с RGB). Если цвета инвертированы — ST7735R_RED.
 *
 *  ВАЖНО: виджеты по умолчанию ВЫКЛЮЧЕНЫ (ST7735_ENABLE_*=0).
 *  Перед #include "st7735ui.h" включайте только нужные — экономия flash/RAM.
 *
 *  tft.begin() без аргумента = 8 МГц SPI (быстро).
 *  tft.begin(9600) = 9.6 КГц SPI (ОЧЕНЬ медленно, экран "висит")!
 *  Не путайте с Serial.begin(9600) — это разные вещи.
 * ===================================================================== */

#include "st7735.h"
#define ST7735_ENABLE_VALUE 1          /* ← включаем только нужный виджет */
#include "st7735ui.h"

#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   8

ST7735 tft(TFT_CS, TFT_DC, TFT_RST, ST7735R_BLACK);

/* Виджет 80px ширины, выравнивание RIGHT — текст прижимается к правому краю.
 * При font7x11 scale=2: pitch=16px, виджет 80px → 5 символов помещается. */
ST7735Value volt(tft, 5, 50, 80, ST7735_ALIGN_RIGHT,
                 ST7735_GREEN, ST7735_BLACK, 2, "V");

void setup() {
    Serial.begin(9600);
    tft.begin();                       /* SPI 8 МГц */
    tft.setRotation(0);                /* портрет 128×160 */
    tft.fillScreen(ST7735_BLACK);
    tft.setFont(&font7x11);

    volt.set(12.345, 3);               /* "12.345 V" */
    delay(1500);

    volt.set(0.5, 2);                  /* "0.50 V" — fillRect стирает "12.345 V" */
    delay(1500);

    volt.setError("ERR");              /* "ERR" красным — fillRect стирает число */
    delay(1500);

    volt.set(99.9, 1);                 /* "99.9 V" — fillRect стирает "ERR" */
    delay(1500);

    /* Смена единицы измерения с авто-перерисовкой */
    volt.setUnit("mA");
    volt.set(1234.5, 1);               /* "1234.5 mA" — может не влезть в 80px,
                                        * авто-масштаб снизит до scale=1 */
}

void loop() {
    /* Плавно меняем напряжение — авто-масштаб держит текст в виджете */
    static double v = 0.0;
    static int8_t dir = 1;
    v += dir * 0.137;
    if (v > 25.0)  { v = 25.0;  dir = -1; }
    if (v < -5.0)  { v = -5.0;  dir =  1; }
    volt.set(v, 2);
    delay(100);
}
