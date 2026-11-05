/*
 * st7735.h — универсальный драйвер ST7735R/S, 160x128 (1.77"), RGB565
 * Платформы: Arduino IDE (AVR/ESP/STM32duino) и «голый» STM32 (HAL)
 * старый API: рисует абсолютными координатами.
 * ВНИМАНИЕ: не совместим с живым терминалом — текст ляжет поверх
 * области, но в _termBuf не попадёт; следующий termScroll/termRedraw
 * перерисует свою область и затрёт результат. При активном терминале
 * используйте print()/println() (или выключите терминал). 
 *  ── ЗАНЯТЫЕ ПИНЫ Arduino Uno/Nano/Pro Mini (ATmega328) ─────────────
 *  Аппаратный SPI (управляется библиотекой SPI, в конструкторе НЕ
 *  указывается — на AVR он физически закреплён за ножками чипа):
 *      11  MOSI  ──┬── TFT SDA / MOSI / DIN
 *      13  SCK   ──┴── TFT SCK / CLK
 *      12  MISO  ──── TFT MISO (если есть; иначе свободен, но pinMode = INPUT)
 *  Управляющие пины дисплея (передаются в конструктор ST7735):
 *      10  TFT_CS   (также SS AVR: держать OUTPUT!)
 *       9  TFT_DC
 *       8  TFT_RST
 *  Другое:
 *       0,1 — Serial RX/TX (USB-монитор)
 */

#pragma once
#include <stdint.h>
#include <stddef.h>         /* size_t — для inline st7735_fmtDouble */
#include <string.h>         /* memcpy — для inline st7735_fmtDouble */
#include "color565.h"

/* псевдонимы Adafruit-стиля для примеров */
#ifndef ST7735_BLACK
  #define ST7735_BLACK   COLOR565_BLACK
  #define ST7735_WHITE   COLOR565_WHITE
  #define ST7735_RED     COLOR565_RED
  #define ST7735_GREEN   COLOR565_LIME
  #define ST7735_BLUE    COLOR565_BLUE
  #define ST7735_CYAN    COLOR565_CYAN
  #define ST7735_MAGENTA COLOR565_MAGENTA
  #define ST7735_YELLOW  COLOR565_YELLOW
  #define ST7735_ORANGE  COLOR565_ORANGE
#endif

/* ---------- команды контроллера ---------- */
#define ST7735_NOP      0x00
#define ST7735_SWRESET  0x01
#define ST7735_SLPIN    0x10
#define ST7735_SLPOUT   0x11
#define ST7735_NORON    0x13
#define ST7735_INVOFF   0x20
#define ST7735_INVON    0x21
#define ST7735_DISPOFF  0x28
#define ST7735_DISPON   0x29
#define ST7735_CASET    0x2A
#define ST7735_RASET    0x2B
#define ST7735_RAMWR    0x2C
#define ST7735_MADCTL   0x36
#define ST7735_COLMOD   0x3A
#define ST7735_FRMCTR1  0xB1
#define ST7735_FRMCTR2  0xB2
#define ST7735_FRMCTR3  0xB3
#define ST7735_INVCTR   0xB4
#define ST7735_PWCTR1   0xC0
#define ST7735_PWCTR2   0xC1
#define ST7735_PWCTR3   0xC2
#define ST7735_PWCTR4   0xC3
#define ST7735_PWCTR5   0xC4
#define ST7735_VMCTR1   0xC5
#define ST7735_GMCTRP1  0xE0
#define ST7735_GMCTRN1  0xE1

/* ST7735 команды для вертикального скроллинга */
#define ST7735_VSCRDEF  0x33   /* Vertical Scrolling Definition */
#define ST7735_VSCSAD   0x37   /* Vertical Scrolling Start Address */

/* ---------- биты MADCTL ---------- */
#define ST7735_MADCTL_MY  0x80
#define ST7735_MADCTL_MX  0x40
#define ST7735_MADCTL_MV  0x20
#define ST7735_MADCTL_RGB 0x00
#define ST7735_MADCTL_BGR 0x08

#ifndef ST7735_MADCTL_COLOR
  #define ST7735_MADCTL_COLOR  ST7735_MADCTL_RGB   /* 0x00 */
#endif

