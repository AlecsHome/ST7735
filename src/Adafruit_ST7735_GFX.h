/* =====================================================================
 *  Adafruit_ST7735_GFX — мост между Adafruit_GFX и нашим ST7735
 *
 *  Класс наследует Adafruit_GFX (как интерфейс рендера — для пропорциональных
 *  шрифтов GFXfont) и содержит ST7735 по значению как backend для SPI.
 *
 *  Особенность Adafruit_GFX 1.10+: методы разделены на 2 слоя.
 *
 *  1) ВИРТУАЛЬНЫЕ (можно override):
 *     - drawPixel (pure virtual) — минимальный, переопределять обязательно
 *     - setRotation
 *     - startWrite / endWrite           ← открывают SPI-сессию
 *     - writePixel                      ← один пиксель внутри сессии
 *     - writeFastVLine / writeFastHLine ← быстрый примитив
 *     - writeFillRect                   ← заливка прямоугольника
 *
 *  2) НЕ-ВИРТУАЛЬНЫЕ публичные методы:
 *     drawCircle, fillCircle, drawTriangle, fillTriangle, drawRoundRect,
 *     fillRoundRect, drawBitmap, drawRGBBitmap — внутри себя вызывают
 *     startWrite → write* primitives → endWrite. Если переопределить write*
 *     (слой 1), все draw* из слоя 2 автоматически пойдут через наш ST7735
 *     с батчингом.
 *
 *  В этом классе для слоя 2 сделаны ТЕНИ (shadow methods) — методы с
 *  тем же именем, но без override. При вызове через конкретный тип
 *  `tft.drawCircle(...)` вызовется наш shadow (прямой путь в ST7735).
 *  При вызове через Adafruit_GFX* pointer вызовется родительский метод,
 *  который внутри использует наши write* переопределения — тоже эффективно.
 *
 *  Что получает пользователь:
 *    • setFont(const GFXfont*) — любой Adafruit 8pt/9pt/.../18pt шрифт,
 *      в т.ч. пропорциональные (DroidSerif, FreeMono и т.д.) — если шрифт
 *      содержит кириллицу, она будет отрисована. Стандартные Adafruit-шрифты
 *      кириллицу НЕ содержат — нужен кастом через fontconvert.exe из TTF
 *      с покрытием Cyrillic (DejaVuSans, FreeSerif).
 *    • print/println из Print — String, int, double, F("..."), работают из
 *      коробки (Adafruit_GFX наследует Print).
 *    • Все примитивы Adafruit_GFX — через виртуальные write* в наш ST7735,
 *      с батчингом SPI (а не pixel-by-pixel).
 *
 *  Чего НЕТ:
 *    • UTF-8 → CP1251 конверсии. Adafruit_GFX::write(uint8_t) рендерит байт
 *      как есть. Кириллицу нужно подавать в CP1251 (байты 0xC0-0xFF = А..я),
 *      либо подключить внешнюю utf8rus().
 *    • ST7735-специфичных методов напрямую (setTextScale, putStr5x7,
 *      termPutChar, ...). Если нужно — добавьте методы-обёртки.
 *
 * Условная компиляция: если ST7735_ENABLE_Adafruit = 0 ДО #include,
 * класс не компилируется вовсе — экономия ~0.5 КБ flash (на AVR Adafruit_GFX
 * тянет ~2 КБ своего кода). По умолчанию = 1 (включён).
 *
 *   #define ST7735_ENABLE_Adafruit 0   // ← перед #include, если не нужен
 *   #include <Adafruit_ST7735_GFX.h>
 * ===================================================================== */

#pragma once

#ifndef ST7735_ENABLE_Adafruit
  /* ДЕФОЛТ 0 — в стиле «включай только нужное». Чтобы активировать мост:
   *   #define ST7735_ENABLE_Adafruit 1
   *   #include <Adafruit_ST7735_GFX.h>
   *   Adafruit_ST7735_GFX tft(...);
   * Если не задавать макрос — класс не определится, и попытка его
   * использовать даст ясную ошибку компилятора. */
  #define ST7735_ENABLE_Adafruit 0
#endif

#if ST7735_ENABLE_Adafruit

#include <Adafruit_GFX.h>      /* Adafruit_GFX + GFXfont + Print */
#include <SPI.h>
#include "st7735.h"             /* наш драйвер как backend */

class Adafruit_ST7735_GFX : public Adafruit_GFX {
public:
    Adafruit_ST7735_GFX(int8_t cs, int8_t dc, int8_t rst,
                        uint8_t tab = ST7735R_RED)
        : Adafruit_GFX(128, 160),   /* raw dims — ST7735 1.77" */
          _tft(cs, dc, rst, tab) {}

    /* ---------- инициализация ---------- */
    void begin(uint32_t spiHz = 8000000UL) {
        _tft.begin(spiHz);
        setRotation(getRotation());     /* синхронизировать _width/_height */
    }

    /* ---------- СЛОЙ 1: ВИРТУАЛЬНЫЕ write* (real overrides) ----------
     * Это то, что Adafruit_GFX вызывает внутри своих draw* методов
     * (drawCircle, drawTriangle, ...) после startWrite. Переопределяем
     * их на наш ST7735 — батчинг SPI через счётчик _inWrite. */

    void startWrite(void) override { _tft.startWrite(); }
    void endWrite(void)   override { _tft.endWrite(); }

