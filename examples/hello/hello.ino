/* =====================================================================
 *  hello.ino — минимальный "Hello, World" на ST7735
 *
 *  Проверка что драйвер работает: заливка экрана, текст, простые примитивы.
 *  Без UI-виджетов — чистый драйвер.
 *
 *  Пины: CS=10, DC=9, RST=8 (Arduino Uno + ST7735 1.77"/1.8")
 *  Таб: ST7735R_BLACK (1.77" с RGB). Если цвета инвертированы — ST7735R_RED.
 * ===================================================================== */

#include "st7735.h"

#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   8

ST7735 tft(TFT_CS, TFT_DC, TFT_RST, ST7735R_BLACK);

void setup() {
    Serial.begin(9600);
    tft.begin();                       /* SPI 8 МГц по умолчанию */
    tft.setRotation(0);                /* портрет 128×160 */

    /* Заливка */
    tft.fillScreen(ST7735_BLACK);

    /* Жёлтый текст вверху */
    tft.setTextColor(ST7735_YELLOW, ST7735_BLACK);
    tft.setFont(&font7x11);
    tft.setTextScale(2);
    tft.setCursor(5, 5);
    tft.print("Hello!");

    /* Белый текст ниже */
    tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
    tft.setTextScale(1);
    tft.setCursor(5, 35);
    tft.print("ST7735 driver");

    /* Кириллица через UTF-8 → CP1251 */
    tft.setCursor(5, 55);
    tft.print("Привет, мир!");

    /* Примитивы */
    tft.drawRect(5, 80, 60, 40, ST7735_GREEN);
    tft.fillRect(10, 85, 50, 30, ST7735_BLUE);

    tft.drawCircle(95, 100, 20, ST7735_RED);
    tft.fillCircle(95, 100, 15, ST7735_MAGENTA);

    tft.drawLine(5, 130, 123, 150, ST7735_CYAN);
    tft.drawLine(5, 150, 123, 130, ST7735_CYAN);

    /* Окружность с эллипсом */
    tft.drawEllipse(64, 100, 30, 20, ST7735_ORANGE);

    /* Числа */
    tft.setCursor(5, 155);
    tft.setTextColor(ST7735_CYAN, ST7735_BLACK);
    tft.print("pi=");
    tft.print(3.14159, 4);
}

void loop() {
    /* статичный экран */
}