/* ---------- настройки (можно переопределить до #include) ---------- */
/*
AVR + без огромных цифр MAX_PX=28 (72 байта RAM не лишние)
AVR + нужны крупные цифры (часы, термометр) MAX_PX=32 (компромисс: 64 байта экономии, scale=4 для font7x11 работает)

╔═══════════════╦═══════════════════╦════════════════════════════╗
║   MAX_PX      ║ `_charBuf` размер ║ Экономия на AVR (2 КБ RAM) ║
╠═══════════════╬═══════════════════╬════════════════════════════╣
║ 64            ║ 128 байт          ║                            ║
║ 28 (дефолт)   ║ 56 байт           ║ 72 байта (3.5% RAM)        ║
║ 32            ║ 64 байт           ║ 64 байт (3.1% RAM)         ║
║ 16            ║ 32 байт           ║ 96 байт (4.7% RAM)         ║
╚═══════════════╩═══════════════════╩════════════════════════════╝
╔══════════════╦═══╦═════╦════════════════════╦════════════════════════╦════════════════════╗
║ Шрифт        ║ w ║(w+1)║    MAX_PX=64       ║   MAX_PX=28            ║  MAX_PX=32         ║
╠══════════════╬═══╬═════╬════════════════════╬════════════════════════╬════════════════════╣
║ **font5x7**  ║ 5 ║  6  ║ scale 10 (60x70 px)║ **scale 4** (24x28 px) ║ scale 5 (30x35 px) ║
║ **font7x11** ║ 7 ║  8  ║ scale 8 (64x88 px) ║ **scale 3** (24x33 px) ║ scale 4 (32x44 px) ║
╚══════════════╩═══╩═════╩════════════════════╩════════════════════════╩════════════════════╝
*/
#ifndef ST7735_TEXT_MAX_PX
  #define ST7735_TEXT_MAX_PX  28     // 64   /* макс. ширина глифа, px (буфер = 128 байт RAM) */
#endif
#ifndef ST7735_TEXT_MAX_H
  #define ST7735_TEXT_MAX_H   16     /* макс. высота шрифта (bpc=2 -> 16 строк) */
#endif
#ifndef ST7735_TERM_ROWS
  #define ST7735_TERM_ROWS    16     /* строки терминала (RAM = ROWS*COLS байт) */
#endif
#ifndef ST7735_TERM_COLS
  #define ST7735_TERM_COLS    27     /* столбцы терминала */
#endif
#ifndef ST7735_ENABLE_TERMINAL
  /* 0 = терминальный модуль не компилируется вовсе: экономия ~1.5 КБ flash
   * и 444 Б RAM на AVR (буфер _termBuf не существует). setTerminal()/
   * termClear() остаются заглушками, print() пишет обычным курсором. */
  #define ST7735_ENABLE_TERMINAL  0 //1
#endif

#ifndef ST7735_ENABLE_Adafruit
  /* Макрос-преднастройка для Adafruit_ST7735_GFX.h (отдельный файл моста).
   *   • В самом st7735.h / st7735.cpp / st7735ui.h НИКОГДА не проверяется —
   *     основной драйвер не зависит от Adafruit_GFX.
   *   • Если пользователь подключит #include <Adafruit_ST7735_GFX.h>,
   *     мост увидит уже заданное значение (или свой дефолт = 0).
   *   • Чтобы ВКЛЮЧИТЬ мост: #define ST7735_ENABLE_Adafruit 1 ПЕРЕД
   *     #include <Adafruit_ST7735_GFX.h> (дефолт 0 — «включай только нужное»).
   * Сам по себе #include "st7735.h" никогда не тянет Adafruit_GFX. */
  #define ST7735_ENABLE_Adafruit 0
#endif

#ifndef ST7735_ENABLE_PRINTF
  /* 0 = метод printf не компилируется вовсе — экономия ~200 Б flash
   *     + vsnprintf из libc не подключается (если никто больше его не зовёт),
   *       а это ещё ~1-2 КБ экономии на AVR.
   * 1 = доступно tft.printf(fmt, ...). %f требует отдельного флага линкера
   *     (-Wl,-u,vfprintf -lprintf_flt) для libprintf_flt. */
  #define ST7735_ENABLE_PRINTF  0  // 1
#endif