    /* drawPixel — pure virtual, обязательно */
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        _tft.drawPixel(x, y, color);
    }

    /* writePixel — единичный пиксель ВНУТРИ startWrite/endWrite сессии
     * (Adafruit_GFX зовёт без условного startWrite/endWrite). */
    void writePixel(int16_t x, int16_t y, uint16_t color) override {
        _tft.drawPixel(x, y, color);
    }

    void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
        _tft.drawFastVLine(x, y, h, color);
    }
    void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
        _tft.drawFastHLine(x, y, w, color);
    }
    void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
        _tft.fillRect(x, y, w, h, color);
    }

    /* ---------- СЛОЙ 1: другие виртуальные public ----------
     * (drawRect, fillRect, drawLine, fillScreen, drawFastVLine/HLine —
     * все виртуальные в Adafruit_GFX 1.10+, override корректен) */

    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
        _tft.drawFastVLine(x, y, h, color);
    }
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
        _tft.drawFastHLine(x, y, w, color);
    }
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
        _tft.drawRect(x, y, w, h, color);
    }
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
        _tft.fillRect(x, y, w, h, color);
    }
    void fillScreen(uint16_t color) override {
        _tft.fillScreen(color);
    }
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) override {
        _tft.drawLine(x0, y0, x1, y1, color);
    }

    /* ---------- ПОВОРОТ: синхронизация Adafruit_GFX ↔ ST7735 ---------- */
    void setRotation(uint8_t r) override {
        _tft.setRotation(r);                /* ST7735: MADCTL + пересчёт _width/_height */
        Adafruit_GFX::setRotation(r);      /* Adafruit_GFX: обновит свои dims */
    }

    /* ---------- СЛОЙ 2: SHADOW methods (НЕ override) ----------
     * В Adafruit_GFX 1.11.x эти методы НЕ виртуальные — мы их ТЕНИМ
     * (не override). При вызове через конкретный тип tft.drawCircle(...)
     * вызовется наш shadow (прямой путь в ST7735). При вызове через
     * Adafruit_GFX* pointer вызовется родительский метод, который
     * внутри использует наши write* переопределения — тоже эффективно,
     * но чуть дольше из-за startWrite/endWrite на каждый примитив. */

    void drawCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
        _tft.drawCircle(x, y, r, color);
    }
    void fillCircle(int16_t x, int16_t y, int16_t r, uint16_t color) {
        _tft.fillCircle(x, y, r, color);
    }
    void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2, uint16_t color) {
        _tft.drawTriangle(x0, y0, x1, y1, x2, y2, color);
    }
    void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                      int16_t x2, int16_t y2, uint16_t color) {
        _tft.fillTriangle(x0, y0, x1, y1, x2, y2, color);
    }
    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                       int16_t r, uint16_t color) {
        _tft.drawRoundRect(x, y, w, h, r, color);
    }
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                       int16_t r, uint16_t color) {
        _tft.fillRoundRect(x, y, w, h, r, color);
    }

    /* 1-бит битмапы. Adafruit signature: (x, y, bitmap, w, h, color).
     * Наш ST7735 signature: (x, y, w, h, bmp, color). Меняем порядок. */
    void drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                    int16_t w, int16_t h, uint16_t color) {
        _tft.drawBitmap(x, y, w, h, bitmap, color);
    }
    void drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                    int16_t w, int16_t h, uint16_t color, uint16_t bg) {
        _tft.drawBitmap(x, y, w, h, bitmap, color, bg);
    }

    /* RGB565 bitmap из RAM. Adafruit drawRGBBitmap хранит uint16_t* в RAM.
     * На AVR наш drawImage требует PROGMEM — используем drawImageRAM,
     * который читает как обычный указатель. */
    void drawRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap,
                       int16_t w, int16_t h) {
        _tft.drawImageRAM(x, y, w, h, bitmap);
    }

    /* ---------- СТАНДАРТНЫЕ Adafruit_GFX-методы (наследуются как есть) ----------
     * setFont(const GFXfont *), setCursor, setTextSize, setTextColor,
     * setTextWrap, write(uint8_t) — наследуются.
     * write() рендерит глифы через drawPixel/drawFastHLine, поддерживая
     * пропорциональные GFXfont'ы с метриками (width, xAdvance, xOffset,
     * yOffset). */

    /* ---------- ST7735-специфичные методы (обёртки для доступа) ---------- */
    void invertDisplay(bool on)  { _tft.invertDisplay(on); }
    void setDisplayOn(bool on)   { _tft.setDisplayOn(on); }
    void sleep(void)             { _tft.sleep(); }
    void wake(void)              { _tft.wake(); }
    void setOffsets(uint8_t colstart, uint8_t rowstart) {
        _tft.setOffsets(colstart, rowstart);
    }

    /* Низкий уровень (если нужно делать кастом-команды контроллеру):
     * вызывать между startWrite/endWrite. */
    void writeCommand(uint8_t c) { _tft.writeCommand(c); }
    void writeData8(uint8_t d)   { _tft.writeData8(d); }
    void writeData16(uint16_t d) { _tft.writeData16(d); }
    void setAddrWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
        _tft.setAddrWindow(x0, y0, x1, y1);
    }

    /* ST7735-шрифт (fixed-width): если GFXfont не нужен, можно переключиться
     * на st7735_font_t и пользоваться putStr5x7/drawString из нашего драйвера.
     * Но это переключит и рендер текста — Adafruit_GFX::write() перестанет
     * работать, пока не вызовете setFont(NULL) обратно (что вернёт
     * дефолтный 5×7 glcdfont.c). */
    void setFontST7735(const st7735_font_t *f) { _tft.setFont(f); }
    const st7735_font_t* fontST7735(void) const { return _tft.font(); }

    /* Доступ к внутреннему ST7735 (если нужно вызвать метод напрямую). */
    ST7735& tft(void) { return _tft; }

private:
    ST7735 _tft;
};

#endif /* ST7735_ENABLE_Adafruit */
