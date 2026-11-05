#include "st7735.h"
#define ST7735_ENABLE_VALUE    1
#include "st7735ui.h"

#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   8

ST7735 tft(TFT_CS, TFT_DC, TFT_RST, ST7735R_BLACK);

ST7735Value valWidget(tft, 5, 130, 118, ST7735_ALIGN_CENTER, ST7735_GREEN, ST7735_BLACK, 2, "V");

void setup() {
    tft.begin();
    tft.setRotation(0); // Портрет 128x160
    tft.fillScreen(ST7735_BLACK);
    tft.setFont(&font7x11);

    // 1. Тест заголовок
    tft.drawStringAligned("ТЕСТ ФОНТА 7x11 (v9)", 0, 2, 128, ST7735_ALIGN_CENTER, ST7735_CYAN, ST7735_BLACK, 1);
    tft.drawFastHLine(0, 14, 128, COLOR565_GRAY);

    // 2. Проверка заглавных и строчных букв
    tft.drawString("АБВГДЕЁЖЗИЙКЛМНО", 2, 18, ST7735_WHITE, ST7735_BLACK, 1);
    tft.drawString("ПРСТУФХЦЧШЩЪЫЬЭЮЯ", 2, 30, ST7735_WHITE, ST7735_BLACK, 1);
    tft.drawString("абвгдеёжзийклмно", 2, 42, ST7735_YELLOW, ST7735_BLACK, 1);
    tft.drawString("прстуфхцчшщъыьэюя", 2, 54, ST7735_YELLOW, ST7735_BLACK, 1);

    // 3. Проверка переработанных глифов (Ч, д, л, и, ж, э)
    tft.drawString("Шишка Щука Дождь", 2, 68, ST7735_GREEN, ST7735_BLACK, 1);
    tft.drawString("Чечевица Элемент", 2, 80, ST7735_GREEN, ST7735_BLACK, 1);

    // 4. Крупный шрифт (scale = 2)
    tft.drawStringAligned("ЧЕЧЕВИЦА", 0, 96, 128, ST7735_ALIGN_CENTER, ST7735_MAGENTA, ST7735_BLACK, 2);

    delay(3000);
}

void loop() {
    static double counter = 0.0;
    counter += 0.123;
    if (counter > 99.9) counter = 0.0;

    valWidget.set(counter, 2);
    delay(150);
}