#ifndef ST7735_CHUNK
  #if defined(__AVR__)
    #define ST7735_CHUNK  64              /* 128 Б стек-буфер */
  #else
    #define ST7735_CHUNK  256             /* 512 Б — не проблема для STM32/ESP32 */
  #endif
#endif
/* ---------- вариант платы ST7735R («таб») ---------- */
enum ST7735Tab {
  ST7735R_RED    = 0,   /* красная плёнка: (0,0), BGR, без инверсии   */
  ST7735R_GREEN  = 1,   /* зелёная: смещения (2,1)                    */
  ST7735R_GREEN3 = 2,   /* зелёная поздняя: (2,1) + INVON             */
  ST7735R_BLACK  = 3    /* чёрная: (0,0), RGB — типично для 1.77" 160x128 */
};

enum ST7735Align { 
  ST7735_ALIGN_LEFT = 0,
  ST7735_ALIGN_CENTER,
  ST7735_ALIGN_RIGHT 
};

#ifndef ST7735_ENABLE_ARC
  #define ST7735_ENABLE_ARC  0   /* 1 = fillArc (тянет sinf/cosf из libm, ~1 КБ) */
#endif

/* RGB565 из 24-бит RGB */
inline uint16_t RGB565(uint8_t R, uint8_t G, uint8_t B) {
    return ((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3);
}

/* ---------- portable finality / range check ----------
 * Без math.h, без <#include> — работает на AVR (double=float), ESP32, STM32.
 *   • NaN:        v != v           (IEEE 754)
 *   • ±Inf:       v - v != 0       (Inf - Inf = NaN, NaN != 0 → true)
 *   • finite:     v == v И v - v == 0
 * Используется в ST7735BarGauge / ST7735Plotter setRange() и конструкторах,
 * чтобы отклонить setRange(0, INFINITY), setRange(-1, +Inf) и т.п. —
 * старая проверка (minVal != minVal || maxVal != maxVal || minVal >= maxVal)
 * пропускала +Inf в maxVal, т.к. +Inf == +Inf (NaN-тест ложный) и
 * finite < +Inf (>= тест ложный). */
inline bool st7735_isfinite(double v) {
    return (v == v) && ((v - v) == 0.0);
}
inline bool st7735_range_ok(double minVal, double maxVal) {
    return st7735_isfinite(minVal) && st7735_isfinite(maxVal) && (minVal < maxVal);
}
/* ---------- portable double -> string formatter ----------
 * Без зависимостей: не использует dtostrf, snprintf, math.h.
 * Те же правила, что в ST7735::print(double):
 *   • digits ограничен 0..6 (после 6 — потеря точности double)
 *   • NaN/Inf/|d| > 4294967040.0 → "OVF" (return 3)
 *   • Округление HALF-AWAY-FROM-ZERO: d += 0.5 / 10^digits ПЕРЕД форматированием
 *     (0.5 → 1, -0.5 → -1). НЕ «банковское» (round-half-to-even) — термин
 *     в ранних комментариях был ошибочен.
 *   • Минус не печатается, если число округляется в ноль: -0.001 при digits=2 → "0.00"
 * Используется из ST7735::print(double) и ST7735Value::formatNumber —
 * убирает дублирование (~150 Б flash экономии).
 * buf должен быть >= 19 байт. Возвращает длину записи без NUL. */
inline size_t st7735_fmtDouble(double d, uint8_t digits, char *buf, size_t n) {
    if (n < 19) { if (n) buf[0] = 0; return 0; }  /* худший случай: знак+10цифр+точка+6знаков+NUL=19 */
    if (digits > 6) digits = 6;
    if (d != d || d > 4294967040.0 || d < -4294967040.0) {
        memcpy(buf, "OVF", 4);
        return 3;
    }
    double r = 0.5;
    for (uint8_t k = 0; k < digits; k++) r /= 10.0;

    size_t i = 0;
    if (d < 0) {
        if (-d >= r) buf[i++] = '-';       /* -0.001 при digits=2 -> "0.00" */
        d = -d;
    }
    d += r;

    unsigned long ip = (unsigned long)d;
    char tb[10]; uint8_t t = 0;
    do { tb[t++] = (char)('0' + ip % 10); ip /= 10; } while (ip);
    while (t) buf[i++] = tb[--t];

    if (digits) {
        buf[i++] = '.';
        double rem = d - (double)(unsigned long)d;
        while (digits--) {
            rem *= 10.0;
            /* Truncate без epsilon: d += r выше уже сделал половинное
             * округление (HALF-AWAY-FROM-ZERO), поэтому truncate даёт
             * правильную цифру. +1e-9 не нужен и вреден — может дать
             * ложный dg=10 → clamp 9, после rem -= 9 остаток ≈ 1.0,
             * следующая итерация снова dg≈10 → clamp 9 — накопление
             * ошибки в младших разрядах ("1.00" → "0.99").
             * Safety `if (dg > 9)` оставляем на случай 9.999... IEEE-754. */
            uint8_t dg = (uint8_t)rem;
            if (dg > 9) dg = 9;
            buf[i++] = (char)('0' + dg);
            rem -= dg;
        }
    }
    buf[i] = 0;
    return i;
}

/* ---------- дескриптор шрифта ----------
 * data: колонки по bpc байт (bpc=1 -> высота до 8, bpc=2 -> до 16),
 * бит (col,row) = (data[глиф*W*bpc + col*bpc] >> row) & 1             */
typedef struct st7735_font_t {
  const uint8_t *data;   /* байты глифов                        */
  uint8_t  w;            /* ширина клетки, px                   */
  uint8_t  h;            /* высота клетки, px                   */
  uint8_t  bpc;          /* байт на колонку (col-major); =1 для row-major */
  uint8_t  flags;        /* бит0: 1 = row-major (7x11)          */
  uint16_t first;        /* первый код диапазона 1              */
  uint16_t count;        /* число глифов диапазона 1            */
  uint16_t first2;       /* первый код диапазона 2 (0 = нет)    */
  uint16_t count2;       /* число глифов диапазона 2            */
} st7735_font_t;

extern const st7735_font_t font5x7;
extern const st7735_font_t font7x11;

/* =====================================================================
 *  ПЛАТФОРМЕННЫЙ СЛОЙ
 * ===================================================================== */
#if defined(ARDUINO) && !defined(ST7735_BARE_STM32)
  #define ST7735_PLAT_ARDUINO  1
#else
  #define ST7735_PLAT_STM32HAL 1
#endif

#if defined(ST7735_PLAT_ARDUINO)          /* ---------- Arduino ---------- */

  #include <Arduino.h>
  #include <SPI.h>

  #define ST7735_DELAY_MS(ms)  delay(ms)
  void st7735_spi_write(const uint8_t *data, uint32_t len);   /* в .cpp */

#else                                     /* ---------- STM32 HAL ---------- */

  #include "main.h"          /* заголовок CubeMX-проекта */

  #ifndef ST7735_SPI_HANDLE
    extern "C" { extern SPI_HandleTypeDef hspi1; }
    #define ST7735_SPI_HANDLE (&hspi1)
  #endif

  /* пины по вашей старой схеме: CS=PA3, DC=PA4, RST=PA2, SPI1 (PA5/PA7) */
  #ifndef ST7735_CS_GPIO
    #define ST7735_CS_GPIO   GPIOA
  #endif
  #ifndef ST7735_CS_PIN
    #define ST7735_CS_PIN    GPIO_PIN_3
  #endif
  #ifndef ST7735_DC_GPIO
    #define ST7735_DC_GPIO   GPIOA
  #endif
  #ifndef ST7735_DC_PIN
    #define ST7735_DC_PIN    GPIO_PIN_4
  #endif
  #ifndef ST7735_RST_GPIO
    #define ST7735_RST_GPIO  GPIOA
  #endif
  #ifndef ST7735_RST_PIN
    #define ST7735_RST_PIN   GPIO_PIN_2
  #endif

  #define ST7735_CS_LOW()   HAL_GPIO_WritePin(ST7735_CS_GPIO,  ST7735_CS_PIN,  GPIO_PIN_RESET)
  #define ST7735_CS_HIGH()  HAL_GPIO_WritePin(ST7735_CS_GPIO,  ST7735_CS_PIN,  GPIO_PIN_SET)
  #define ST7735_DC_LOW()   HAL_GPIO_WritePin(ST7735_DC_GPIO,  ST7735_DC_PIN,  GPIO_PIN_RESET)
  #define ST7735_DC_HIGH()  HAL_GPIO_WritePin(ST7735_DC_GPIO,  ST7735_DC_PIN,  GPIO_PIN_SET)
  #define ST7735_RST_LOW()  HAL_GPIO_WritePin(ST7735_RST_GPIO, ST7735_RST_PIN, GPIO_PIN_RESET)
  #define ST7735_RST_HIGH() HAL_GPIO_WritePin(ST7735_RST_GPIO, ST7735_RST_PIN, GPIO_PIN_SET)

  #define ST7735_DELAY_MS(ms)  HAL_Delay(ms)
  /* микросекундной задержке здесь не место: HAL_Delay работает в мс, а для
   * ST7735-таймингов используются только SWRESET/SLPOUT-паузы >= 1 мс.
   * Короткую DWT-CYCNT или TIM-задержку добавляйте локально, когда нужно. */

  static inline void st7735_spi_write(const uint8_t *data, uint32_t len) {
    HAL_SPI_Transmit(ST7735_SPI_HANDLE, (uint8_t *)data, (uint16_t)len, 100);
  }
#endif

/* =====================================================================
 *  КЛАСС
 * ===================================================================== */
class ST7735 {
public:
    ST7735(int8_t cs = -1, int8_t dc = -1, int8_t rst = -1,
           uint8_t tab = ST7735R_BLACK);

    void begin(uint32_t spiHz = 8000000UL);   

    /* ---- ориентация ---- */
    void     setRotation(uint8_t r);          /* 0..3 */
    uint8_t  getRotation(void) const { return _rotation;  };
    int16_t  width(void)  const { return (int16_t)_width ;};
    int16_t  height(void) const { return (int16_t)_height;};
    void     setOffsets(uint8_t colstart, uint8_t rowstart);

    /* ---- сессия SPI ---- */
    void startWrite(void);
    void endWrite(void);

    /* ---- низкий уровень: вызывать МЕЖДУ startWrite/endWrite ---- */
    void writeCommand(uint8_t c);
    void writeData8(uint8_t d);
    void writeData16(uint16_t d);              /* старший байт вперёд */
    void setAddrWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1); /* + RAMWR */
    void writePixels(const uint16_t *colors, uint32_t count);
    void writePixelsBytes(const uint8_t *bytes, uint32_t count);
    void writeColor(uint16_t color, uint32_t count);

    /* ---- примитивы ---- */
    void drawPixel(int16_t x, int16_t y, uint16_t color);
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color);
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color);
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void fillScreen(uint16_t color)
        { fillRect(0, 0, (int16_t)_width, (int16_t)_height, color); }
    void clear(uint16_t color = ST7735_BLACK);


    /* ---- изображения ----
    * drawImage / drawBitmap      — источник в PROGMEM (AVR) или обычной памяти
    *                               (ESP32/STM32, где флеш отображён в RAM).
    *                               На AVR: массив ОБЯЗАН быть объявлен PROGMEM,
    *                               иначе прочитается мусор (pgm_read_* идёт в flash).
    * drawImageRAM / drawBitmapRAM — источник в RAM (обычный массив без PROGMEM).
    *                               Работает на всех платформах, включая AVR. */

    void drawImage(int16_t x, int16_t y, int16_t w, int16_t h,
               const uint16_t *px);
    void drawImageRAM(int16_t x, int16_t y, int16_t w, int16_t h,
                  const uint16_t *px);
    void drawBitmap(int16_t x, int16_t y, int16_t w, int16_t h,
                const uint8_t *bitmap, uint16_t fg);
    void drawBitmapRAM(int16_t x, int16_t y, int16_t w, int16_t h,
                   const uint8_t *bitmap, uint16_t fg);
    void drawBitmap(int16_t x, int16_t y, int16_t w, int16_t h,
                const uint8_t *bitmap, uint16_t fg, uint16_t bg);
    void drawBitmapRAM(int16_t x, int16_t y, int16_t w, int16_t h,
                   const uint8_t *bitmap, uint16_t fg, uint16_t bg);
    void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
    void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);
    void setScrollArea(int16_t topFixed, int16_t scrollHeight, int16_t botFixed);
    void scrollTo(int16_t offset);

    /* ---- управление ---- */
    void invertDisplay(bool on);
    void setDisplayOn(bool on);
   
    /* ---- питание ---- */
    void sleep(void);                 /* SLPIN, дисплей гаснет, GRAM сохраняется */
    void wake(void);                  /* SLPOUT + DISPON, блокирует 120 мс */

    /* ---- текст ----
    * setTextWrap действует на print/drawString/putStr5x7:
    *   true  — перенос на новую строку
    *   false — остановка вывода (обрезка)
    * Терминал переноса не отключает (у него автоскролл).        */
    void setFont(const st7735_font_t *f);
    const st7735_font_t* font(void) const { return _font; }
    void setTextScale(uint8_t s);
    void setTextColor(uint16_t fg);                  /* прозрачный фон  */
    void setTextColor(uint16_t fg, uint16_t bg);     /* непрозрачный    */
    void setTextWrap(bool w)    { _wrap = w; }
    void setCursor(int16_t x, int16_t y) { _cx = x; _cy = y; }
    int16_t cursorX(void) const { return _cx; }
    int16_t cursorY(void) const { return _cy; }
    
    // Залить зону и вернуть её как «буфер» для последующей перерисовки?
    // Нет — RAM на AVR нет. Вместо этого:

    // 1. Очистить зону под строку фиксированной высоты (часто нужно).
    //    Раньше был один метод clearLine с часовым параметром bg=0xFFFF —
    //    белый фон (0xFFFF) было невозможно передать явно. Две перегрузки
    //    с одним именем тоже ловушка: clearLine(x,y,w,0xFFFF) попадало в
    //    scale-перегрузку (0xFFFF → scale=255, компилятор предупреждает, но
    //    код компилируется), а старый вызов clearLine(x,y,w,scale,bg) тихо
    //    менял смысл аргументов местами. Решение — два РАЗНЫХ имени:
    //      clearLine(x,y,w)         — bg из текущего textBg (или BLACK)
    //      clearLine(x,y,w,scale)   — то же, с масштабом
    //      clearLineBg(x,y,w,bg)            — явно заданный bg (в т.ч. WHITE)
    //      clearLineBg(x,y,w,bg,scale)      — явно заданный bg + масштаб
    void clearLine(int16_t x, int16_t y, int16_t w, uint8_t scale = 1) {
        fillRect(x, y, w, (int16_t)lineHeight(scale),
                 _textBgValid ? _textBg : ST7735_BLACK);
    }
    void clearLineBg(int16_t x, int16_t y, int16_t w, uint16_t bg, uint8_t scale = 1) {
        fillRect(x, y, w, (int16_t)lineHeight(scale), bg);
    }
    
    /* ---------- вывод: строки, символы, числа ---------- */
    void print(const char *s);
    void print(char c)          { char s[2] = { c, 0 }; print(s); }

    /* 1-аргументные — без base, печать как десятичное число */
    void print(int n)           { print((long)n); }
    void print(unsigned int n)  { print((unsigned long)n); }
    void print(long n);
    void print(unsigned long n);
    void print(double d, uint8_t digits = 2);

    void println(int n)           { print(n);     println(); }
    void println(unsigned int n)  { print(n);     println(); }
    void println(long n)          { print(n);     println(); }
    void println(unsigned long n) { print(n);     println(); }

    /* 2-аргументные — с основанием системы счисления (2..36)
     * Знаковые (int, long): printBase(long) — с минусом для отрицательных base==10.
     * Беззнаковые (unsigned int, unsigned long): printUnsignedBase(unsigned long) —
     * без ветки negative, без cast к long. Для 0x80000000UL в BIN даёт
     * "10000000000000000000000000000000" (32 цифры), а не "-10000000...0".
     * В DECIMAL (base==10 или base==0) unsigned идут в print(unsigned long) —
     * там всегда положительный десятичный вывод без знака. */
    void print(int n,           uint8_t base) { if (base == 10 || base == 0) print((long)n);          else printBase((long)n, base); }
    void print(unsigned int n,  uint8_t base) { if (base == 10 || base == 0) print((unsigned long)n); else printUnsignedBase((unsigned long)n, base); }
    void print(long n,          uint8_t base) { if (base == 10 || base == 0) print(n);                else printBase(n, base); }
    void print(unsigned long n, uint8_t base) { if (base == 10 || base == 0) print(n);                else printUnsignedBase(n, base); }

    void println(void)               { print("\n"); }
    void println(const char *s)      { print(s);     println(); }
    void println(char c)             { print(c);     println(); }
    void println(int n,           uint8_t base) { print(n, base); println(); }
    void println(unsigned int n,  uint8_t base) { print(n, base); println(); }
    void println(long n,          uint8_t base) { print(n, base); println(); }
    void println(unsigned long n, uint8_t base) { print(n, base); println(); }
    void println(double d, uint8_t digits = 2) { print(d, digits); println(); }
    
    /* ВНИМАНИЕ: сравнение только по символу, не по цвету.
     * Если изменился только fg/bg — символ не перерисуется.
     * Для принудительной перерисовки передайте prev_str="" (пустую). */
    void drawChangedString(int16_t x, int16_t y,
                       const char *new_str, const char *prev_str,
                       uint16_t fg, uint16_t bg, uint8_t scale = 1);
    /* ---- примитивы ---- */
    void drawEllipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint16_t color);
    void fillEllipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint16_t color);

