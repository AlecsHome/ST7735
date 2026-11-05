/* =====================================================================
 *  ellipse_test.ino — визуальная проверка drawEllipse/fillEllipse
 *
 *  Рисует три пары эллипсов с разными rx/ry:
 *    1) Круглый      10/10  — проверка симметрии круга, ровные дуги
 *    2) Сплюснутый   30/8   — крутой переход у полюсов, риск ступенек
 *                             в горизонтальной развёртке
 *    3) Вытянутый     5/20  — узкий высокий, риск вертикальных разрывов
 *                             при |dy/dx| >> 1
 *
 *  Что искать на экране:
 *    • Контур НЕ прерывистый — все точки соединены (8-связность).
 *    • Нет «дыр» в заливке.
 *    • Контур и заливка визуально совпадают по форме.
 *    • Нет ступенек на крутых участках (где xo(dy) - xo(dy+1) > 1).
 *
 *  Пины (типовая распиновка Arduino Uno + ST7735 1.77"):
 *    CS=10, DC=9, RST=8, SPI=ход 13/11.
 *    Меняйте под свою плату/распиновку.
 *
 *  Контроллер: ST7735R Red Tab (по умолчанию). Для чёрной платы
 *  замените ST7735R_RED на ST7735R_BLACK в конструкторе tft.
 * ===================================================================== */

#include <Arduino.h>
#include <SPI.h>
#include "st7735.h"

#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   8

ST7735 tft(TFT_CS, TFT_DC, TFT_RST, ST7735R_RED);

/* Рисует пару: контур слева + заливка справа.
 * halfW — половина ширины пары; используется, чтобы разместить оба
 * эллипса симметрично относительно cx. */
static void drawPair(int16_t cx, int16_t cy,
                     int16_t rx, int16_t ry,
                     uint16_t outlineColor, uint16_t fillColor)
{
    const int16_t halfW = rx + 2;              /* зазор между контуром и заливкой */
    tft.drawEllipse((int16_t)(cx - halfW), cy, rx, ry, outlineColor);
    tft.fillEllipse((int16_t)(cx + halfW), cy, rx, ry, fillColor);
}

/* Подпись по центру под эллипсом — та же ширина, что у заголовка,
 * чтобы все три подписи были выровнены по центру. */
static void drawLabel(int16_t cy, const char *text)
{
    tft.drawStringAligned(text, 0, cy, 128, ST7735_ALIGN_CENTER,
                          COLOR565_YELLOW, COLOR565_BLACK, 1);
}

void setup()
{
    Serial.begin(115200);
    Serial.println(F("ST7735 ellipse test: drawEllipse + fillEllipse"));

    tft.begin();
    tft.setRotation(0);                        /* портрет 128x160 */
    tft.fillScreen(COLOR565_BLACK);

    /* Заголовок — по центру верхней строки */
    tft.drawStringAligned("Ellipse Test", 0, 2, 128,
                          ST7735_ALIGN_CENTER,
                          COLOR565_WHITE, COLOR565_BLACK, 1);

    /* ---------- 1) Круглый 10/10 — слева контур, справа заливка -----
     * Контур должен быть ровной окружностью — никаких ступенек, никаких
     * разрывов. Контур и заливка должны визуально совпадать.
     */
    drawPair(64, 35, 10, 10, COLOR565_CYAN, COLOR565_BLUE);
    drawLabel(35 + 10 + 2, "10/10 round");

    /* ---------- 2) Сплюснутый 30/8 — только контур ------------------
     * Самый показательный: 8 строк, у полюсов |dy/dx| ≈ ∞, а у экватора
     * ок. Проверяем, что нет «ступенек» — горизонтальный сегмент длиной
     * k пикселей закрывает переход от xo(dy) к xo(dy+1).
     * Ширина 2*30+1 = 61 px — пара не помещается, рисуем только контур.
     */
    tft.drawEllipse(64, 80, 30, 8, COLOR565_YELLOW);
    drawLabel(80 + 8 + 2, "30/8 flat");

    /* ---------- 3) Вытянутый 5/20 — слева контур, справа заливка ----
     * Узкий высокий эллипс. На экваторе xo=5, у полюсов xo=0. Шаг по
     * dy = 1, шаг по xo <= 1 — простая вертикальная линия с диагональю
     * на полюсах. Пара 2*(2*5+1)+4 = 26 px — помещается.
     */
    drawPair(64, 125, 5, 20, COLOR565_GREEN, COLOR565_MAGENTA);
    drawLabel(125 + 20 + 2, "5/20 tall");

    Serial.println(F("Done. Visually check:"));
    Serial.println(F("  - outlines are continuous (8-connected)"));
    Serial.println(F("  - no stair-steps on steep parts"));
    Serial.println(F("  - fill matches outline shape"));
}

void loop()
{
    /* Статичный тест. Чтобы перерисовать — нажмите RESET на плате. */
}
