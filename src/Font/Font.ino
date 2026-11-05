#include "st7735.h"

#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   8

ST7735 tft(TFT_CS, TFT_DC, TFT_RST, ST7735R_BLACK);

void setup() {
    tft.begin();
    tft.setRotation(0);                  /* портрет 128x160 */
    tft.fillScreen(COLOR565_BLACK);

    /* Заголовок */
    tft.setTextColor(COLOR565_YELLOW, COLOR565_BLACK);
    tft.setTextScale(1);
    tft.setCursor(2, 0);
    tft.print("font7x11 table");

    /* Сетка глифов: 16 столбцов × 12 строк = 192 глифа */
    /* Каждый глиф 7x11 + 1px gap = 8x12 ячейка */
    /* 16 столбцов × 8 = 128 px — ровно по ширине */
    /* 12 строк × 12 = 144 px — помещается в 160 */

    const uint8_t scale = 1;
    const uint8_t cellW = 8;              /* 7 + 1 gap */
    const uint8_t cellH = 12;            /* 11 + 1 gap */
    const uint8_t cols = 16;
    const uint8_t rows = 12;
    const int16_t startY = 14;           /* под заголовком */

    /* Диапазон 1: 0x20-0x7F (96 глифов) — ASCII */
    /* Диапазон 2: 0xA0-0xFF (96 глифов) — CP1251 кириллица */

    for (uint8_t r = 0; r < rows; r++) {
        for (uint8_t c = 0; c < cols; c++) {
            uint16_t idx = r * cols + c;
            uint16_t code;
            if (idx < 96) {
                code = 0x20 + idx;       /* ASCII */
            } else {
                code = 0xA0 + (idx - 96);/* CP1251 */
            }

            int16_t x = c * cellW;
            int16_t y = startY + r * cellH;

            /* Рамка ячейки — тонкая, чтобы видеть границы */
            tft.drawRect(x, y, cellW, cellH, COLOR565_DARK_GRAY);

            /* Сам глиф */
            tft.setTextColor(COLOR565_WHITE, COLOR565_BLACK);
            tft.setTextScale(scale);
            tft.setCursor(x, y);
            tft.print((char)code);      /* драйвер сам конвертирует CP1251→индекс */
        }
    }

    /* Внизу — коды первой и последней ячейки для ориентации */
    tft.setTextColor(COLOR565_CYAN, COLOR565_BLACK);
    tft.setCursor(0, 152);
    tft.print("20-7F A0-FF");
}

void loop() {}