#if ST7735_ENABLE_ARC
    /* кольцевой сектор; углы в градусах по часовой от 12 часов, endDeg > startDeg */
    void fillArc(int16_t x0, int16_t y0, int16_t rOut, int16_t rIn,
                 int16_t startDeg, int16_t endDeg, uint16_t color);
#endif
    /* value/prev/vmax: prev = -1 при первой отрисовке (рисуется рамка и фон) */
    void drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h,
                         int32_t value, int32_t prev, int32_t vmax,
                         uint16_t fg, uint16_t bg, uint16_t border);
    /* 1-бит битмап с масштабом sc; invert меняет нули и единицы; фон не заливается */
    void drawBitmapScaled(int16_t x, int16_t y, int16_t w, int16_t h,
                          const uint8_t *bmp, uint16_t fg, uint8_t scale,
                          bool invert = false);

    /* ---- текст ---- */
    void drawStringAligned(const char *s, int16_t x, int16_t y, int16_t w,
                           uint8_t align, uint16_t fg, uint16_t bg, uint8_t scale = 1);
    uint16_t textHeight(uint8_t scale = 0) const;   /* h*scale; 0 = текущий _textScale */
    uint16_t lineHeight(uint8_t scale = 0) const;   /* (h+1)*scale: шаг строки */

#if ST7735_ENABLE_PRINTF
    void printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#endif

#if defined(ST7735_PLAT_ARDUINO)        /* F("...") и String — только Arduino */
    void print(const __FlashStringHelper *ifs);
    void println(const __FlashStringHelper *ifs) { print(ifs); println(); }

    /* String (Arduino WString.h) — передаётся в print(const char*) через .c_str().
     * Без этих перегрузок tft.print(someString) давал бы "no matching function"
     * — String неявно не приводится к const char*. */
    void print(const String &s)        { print(s.c_str()); }
    void println(const String &s)      { print(s);  println(); }
#endif

    uint16_t drawChar(int16_t x, int16_t y, uint16_t code, uint16_t fg, uint16_t bg, uint8_t scale);
    uint16_t drawChar(int16_t x, int16_t y, uint16_t code, uint16_t fg, uint8_t scale); /* прозрачный */
    void drawString(const char *s, int16_t x, int16_t y, uint16_t fg, uint16_t bg, uint8_t scale = 1);
    void putStr5x7(uint8_t scale, uint16_t x, uint16_t y,
                   const char *str, uint16_t color, uint16_t bgcolor);
    uint16_t textWidth(const char *s) const;
    uint16_t textWidth(const char *s, uint8_t scale) const;
    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                       int16_t r, uint16_t color);
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                       int16_t r, uint16_t color);
    void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                  int16_t x2, int16_t y2, uint16_t color);
    void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                  int16_t x2, int16_t y2, uint16_t color);

                  

     /* ---- терминал с автоскроллом ----
     * print()/println() пишут в область, при заполнении строки едут вверх.
     * Поворот терминал не пересчитывает.
     * Модуль отключается: #define ST7735_ENABLE_TERMINAL 0 до #include
     * (методы остаются, но превращаются в заглушки). */

#if ST7735_ENABLE_TERMINAL
    void setTerminal(int16_t x, int16_t y, int16_t w, int16_t h);
    void setTerminal(void)          { _termOn = false; }      /* выключить */
    void setTerminalBg(uint16_t c) {
         _termBg = c;
          if (_termOn) {
           fillRect(_termX, _termY, _termW, _termH, c);   /* перекрасить промежутки и дно */
           termRedraw();
           }
         }
    void termClear(void);
    bool terminalOn(void) const     { return _termOn; }
    void termHwScroll(bool on);
#else
    void setTerminal(int16_t, int16_t, int16_t, int16_t) { _termOn = false; }
    void setTerminal(void)          { _termOn = false; }
    void setTerminalBg(uint16_t)    { }
    void termClear(void)            { }
    bool terminalOn(void) const     { return false; }
    void termHwScroll(bool)         { }
#endif


private:
    void hwReset(void);
    void runInitTable(void);
    void csLow(void);  void csHigh(void);
    void dcLow(void);  void dcHigh(void);
    void rstLow(void); void rstHigh(void);

    void renderCharGI(int16_t x, int16_t y, uint16_t gi,
                      const st7735_font_t *f,           /* ← указатель, не uint16_t */
                      uint16_t fg, uint16_t bg, uint8_t scale, bool opaque);
    void drawCircleHelper(int16_t x0, int16_t y0, int16_t r,
                          uint8_t cornername, uint16_t color);
    void fillCircleHelper(int16_t x0, int16_t y0, int16_t r,
                          uint8_t corners, int16_t delta, uint16_t color);
    void writePixelsSwap(const uint16_t *px, uint32_t count);       /* PROGMEM/flash */
    void drawFastVLines2(int16_t xa, int16_t xb, int16_t y, int16_t h, uint16_t color);
    void printBase(long n, uint8_t base);
    void printUnsignedBase(unsigned long u, uint8_t base);


#if ST7735_ENABLE_TERMINAL
    void termPutChar(uint16_t ch);
    void termScroll(uint16_t rows);
    void termRedraw(void);
    bool termHwActivate(void);
    void termHwReset(void);
    uint16_t termHwRowLine(uint16_t r, uint16_t rows, uint16_t lineH) const;
#endif

    /* порядок полей = порядок в init-листе конструктора */
    int16_t  _cs, _dc, _rst;
    uint8_t  _rotation;
    uint16_t _width, _height;
    uint8_t  _colstart, _rowstart;
    uint8_t  _xoff, _yoff;
    uint32_t _spiHz;
    uint8_t  _madctlRgb;
    bool     _invertInit;

    const st7735_font_t *_font;
    uint8_t  _textScale;
    uint16_t _textFg, _textBg;
    bool     _textBgValid, _wrap;
    int16_t  _cx, _cy;
    uint8_t  _charBuf[ST7735_TEXT_MAX_PX * 2];  /* ячейка MAX_PX px, RGB565 = 2 Б/пиксель */

    bool     _termOn;                           /* живёт всегда: 1 байт, включает заглушки */
    uint8_t  _inWrite;                          /* bool → uint8_t: счётчик вложенности */
    int16_t  _wx0, _wx1, _wy0, _wy1;            /* кэш окна (с учётом _xoff/_yoff), -1 = не задано */

#if ST7735_ENABLE_TERMINAL
    uint8_t  _termBuf[ST7735_TERM_ROWS][ST7735_TERM_COLS];
    int16_t  _termX, _termY, _termW, _termH;
    uint8_t  _termRow, _termCol;
    uint16_t _termBg;
    int16_t  _termTfa;        /* TFA: физическая GRAM-строка начала области */
    uint16_t _termVsa;        /* VSA: высота области прокрутки (строк GRAM) */
    uint16_t _termVspOff;     /* текущий сдвиг VSP-TFA, 0..VSA-1 (в пикселях GRAM) */
    uint16_t _termHwLineH;    /* lineH шрифта, под которую настроена область */
    bool     _termHwReq;      /* запрос пользователя termHwScroll(true) */
    bool     _termHwOn;       /* область настроена и активна */
    /* ВНИМАНИЕ: направление VSP и формула termHwRowLine согласованы
     * между собой, но требуют проверки на реальном железе. Семантика
     * VSCSAD (строка на нижнем vs верхнем краю области) различается
     * между даташитами ST7735 и реализациями. */
#endif
};