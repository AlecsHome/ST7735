/* =====================================================================
    ТЕРМИНАЛ — инварианты (для сопровождения):
    1. pitch/lineH: setFont гарантирует (w+1) <= ST7735_TEXT_MAX_PX, а
       st7735_scale ограничивает scale так, что (w+1)*scale <=
       ST7735_TEXT_MAX_PX (28 по дефолту) — ни int16-переполнений, ни выхода за _charBuf.
    2. _termBuf: строка заполнена плотно от col 0 до первого NUL.
       '\r' отматывает в 0 без очистки хвоста (перезапись, как в
       настоящем терминале). termRedraw вправе останавливаться на NUL.
    3. cols==0 (символ шире области): termPutChar отбрасывает вывод,
       termRedraw только заливает фон — деградация корректная.
    4. Нижняя неполная строка (_termH % lineH): НЕ очищается при termRedraw,
       сохраняет содержимое. Годится под статус-полосу — она переживёт
       termScroll без затирания. При смене _termBg (setTerminalBg) заливается
       целиком, потом перерисовывается глифами.
    5. Столбец TERM_COLS-1 — резерв под гарантированный NUL,
       данными не занимается (см. termPutChar/termRedraw клампы).
    6. Весь терминальный блок условной компиляцией ST7735_ENABLE_TERMINAL
       (см. st7735.h): при 0 экономится ~1.5 КБ flash и RAM
       444 буфер + 8 координат + 2 счётчика + 2 фон
    7. На AVR %f не работает без -Wl,-u,vfprintf -lprintf_flt в линкере. 
   ===================================================================== */

#include "st7735.h"
#include <string.h>      /* memset/memcpy — явно */
#include "font5x7.h"
#include "font7x11.h"

const st7735_font_t font5x7  = {
    Font5x7,    // data
    5,          // w
    8,          // h ← было 7; данные шрифта используют bit 7 для descender'ов
                //   (g, p, q, y, Д, Ц, Щ, д, ф, ц, щ — у всех в данных выставлен
                //    старший бит колонки). При h=7 рендерер (renderCharGI) читал
                //    только row < 7 — bit 7 терялся, хвостики обрезались.
                //   h=8 включает bit 7. bpc=1 остаётся достаточным: для col-major
                //   нужно bpc >= (h+7)/8 = 1. ST7735_TEXT_MAX_H (16) >= 8. Побочные
                //   эффекты: lineHeight (h+1)*scale = 9*scale вместо 8*scale —
                //   чуть больше межстрочный интервал в терминале, textHeight = 8*scale.
    1,          // bpc
    0,          // flags: 0 = column-major (байт = колонка)
    0x20,       // first
    96,         // n1
    0xA0,       // second
    96          // n2
};

const st7735_font_t font7x11 = {
    Font7x11,   // data
    7,          // w
    11,         // h
    1,          // bpc
    1,          // flags: 1 = row-major (байт = строка)
    0x20,       // first
    96,         // n1
    0xA0,       // second
    96          // n2
};

/* PROGMEM только на AVR */
#if defined(__AVR__) || defined(ESP8266)
#include <avr/pgmspace.h>
#define ST7735_PGM    PROGMEM
#define ST7735_RD8(a) pgm_read_byte(a)
#define ST7735_RD16(a) pgm_read_word(a)
#else
#define ST7735_PGM
#define ST7735_RD8(a)  (*(const uint8_t *)(a))
#define ST7735_RD16(a) (*(const uint16_t *)(a))
#endif

/* ---------- SPI: Arduino ---------- */
#if defined(ST7735_PLAT_ARDUINO)
void st7735_spi_write(const uint8_t *data, uint32_t len) {
#if defined(ESP32) || defined(ESP8266)
    SPI.writeBytes((uint8_t *)data, len);
#elif defined(__AVR__) && defined(SPDR) && defined(SPIF)
    /* По даташиту: SPIF сбрасывается последовательностью "чтение SPSR + доступ к SPDR".
     * Очищаем возможный висящий SPIF перед началом, затем в цикле чтение SPSR в
     * ожидании + следующая запись в SPDR автоматически очищают флаг. */
    { volatile uint8_t s = SPSR; (void)s; volatile uint8_t d = SPDR; (void)d; }
    while (len--) {
        SPDR = *data++;
        while (!(SPSR & (1 << SPIF)));
    }
#else
    while (len--) SPI.transfer(*data++);
#endif
}
#endif   /* ST7735_PLAT_ARDUINO */

/* ---------- таблица инициализации ST7735R ---------- */
#define ST7735_D  0x80    /* флаг "есть задержка" в старшем бите поля nargs */

static const uint8_t ST7735_PGM st7735_init[] = {
  ST7735_SWRESET, ST7735_D | 0, 150,
  ST7735_SLPOUT,  ST7735_D | 0, 120,
  ST7735_FRMCTR1, 3, 0x01, 0x2C, 0x2D,
  ST7735_FRMCTR2, 3, 0x01, 0x2C, 0x2D,
  ST7735_FRMCTR3, 6, 0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D,
  ST7735_INVCTR,  1, 0x07,
  ST7735_PWCTR1,  3, 0xA2, 0x02, 0x84,
  ST7735_PWCTR2,  1, 0xC5,
  ST7735_PWCTR3,  2, 0x0A, 0x00,
  ST7735_PWCTR4,  2, 0x8A, 0x2A,
  ST7735_PWCTR5,  2, 0x8A, 0xEE,
  ST7735_VMCTR1,  1, 0x0E,
  ST7735_INVOFF,  0,
  ST7735_MADCTL,  1, ST7735_MADCTL_MX | ST7735_MADCTL_MY,
  ST7735_COLMOD,  ST7735_D | 1, 0x05, 10,
  ST7735_GMCTRP1, 16, 0x02,0x1C,0x07,0x12,0x37,0x32,0x29,0x2D,
                      0x29,0x25,0x2B,0x39,0x00,0x01,0x03,0x10,
  ST7735_GMCTRN1, 16, 0x03,0x1D,0x07,0x06,0x2E,0x2C,0x29,0x2D,
                      0x2E,0x2E,0x37,0x3F,0x00,0x00,0x02,0x10,
  ST7735_NORON,   ST7735_D | 0, 10,
  ST7735_DISPOFF, 0,
  0                                   /* конец (NOP не используется) */
};

/* =====================================================================
    Пины / сессия
   ===================================================================== */
void ST7735::csLow(void) {
#if defined(ST7735_PLAT_ARDUINO)
  if (_cs >= 0) digitalWrite(_cs, LOW);
#else
  ST7735_CS_LOW();
#endif
}
void ST7735::csHigh(void) {
#if defined(ST7735_PLAT_ARDUINO)
  if (_cs >= 0) digitalWrite(_cs, HIGH);
#else
  ST7735_CS_HIGH();
#endif
}
void ST7735::dcLow(void) {
#if defined(ST7735_PLAT_ARDUINO)
  if (_dc >= 0) digitalWrite(_dc, LOW);
#else
  ST7735_DC_LOW();
#endif
}
void ST7735::dcHigh(void) {
#if defined(ST7735_PLAT_ARDUINO)
  if (_dc >= 0) digitalWrite(_dc, HIGH);
#else
  ST7735_DC_HIGH();
#endif
}
void ST7735::rstLow(void) {
#if defined(ST7735_PLAT_ARDUINO)
  if (_rst >= 0) digitalWrite(_rst, LOW);
#else
  ST7735_RST_LOW();
#endif
}
void ST7735::rstHigh(void) {
#if defined(ST7735_PLAT_ARDUINO)
  if (_rst >= 0) digitalWrite(_rst, HIGH);
#else
  ST7735_RST_HIGH();
#endif
}
void ST7735::startWrite(void) {
  if (_inWrite++ == 0) {
#if defined(ST7735_PLAT_ARDUINO)
    SPI.beginTransaction(SPISettings(_spiHz, MSBFIRST, SPI_MODE0));
#endif
    csLow();
  }
}
void ST7735::endWrite(void) {
  if (_inWrite == 0) return;
  if (--_inWrite == 0) {
    csHigh();
#if defined(ST7735_PLAT_ARDUINO)
    SPI.endTransaction();
#endif
  }
}
/* =====================================================================
    Инициализация
   ===================================================================== */
ST7735::ST7735(int8_t cs, int8_t dc, int8_t rst, uint8_t tab)
  : _cs(cs), _dc(dc), _rst(rst), _rotation(0),
    _width(128), _height(160),
    _colstart(0), _rowstart(0), _xoff(0), _yoff(0),
    _spiHz(8000000UL),
    _madctlRgb(ST7735_MADCTL_COLOR), _invertInit(false),
    _font(&font5x7), _textScale(1),
    _textFg(COLOR565_WHITE), _textBg(COLOR565_BLACK),
    _textBgValid(false), _wrap(true), _cx(0), _cy(0),
    _termOn(false), _inWrite(0)
#if ST7735_ENABLE_TERMINAL
    , _termX(0), _termY(0), _termW(0), _termH(0),
    _termRow(0), _termCol(0), _termBg(COLOR565_BLACK),
    _termTfa(0), _termVsa(0), _termVspOff(0), _termHwLineH(0),
    _termHwReq(false), _termHwOn(false)
#endif
{
  memset(_charBuf, 0, sizeof(_charBuf));
#if ST7735_ENABLE_TERMINAL
  memset(_termBuf, 0, sizeof(_termBuf));
#endif

  _wx0 = _wx1 = _wy0 = _wy1 = -1;          /* ← В ТЕЛЕ, не в init-листе */

  switch (tab) {
    case ST7735R_GREEN:
    case ST7735R_GREEN3:
      _colstart = 2; _rowstart = 1;
      break;
    default:
      _colstart = 0; _rowstart = 0;
      break;
  }
  if (tab == ST7735R_GREEN3) _invertInit = true;
  _madctlRgb = (tab == ST7735R_BLACK) ? ST7735_MADCTL_RGB : ST7735_MADCTL_BGR;
}

void ST7735::begin(uint32_t spiHz) {
#if defined(ST7735_PLAT_ARDUINO)
  _spiHz = spiHz;
  if (_cs  >= 0) pinMode(_cs,  OUTPUT);
  if (_dc  >= 0) pinMode(_dc,  OUTPUT);
  if (_rst >= 0) pinMode(_rst, OUTPUT);
  SPI.begin();
#else
  (void)spiHz;    /* частота задаётся предделителем в CubeMX */
#endif
  csHigh(); dcHigh();           /* линии в известном состоянии */
  hwReset();
  runInitTable();
  if (_invertInit) invertDisplay(true);
  setRotation(0);
  fillScreen(COLOR565_BLACK);   /* очистка при ВЫКЛЮЧЕННОМ дисплее */
  setDisplayOn(true);           /* DISPON — включаем после очистки, без вспышки */
  ST7735_DELAY_MS(25);
  _cx = 0; _cy = 0;
}

void ST7735::hwReset(void) {
    csHigh();                         /* защита от мусора по SPI */
    rstHigh();  ST7735_DELAY_MS(5);
    rstLow();   ST7735_DELAY_MS(5);
    rstHigh();  ST7735_DELAY_MS(120);
}
/* вся таблица одной SPI-транзакцией (паузы при низком CS допустимы) */
void ST7735::runInitTable(void) {
  _wx0 = _wx1 = _wy0 = _wy1 = -1;      /* сброс кэша окна — SWRESET сбросил контроллер */
  const uint8_t *p = st7735_init;
  uint8_t cmd, buf[16];
  startWrite();
  while ((cmd = ST7735_RD8(p++)) != 0) {
    uint8_t n = ST7735_RD8(p++), argc = n & 0x1F;
    writeCommand(cmd);
    for (uint8_t j = 0; j < argc; j++) buf[j] = ST7735_RD8(p++);
    if (argc) st7735_spi_write(buf, argc);
    if (n & ST7735_D) ST7735_DELAY_MS(ST7735_RD8(p++));
  }
  endWrite();
}

/* =====================================================================
    Низкий уровень
   ===================================================================== */
/* ВНИМАНИЕ: прямая отправка CASET/RASET через writeCommand() обходит кэш окна.
 * После неё вызовите setAddrWindow() с другими координатами или setRotation(). */
void ST7735::writeCommand(uint8_t c) {
  dcLow();
  st7735_spi_write(&c, 1);
  dcHigh();
}
void ST7735::writeData8(uint8_t d) {
  st7735_spi_write(&d, 1);
}
void ST7735::writeData16(uint16_t d) {
  uint8_t b[2] = { (uint8_t)(d >> 8), (uint8_t)d };
  st7735_spi_write(b, 2);
}

/* полные 16-битные адреса (задел под большие контроллеры) */
void ST7735::setAddrWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  x0 += _xoff; y0 += _yoff; x1 += _xoff; y1 += _yoff;
  uint8_t b[4];
  if (x0 != _wx0 || x1 != _wx1) {
    writeCommand(ST7735_CASET);
    b[0] = (uint8_t)(x0 >> 8); b[1] = (uint8_t)x0;
    b[2] = (uint8_t)(x1 >> 8); b[3] = (uint8_t)x1;
    st7735_spi_write(b, 4);
    _wx0 = x0; _wx1 = x1;
  }
  if (y0 != _wy0 || y1 != _wy1) {
    writeCommand(ST7735_RASET);
    b[0] = (uint8_t)(y0 >> 8); b[1] = (uint8_t)y0;
    b[2] = (uint8_t)(y1 >> 8); b[3] = (uint8_t)y1;
    st7735_spi_write(b, 4);
    _wy0 = y0; _wy1 = y1;
  }
  writeCommand(ST7735_RAMWR);
}
/* OPT: буфер чанка заполняется ОДИН раз и переиспользуется для всех чанков —
 * раньше каждый чанк по 64 px перезаполнялся заново (лишняя работа на каждые
 * 128 байт; на заливке экрана — 320 лишних заполнений).
 * Безопасно: st7735_spi_write источник не модифицирует (AVR transfer читает
 * байт, ESP32 writeBytes копирует внутрь, HAL_SPI_Transmit — только на выход). */
#if defined(ST7735_PLAT_ARDUINO) && defined(__AVR__) && defined(SPDR) && defined(SPIF)
void ST7735::writeColor(uint16_t color, uint32_t count) {
    if (!count) return;
    const uint8_t hi = (uint8_t)(color >> 8), lo = (uint8_t)color;
    /* очистка возможного висящего SPIF: чтение SPSR + чтение SPDR */
    { volatile uint8_t s = SPSR; (void)s; volatile uint8_t d = SPDR; (void)d; }
    while (count--) {
        SPDR = hi; while (!(SPSR & (1 << SPIF)));
        SPDR = lo; while (!(SPSR & (1 << SPIF)));
    }
}
#else
void ST7735::writeColor(uint16_t color, uint32_t count) {
    if (!count) return;
    uint8_t buf[ST7735_CHUNK * 2], hi = (uint8_t)(color >> 8), lo = (uint8_t)color;
    uint16_t full = (count > ST7735_CHUNK) ? ST7735_CHUNK : (uint16_t)count;
    for (uint16_t i = 0; i < full; i++) {
        buf[2 * i]     = hi;
        buf[2 * i + 1] = lo;
    }
    while (count >= ST7735_CHUNK) {
        st7735_spi_write(buf, ST7735_CHUNK * 2);
        count -= ST7735_CHUNK;
    }
    if (count) st7735_spi_write(buf, (uint32_t)count * 2);
}
#endif

void ST7735::writePixels(const uint16_t *colors, uint32_t count) {
  uint8_t buf[ST7735_CHUNK * 2];
  while (count) {
    uint32_t n = (count > ST7735_CHUNK) ? ST7735_CHUNK : count;
    for (uint32_t i = 0; i < n; i++) {
      uint16_t c = *colors++;
      buf[2 * i]     = (uint8_t)(c >> 8); /* старший байт вперёд */
      buf[2 * i + 1] = (uint8_t)c;
    }
    st7735_spi_write(buf, n * 2);
    count -= n;
  }
}

/* Как writePixels, но байты УЖЕ готовы (RGB565 MSB-first, 2 Б/пиксель).
 * count — пиксели. Отправляет блоками ST7735_CHUNK пикселей, как writePixels
 * (ограничение размера HAL_SPI_Transmit на STM32 — uint16_t). */
void ST7735::writePixelsBytes(const uint8_t *bytes, uint32_t count) {
  while (count) {
    uint32_t n = (count > ST7735_CHUNK) ? ST7735_CHUNK : count;
    st7735_spi_write(bytes, n * 2);
    bytes += n * 2;
    count -= n;
  }
}

/* =====================================================================
    Ориентация
   ===================================================================== */
void ST7735::setRotation(uint8_t r) {
  _rotation = r & 3;
  _wx0 = _wx1 = _wy0 = _wy1 = -1;
  uint8_t madctl = _madctlRgb;
  switch (_rotation) {
    case 0:  madctl |= ST7735_MADCTL_MX | ST7735_MADCTL_MY; /* портрет 0°   */
      _xoff = _colstart; _yoff = _rowstart; _width = 128; _height = 160; break;
    case 1:  madctl |= ST7735_MADCTL_MY | ST7735_MADCTL_MV; /* альбом 90°   */
      _xoff = _rowstart; _yoff = _colstart; _width = 160; _height = 128; break;
    case 2:  /* портрет 180°: инверсия rotation 0 — биты MX|MY сняты */
      _xoff = _colstart; _yoff = _rowstart; _width = 128; _height = 160; break;
    case 3:  madctl |= ST7735_MADCTL_MX | ST7735_MADCTL_MV; /* альбом 270°  */
      _xoff = _rowstart; _yoff = _colstart; _width = 160; _height = 128; break;
  }
  startWrite();
  writeCommand(ST7735_MADCTL);
  writeData8(madctl);
  endWrite();
#if ST7735_ENABLE_TERMINAL
  /* Поворот меняет геометрию GRAM: область прокрутки более не валидна.
   * Возвращаем контроллеру полный экран без сдвига и уходим в софт-скролл
   * (сам терминал поворот и так не пересчитывает — см. st7735.h). */
  if (_termHwOn) {
    _termHwOn = false;
    setScrollArea(0, (int16_t)_height, 0);
    scrollTo(0);
  }
#endif
}

void ST7735::setOffsets(uint8_t colstart, uint8_t rowstart) {
  _colstart = colstart;
  _rowstart = rowstart;
  setRotation(_rotation);      /* пересчитать _xoff/_yoff */
}

/* =====================================================================
    Примитивы (клиппинг через int32 — без переполнений на 16-битном int AVR)
   ===================================================================== */
void ST7735::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if ((uint16_t)x >= (uint16_t)_width || (uint16_t)y >= (uint16_t)_height) return;
  
  startWrite();
  setAddrWindow(x, y, x, y);
  uint8_t b[2] = { (uint8_t)(color >> 8), (uint8_t)color };
  st7735_spi_write(b, 2);
  endWrite();
}

void ST7735::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  int32_t X0 = x, Y0 = y, X1 = (int32_t)x + w - 1, Y1 = (int32_t)y + h - 1;
  if (X1 < X0 || Y1 < Y0) return;
  if (X0 < 0) X0 = 0;
  if (Y0 < 0) Y0 = 0;
  if (X1 >= (int32_t)_width)  X1 = (int32_t)_width  - 1;
  if (Y1 >= (int32_t)_height) Y1 = (int32_t)_height - 1;
  if (X0 > X1 || Y0 > Y1) return;

  startWrite();
  setAddrWindow((int16_t)X0, (int16_t)Y0, (int16_t)X1, (int16_t)Y1);
  writeColor(color, (uint32_t)(X1 - X0 + 1) * (uint32_t)(Y1 - Y0 + 1));
  endWrite();
}

void ST7735::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
  if (w < 1) return;
  int32_t x1 = (int32_t)x + w - 1;      /* int32: на AVR int 16-битный */
  if (x < 0) x = 0;
  if (x1 >= (int32_t)_width) x1 = (int32_t)_width - 1;
  if (x > x1 || (uint16_t)y >= (uint16_t)_height) return;

  startWrite();
  setAddrWindow(x, y, (int16_t)x1, y);
  /* OPT: дублированный код заливки убран — окно уже выставлено, дальше
   * общий путь writeColor (минус ~60 Б flash, плюс скорость) */
  writeColor(color, (uint32_t)(x1 - x + 1));
  endWrite();
}

void ST7735::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
  if (x < 0 || x >= (int16_t)_width) return;
  int32_t Y0 = y, Y1 = (int32_t)y + h - 1;
  if (Y1 < Y0) return;
  if (Y0 < 0) Y0 = 0;
  if (Y1 >= (int32_t)_height) Y1 = (int32_t)_height - 1;
  if (Y0 > Y1) return;
  
  startWrite();
  setAddrWindow(x, (int16_t)Y0, x, (int16_t)Y1);
  writeColor(color, (uint32_t)(Y1 - Y0 + 1));
  endWrite();
}

/* одна транзакция на весь прямоугольник */
void ST7735::drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    startWrite();
    drawFastHLine(x, y, w, color);                                       /* верх */
    if (h > 1) drawFastHLine(x, (int16_t)((int32_t)y + h - 1), w, color); /* низ */
    if (h > 2) {
        drawFastVLine(x, (int16_t)(y + 1), (int16_t)(h - 2), color);     /* лево */
        if (w > 1)
            drawFastVLine((int16_t)((int32_t)x + w - 1), (int16_t)(y + 1),
                          (int16_t)(h - 2), color);                      /* право */
    }
    endWrite();
}

void ST7735::drawProgressBar(int16_t x, int16_t y, int16_t w, int16_t h,
                             int32_t value, int32_t prev, int32_t vmax,
                             uint16_t fg, uint16_t bg, uint16_t border) {
  if (w < 3 || h < 3 || vmax <= 0) return;
  const int32_t iw = w - 2;
  if (value < 0) value = 0; else if (value > vmax) value = vmax;
  
  /* uint32_t scaling — на AVR int64_t тянет libgcc.
   *
   * ВАЖНО: предел сдвига — iw*256, не 0x00FFFFFF! Старая версия
   * `while (vm > 0x00FFFFFFUL)` позволяла v ≤ 16M после сдвига, и тогда
   * iw*v ≤ 32767 * 16M ≈ 5.5×10^11 — переполняло uint32_t (max 4.3×10^9).
   * На больших дисплеях (iw=478) это давало мусор вместо длины полосы.
   *
   * С новым пределом vm ≤ iw*256: v ≤ iw*256, iw*v ≤ iw*iw*256.
   * Для iw=478: 478²·256 ≈ 5.8×10^7 — влезает в uint32_t с запасом.
   * Точность: v масштабируется до 256*x — теряем максимум 8 бит, что
   * при значениях vmax ≤ 16M означает ошибку ≤ 1/256 ≈ 0.4%. Достаточно
   * для progress bar. */
  uint32_t vm = (uint32_t)vmax, v = (uint32_t)value;
  uint32_t p  = (prev > 0) ? (uint32_t)((prev > vmax) ? vmax : prev) : 0;
  while (vm > (uint32_t)iw * 256UL) { vm >>= 1; v >>= 1; p >>= 1; }
  const int32_t nf = (int32_t)((uint32_t)iw * v / vm);
  const int32_t pf = (int32_t)((uint32_t)iw * p / vm);

  startWrite();

  if (prev < 0) {
    drawRect(x, y, w, h, border);
    fillRect(x + 1, y + 1, (int16_t)nf, h - 2, fg);
    fillRect((int16_t)(x + 1 + nf), y + 1, (int16_t)(iw - nf), h - 2, bg);
  } else {
    if (nf > pf)      fillRect((int16_t)(x + 1 + pf), y + 1, (int16_t)(nf - pf), h - 2, fg);
    else if (nf < pf) fillRect((int16_t)(x + 1 + nf), y + 1, (int16_t)(pf - nf), h - 2, bg);
  }

  // 2. Отрисовка 10% рисок строго ПОСЛЕ fillRect (перерисовываем всегда)
  for (uint8_t i = 1; i < 10; i++) {
    int16_t tx = x + 1 + (int16_t)((int32_t)iw * i / 10);
    
    // Динамический выбор цвета для контраста:
    // если риска поверх закрашенной полосы (fg) — рисуем цветом bg (или border),
    // если поверх пустого фона (bg) — рисуем цветом border (или fg).
    uint16_t tickColor = (tx < x + 1 + (int16_t)nf) ? bg : border;
    
    drawFastVLine(tx, y + 1, h - 2, tickColor);
  }

  endWrite();
}
/* =====================================================================
    Скруглённые прямоугольники
   ===================================================================== */

/* drawCircleHelper — приватный, рисует дугу для одного угла.
 * cornername bitmask: 1=TL, 2=TR, 4=BR, 8=BL.
 * Вызывать внутри startWrite/endWrite. */
void ST7735::drawCircleHelper(int16_t x0, int16_t y0, int16_t r,
                              uint8_t cornername, uint16_t color) {
    if (r <= 0) return;
    int16_t f     = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x     = 0;
    int16_t y     = r;

    while (x <= y) {
        if (cornername & 0x04) {                 /* bottom-right */
            drawPixel(x0 + x, y0 + y, color);
            drawPixel(x0 + y, y0 + x, color);
        }
        if (cornername & 0x02) {                 /* top-right */
            drawPixel(x0 + x, y0 - y, color);
            drawPixel(x0 + y, y0 - x, color);
        }
        if (cornername & 0x08) {                 /* bottom-left */
            drawPixel(x0 - y, y0 + x, color);
            drawPixel(x0 - x, y0 + y, color);
        }
        if (cornername & 0x01) {                 /* top-left */
            drawPixel(x0 - y, y0 - x, color);
            drawPixel(x0 - x, y0 - y, color);
        }
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
    }
}

/* fillCircleHelper — приватный, заливает дугу для одного/двух углов.
 * corners bitmask: 1=правая сторона, 2=левая сторона.
 * delta — доп. высота v-линий (для fillRoundRect = h - 2*r - 1).
 * Вызывать внутри startWrite/endWrite. */
void ST7735::fillCircleHelper(int16_t x0, int16_t y0, int16_t r,
                              uint8_t corners, int16_t delta, uint16_t color) {
    if (r <= 0) return;
    int16_t f     = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x     = 0;
    int16_t y     = r;
    int16_t px = x, py = y;

    delta++;                                      /* избежать +1 в цикле */

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        if (x < (y + 1)) {                        /* основная дуга */
            if (corners & 1)
                drawFastVLine(x0 + x, y0 - y, 2 * y + delta, color);
            if (corners & 2)
                drawFastVLine(x0 - x, y0 - y, 2 * y + delta, color);
        }
        if (y != py) {                            /* «хвост» предыдущей итерации */
            if (corners & 1)
                drawFastVLine(x0 + py, y0 - px, 2 * px + delta, color);
            if (corners & 2)
                drawFastVLine(x0 - py, y0 - px, 2 * px + delta, color);
            py = y;
        }
        px = x;
    }
}
static uint16_t st7735_isqrt(uint32_t v) {
  uint32_t r = 0, b = 1UL << 30;
  while (b > v) b >>= 2;
  while (b) {
    if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1;
    b >>= 2;
  }
  return (uint16_t)r;
}

/* полуширина эллипса на расстоянии dy от центра */
static int32_t st7735_ellipseHalfW(int32_t rx, int32_t ry, int32_t dy) {
  int32_t ry2 = ry * ry;
  int32_t num = rx * rx * (ry2 - dy * dy);       /* rx,ry <= 180 -> < 2^31 */
  return st7735_isqrt((uint32_t)((num + ry2 / 2) / ry2));
}

void ST7735::fillEllipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint16_t color) {
  if (rx < 0 || ry < 0) return;
  if (rx > 180) rx = 180;
  if (ry > 180) ry = 180;
  if (ry == 0) { drawFastHLine(x0 - rx, y0, 2 * rx + 1, color); return; }
  if (rx == 0) { drawFastVLine(x0, y0 - ry, 2 * ry + 1, color); return; }
  startWrite();
  for (int32_t dy = 0; dy <= ry; dy++) {
    int32_t xo = st7735_ellipseHalfW(rx, ry, dy);
    drawFastHLine((int16_t)(x0 - xo), (int16_t)(y0 + dy), (int16_t)(2 * xo + 1), color);
    if (dy) drawFastHLine((int16_t)(x0 - xo), (int16_t)(y0 - dy), (int16_t)(2 * xo + 1), color);
  }
  endWrite();
}

/* Контур эллипса: на каждой строке рисуем горизонтальные сегменты,
 * покрывающие пиксели периметра, которые НОВЫЕ относительно следующей
 * строки (dy+1). Сегмент — это разность xo(dy) \ xo(dy+1): от xi=xo(dy+1)+1
 * до xo(dy) на правой стороне и симметрично на левой.
 *   • Если xo(dy) == xo(dy+1) (плоский участок) — xi=xn+1>xo, клемпится к xo,
 *     получается сегмент длиной 1 (самая правая/левая точка строки).
 *   • Если xo(dy) - xo(dy+1) = k (крутой участок) — сегмент длиной k пикселей,
 *     закрывающий всю «ступеньку» одним drawFastHLine.
 * 8-связность гарантирована: переход dy→dy+1 — диагональный шаг ±1 по x.
 * Дубликатов нет: каждая точка периметра рисуется ровно один раз.
 *
 * Полюс (dy == ry) выделен в отдельную ветку в начале итерации:
 * там xo=0, обычная segment-логика дала бы 4 вызова drawFastHLine на 2 пикселя
 * (пары дублирующихся x0+0 == x0-0). Двумя drawPixel — то же самое,
 * но без 4 setAddrWindow и без 4 SPI-посылок. */
void ST7735::drawEllipse(int16_t x0, int16_t y0, int16_t rx, int16_t ry, uint16_t color) {
  if (rx < 0 || ry < 0) return;
  if (rx > 180) rx = 180;
  if (ry > 180) ry = 180;
  if (ry == 0) { drawFastHLine(x0 - rx, y0, 2 * rx + 1, color); return; }
  if (rx == 0) { drawFastVLine(x0, y0 - ry, 2 * ry + 1, color); return; }
  startWrite();
  /* Оптимизация: на итерации dy нужны xo = halfW(dy) и xn = halfW(dy+1).
   * На следующей итерации dy+1 xo_new = halfW(dy+1) = xn (кэшируем).
   * Раньше halfW вызывался дважды на каждой итерации — теперь только
   * один раз для xn, всего ry+1 вызовов вместо 2*(ry+1). */
  int32_t xn = st7735_ellipseHalfW(rx, ry, 0);   /* = xo для dy=0 */
  for (int32_t dy = 0; dy <= ry; dy++) {
    if (dy == ry) {
      /* полюс: 2 пикселя (верх + низ), без segment-логики */
      drawPixel(x0, y0 + ry, color);
      drawPixel(x0, y0 - ry, color);
      break;
    }
    int32_t xo = xn;                              /* кэшированное с прошлой итерации */
    xn = st7735_ellipseHalfW(rx, ry, dy + 1);     /* один вызов вместо двух */
    int32_t xi = xn + 1; if (xi > xo) xi = xo;           /* внутренняя граница сегмента */
    int16_t len = (int16_t)(xo - xi + 1);
    drawFastHLine((int16_t)(x0 + xi), (int16_t)(y0 + dy), len, color);
    drawFastHLine((int16_t)(x0 - xo), (int16_t)(y0 + dy), len, color);
    if (dy) {
      drawFastHLine((int16_t)(x0 + xi), (int16_t)(y0 - dy), len, color);
      drawFastHLine((int16_t)(x0 - xo), (int16_t)(y0 - dy), len, color);
    }
  }
  endWrite();
}

/* Контур скруглённого прямоугольника. Одна SPI-сессия. */
void ST7735::drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                           int16_t r, uint16_t color) {
    if (w <= 0 || h <= 0 || r < 0) return;
    int16_t max_r = ((w < h) ? w : h) / 2;
    if (r > max_r) r = max_r;

    startWrite();
    drawFastHLine(x + r, y,             w - 2 * r, color);         /* верх   */
    drawFastHLine(x + r, y + h - 1,     w - 2 * r, color);         /* низ   */
    drawFastVLine(x,             y + r, h - 2 * r, color);         /* лево  */
    drawFastVLine(x + w - 1,     y + r, h - 2 * r, color);         /* право */
    drawCircleHelper(x + r,         y + r,         r, 1, color);  /* TL */
    drawCircleHelper(x + w - r - 1, y + r,         r, 2, color);  /* TR */
    drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4, color);  /* BR */
    drawCircleHelper(x + r,         y + h - r - 1, r, 8, color);  /* BL */
    endWrite();
}

/* Заливка скруглённого прямоугольника. Одна SPI-сессия. */
void ST7735::fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                           int16_t r, uint16_t color) {
    if (w <= 0 || h <= 0 || r < 0) return;
    int16_t max_r = ((w < h) ? w : h) / 2;
    if (r > max_r) r = max_r;

    startWrite();
    /* центральный прямоугольник — без углов */
    fillRect(x + r, y, w - 2 * r, h, color);
    /* правая и левая полосы + верхний и нижний правый/левый углы
     * (delta = h - 2*r - 1 продлевает v-линии на полную высоту) */
    fillCircleHelper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, color);  /* TR+BR */
    fillCircleHelper(x + r,         y + r, r, 2, h - 2 * r - 1, color);  /* TL+BL */
    endWrite();
}
#if ST7735_ENABLE_ARC
#include <math.h>
void ST7735::fillArc(int16_t x0, int16_t y0, int16_t rOut, int16_t rIn,
                     int16_t startDeg, int16_t endDeg, uint16_t color) {
  if (rOut <= 0 || rIn < 0 || rIn >= rOut) return;
  if (rOut > 200) rOut = 200;
  int32_t sweep = (int32_t)endDeg - startDeg;
  if (sweep <= 0) return;
  const bool full = (sweep >= 360);
  const float k = 0.01745329252f;
  const int32_t ss = (int32_t)(sinf(startDeg * k) * 1024), cs = (int32_t)(cosf(startDeg * k) * 1024);
  const int32_t se = (int32_t)(sinf(endDeg   * k) * 1024), ce = (int32_t)(cosf(endDeg   * k) * 1024);
  const int32_t ro2 = (int32_t)rOut * rOut, ri2 = (int32_t)rIn * rIn;

  int32_t xmin = -rOut, xmax = rOut;
  if (x0 + xmin < 0) xmin = -x0;
  if (x0 + xmax >= (int32_t)_width) xmax = (int32_t)_width - 1 - x0;
  if (xmin > xmax) return;

  startWrite();
  for (int32_t dy = -rOut; dy <= rOut; dy++) {
    int32_t y = (int32_t)y0 + dy;
    if (y < 0 || y >= (int32_t)_height) continue;
    const int32_t up = -dy;
    int32_t run = 0, runStart = 0;
    for (int32_t dx = xmin; dx <= xmax + 1; dx++) {
      bool in = false;
      if (dx <= xmax) {
        int32_t d2 = dx * dx + dy * dy;
        if (d2 <= ro2 && d2 >= ri2) {
          if (full) in = true;
          else {
            int32_t c0 = cs * dx - ss * up;       /* >=0: по часовой от начала */
            int32_t c1 = ce * dx - se * up;       /* <=0: против часовой от конца */
            in = (sweep <= 180) ? (c0 >= 0 && c1 <= 0) : (c0 >= 0 || c1 <= 0);
          }
        }
      }
      if (in) { if (!run) runStart = dx; run++; }
      else if (run) {
        drawFastHLine((int16_t)(x0 + runStart), (int16_t)y, (int16_t)run, color);
        run = 0;
      }
    }
  }
  endWrite();
}
#endif

/* =====================================================================
    Треугольники
   ===================================================================== */

/* Контур треугольника — три отрезка в одной SPI-сессии. */
void ST7735::drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                          int16_t x2, int16_t y2, uint16_t color) {
    startWrite();
    drawLine(x0, y0, x1, y1, color);
    drawLine(x1, y1, x2, y2, color);
    drawLine(x2, y2, x0, y0, color);
    endWrite();
}

/* Заливка треугольника — scanline-растеризация.
 * Сортируем вершины по Y, затем заливаем верхнюю и нижнюю половины
 * горизонтальными линиями. Математика в int32_t — безопасно на AVR. */
void ST7735::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                          int16_t x2, int16_t y2, uint16_t color) {
    /* Сортировка по Y: y0 <= y1 <= y2 */
    if (y0 > y1) { int16_t t=x0; x0=x1; x1=t; t=y0; y0=y1; y1=t; }
    if (y1 > y2) { int16_t t=x1; x1=x2; x2=t; t=y1; y1=y2; y2=t; }
    if (y0 > y1) { int16_t t=x0; x0=x1; x1=t; t=y0; y0=y1; y1=t; }

    startWrite();

    if (y0 == y2) {
        /* Вырожденный — все вершины на одной строке */
        int16_t a = x0, b = x0;
        if (x1 < a) a = x1; else if (x1 > b) b = x1;
        if (x2 < a) a = x2; else if (x2 > b) b = x2;
        drawFastHLine(a, y0, b - a + 1, color);
        endWrite();
        return;
    }

    int16_t dx01 = x1 - x0, dy01 = y1 - y0;
    int16_t dx02 = x2 - x0, dy02 = y2 - y0;
    int16_t dx12 = x2 - x1, dy12 = y2 - y1;
    int32_t sa = 0, sb = 0;

    /* Верхняя половина: от y0 до y1-1 (или y1, если y1==y2 — плоский низ) */
    int16_t last = (y1 == y2) ? y1 : y1 - 1;
    int16_t y;

    for (y = y0; y <= last; y++) {
        /* Сначала вычисляем в int32_t (x0 + sa/dy01), потом cast в int16_t.
         * Раньше было `x0 + (int16_t)(sa/dy01)` — cast ДО сложения мог
         * переполниться при больших x0 или sa/dy01. */
        int16_t a = (int16_t)((int32_t)x0 + sa / dy01);
        int16_t b = (int16_t)((int32_t)x0 + sb / dy02);
        sa += dx01;
        sb += dx02;
        if (a > b) { int16_t t = a; a = b; b = t; }
        drawFastHLine(a, y, b - a + 1, color);
    }

    /* Нижняя половина: от y до y2 */
    sa = (int32_t)dx12 * (y - y1);
    sb = (int32_t)dx02 * (y - y0);
    for (; y <= y2; y++) {
        int16_t a = (int16_t)((int32_t)x1 + sa / dy12);
        int16_t b = (int16_t)((int32_t)x0 + sb / dy02);
        sa += dx12;
        sb += dx02;
        if (a > b) { int16_t t = a; a = b; b = t; }
        drawFastHLine(a, y, b - a + 1, color);
    }

    endWrite();
}

void ST7735::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {

    /* ранний выход: обе точки по одну сторону за экраном */
    if ((x0 < 0 && x1 < 0) || (y0 < 0 && y1 < 0) ||
        (x0 >= (int16_t)_width  && x1 >= (int16_t)_width) ||
        (y0 >= (int16_t)_height && y1 >= (int16_t)_height)) return;

    /* H/V линии: клиппим к [0,_width-1]×[0,_height-1] через int32_t,
     * только после этого преобразуем длину в int16_t для drawFast*.
     * Без этого при x0=-32768, x1=32767 длина = 65536 → cast в int16_t
     * даёт 0, и drawFastHLine рисует мусор. */
    if (x0 == x1) {
        if (x0 < 0 || x0 >= (int16_t)_width) return;
        int32_t yA = y0, yB = y1;
        if (yA > yB) { int32_t t = yA; yA = yB; yB = t; }
        if (yA < 0) yA = 0;
        if (yB >= (int32_t)_height) yB = (int32_t)_height - 1;
        if (yA > yB) return;
        drawFastVLine(x0, (int16_t)yA, (int16_t)(yB - yA + 1), color);
        return;
    }
    if (y0 == y1) {
        if (y0 < 0 || y0 >= (int16_t)_height) return;
        int32_t xA = x0, xB = x1;
        if (xA > xB) { int32_t t = xA; xA = xB; xB = t; }
        if (xA < 0) xA = 0;
        if (xB >= (int32_t)_width) xB = (int32_t)_width - 1;
        if (xA > xB) return;
        drawFastHLine((int16_t)xA, y0, (int16_t)(xB - xA + 1), color);
        return;
    }
    /* диагональ: Bresenham */
    int32_t dx = (x1 > x0) ? (int32_t)x1 - x0 : (int32_t)x0 - x1;
    int32_t dy = (y1 > y0) ? (int32_t)y1 - y0 : (int32_t)y0 - y1;
    int32_t sx = (x0 < x1) ? 1 : -1, sy = (y0 < y1) ? 1 : -1;
    int32_t x = x0, y = y0;
    bool steep = dy > dx;
    int32_t major = steep ? dy : dx, minor = steep ? dx : dy;
    int32_t err = major / 2;
    int32_t runStart = steep ? y : x;
    int32_t runMinor = steep ? x : y;
    int32_t step     = steep ? sy : sx;
    int32_t pos      = runStart;

    startWrite();                                       /* вся диагональ в одной сессии */
    for (int32_t i = 0; i <= major; i++) {
        bool last = (i == major);
        bool change = false;
        int32_t nextMinor = runMinor;
        if (!last) {
            err -= minor;
            if (err < 0) {
                err += major;
                nextMinor = runMinor + (steep ? sx : sy);
                change = true;
            }
        }
        if (change || last) {
            int32_t a = (runStart < pos) ? runStart : pos;
            int32_t len = ((runStart < pos) ? pos - runStart : runStart - pos) + 1;
            if (steep) drawFastVLine((int16_t)runMinor, (int16_t)a, (int16_t)len, color);
            else       drawFastHLine((int16_t)a, (int16_t)runMinor, (int16_t)len, color);
            runMinor = nextMinor;
            runStart = pos + step;
        }
        pos += step;
    }
    endWrite();
}

/* =====================================================================
    ИЗОБРАЖЕНИЯ (НОВОЕ)
   ===================================================================== */

/* Байт-свап + отправка потока RGB565 чанками по 64 px.
 * Источник читается ST7735_RD16: на AVR изображения обязаны лежать в PROGMEM
 * (как глифы шрифтов); на ESP32/STM32 — любой const-массив (флеш = память). */
void ST7735::writePixelsSwap(const uint16_t *px, uint32_t count) {
  uint8_t buf[ST7735_CHUNK * 2];
  while (count) {
    uint32_t n = (count > ST7735_CHUNK) ? ST7735_CHUNK : count;
    for (uint32_t i = 0; i < n; i++) {
      uint16_t c = ST7735_RD16(px + i);
      buf[2 * i]     = (uint8_t)(c >> 8);   /* старший байт вперёд */
      buf[2 * i + 1] = (uint8_t)c;
    }
    st7735_spi_write(buf, n * 2);
    px += n;
    count -= n;
  }
}

/* Общая часть клиппинга — возвращает false, если изображение полностью вне экрана.
 * X0..Y1 — итоговые координаты окна, skipL/skipT — смещения в источнике. */
static bool st7735_clipImage(int16_t x, int16_t y, int16_t w, int16_t h,
                             uint16_t scrW, uint16_t scrH,
                             int32_t &X0, int32_t &Y0, int32_t &X1, int32_t &Y1,
                             uint32_t &skipL, uint32_t &skipT) {
  X0 = x; Y0 = y;
  X1 = (int32_t)x + w - 1; Y1 = (int32_t)y + h - 1;
  skipL = 0; skipT = 0;
  if (X0 < 0) { skipL = (uint32_t)(-X0); X0 = 0; }
  if (Y0 < 0) { skipT = (uint32_t)(-Y0); Y0 = 0; }
  if (X1 >= (int32_t)scrW) X1 = (int32_t)scrW - 1;
  if (Y1 >= (int32_t)scrH) Y1 = (int32_t)scrH - 1;
  return !(X0 > X1 || Y0 > Y1);
}

/* Блит RGB565-изображения с клиппингом: одно окно, одна SPI-транзакция,
 * конвертация байтов чанками по 64 px. Полностью видимое изображение
 * уходит одним непрерывным потоком (без окно-настроечных издержек). */
void ST7735::drawImage(int16_t x, int16_t y, int16_t w, int16_t h,
                       const uint16_t *px) {
  if (!px || w < 1 || h < 1) return;
  int32_t X0, Y0, X1, Y1;
  uint32_t skipL, skipT;
  if (!st7735_clipImage(x, y, w, h, _width, _height, X0, Y0, X1, Y1, skipL, skipT)) return;

  const uint16_t *src = px + skipT * (uint32_t)w + skipL;
  uint32_t sendPix = (uint32_t)(X1 - X0 + 1);
  uint32_t rows    = (uint32_t)(Y1 - Y0 + 1);

  startWrite();
  setAddrWindow((int16_t)X0, (int16_t)Y0, (int16_t)X1, (int16_t)Y1);
  if (skipL == 0 && sendPix == (uint32_t)w) {
    writePixelsSwap(src, sendPix * rows);
  } else {
    for (uint32_t r = 0; r < rows; r++) { writePixelsSwap(src, sendPix); src += w; }
  }
  endWrite();
}

void ST7735::drawImageRAM(int16_t x, int16_t y, int16_t w, int16_t h,
                          const uint16_t *px) {
  if (!px || w < 1 || h < 1) return;
  int32_t X0, Y0, X1, Y1;
  uint32_t skipL, skipT;
  if (!st7735_clipImage(x, y, w, h, _width, _height, X0, Y0, X1, Y1, skipL, skipT)) return;

  const uint16_t *src = px + skipT * (uint32_t)w + skipL;
  uint32_t sendPix = (uint32_t)(X1 - X0 + 1);
  uint32_t rows    = (uint32_t)(Y1 - Y0 + 1);

  startWrite();
  setAddrWindow((int16_t)X0, (int16_t)Y0, (int16_t)X1, (int16_t)Y1);
  if (skipL == 0 && sendPix == (uint32_t)w) {
    writePixels(src, sendPix * rows);
  } else {
    for (uint32_t r = 0; r < rows; r++) { writePixels(src, sendPix); src += w; }
  }
  endWrite();
}

/* 1-битный bitmap, формат Adafruit: строка = целое число байт, старший бит —
 * левый пиксель. На AVR данные — в PROGMEM.
 * drawBitmap(..., fg)     — прозрачный: рисуются только установленные биты;
 * drawBitmap(..., fg, bg) — непрозрачный: заливается вся область.
 * 1 бит/пиксель: хранение в 16 раз компактнее RGB565. */
void ST7735::drawBitmap(int16_t x, int16_t y, int16_t w, int16_t h,
                        const uint8_t *bmp, uint16_t fg) {
  if (!bmp || w < 1 || h < 1) return;
  int32_t X0, Y0, X1, Y1;
  uint32_t skipL, skipT;
  if (!st7735_clipImage(x, y, w, h, _width, _height, X0, Y0, X1, Y1, skipL, skipT)) return;

  const uint32_t bpr = ((uint32_t)w + 7) >> 3;
  const uint8_t *src = bmp + skipT * bpr;

  startWrite();
  for (int32_t sy = Y0; sy <= Y1; sy++, src += bpr) {
    int32_t run = 0;
    for (int32_t sx = X0; sx <= X1 + 1; sx++) {
      bool on = false;
      if (sx <= X1) {
        uint32_t col = (uint32_t)(sx - x);
        on = (ST7735_RD8(&src[col >> 3]) & (uint8_t)(0x80u >> (col & 7))) != 0;
      }
      if (on) run++;
      else if (run) {
        setAddrWindow((int16_t)(sx - run), (int16_t)sy, (int16_t)(sx - 1), (int16_t)sy);
        writeColor(fg, (uint32_t)run);
        run = 0;
      }
    }
  }
  endWrite();
}

void ST7735::drawBitmapRAM(int16_t x, int16_t y, int16_t w, int16_t h,
                           const uint8_t *bmp, uint16_t fg) {
  if (!bmp || w < 1 || h < 1) return;
  int32_t X0, Y0, X1, Y1;
  uint32_t skipL, skipT;
  if (!st7735_clipImage(x, y, w, h, _width, _height, X0, Y0, X1, Y1, skipL, skipT)) return;

  const uint32_t bpr = ((uint32_t)w + 7) >> 3;
  const uint8_t *src = bmp + skipT * bpr;

  startWrite();
  for (int32_t sy = Y0; sy <= Y1; sy++, src += bpr) {
    int32_t run = 0;
    for (int32_t sx = X0; sx <= X1 + 1; sx++) {
      bool on = false;
      if (sx <= X1) {
        uint32_t col = (uint32_t)(sx - x);
        on = (src[col >> 3] & (uint8_t)(0x80u >> (col & 7))) != 0;  /* прямое чтение */
      }
      if (on) run++;
      else if (run) {
        setAddrWindow((int16_t)(sx - run), (int16_t)sy, (int16_t)(sx - 1), (int16_t)sy);
        writeColor(fg, (uint32_t)run);
        run = 0;
      }
    }
  }
  endWrite();
}
void ST7735::drawBitmap(int16_t x, int16_t y, int16_t w, int16_t h,
                        const uint8_t *bmp, uint16_t fg, uint16_t bg) {
  if (!bmp || w < 1 || h < 1) return;
  int32_t X0, Y0, X1, Y1;
  uint32_t skipL, skipT;
  if (!st7735_clipImage(x, y, w, h, _width, _height, X0, Y0, X1, Y1, skipL, skipT)) return;

  const uint32_t bpr = ((uint32_t)w + 7) >> 3;
  const uint8_t *src = bmp + skipT * bpr;
  const uint8_t fghi = (uint8_t)(fg >> 8), fglo = (uint8_t)fg;
  const uint8_t bghi = (uint8_t)(bg >> 8), bglo = (uint8_t)bg;
  uint8_t buf[ST7735_CHUNK * 2];

  startWrite();
  setAddrWindow((int16_t)X0, (int16_t)Y0, (int16_t)X1, (int16_t)Y1);
  for (int32_t sy = Y0; sy <= Y1; sy++, src += bpr) {
    for (int32_t sx = X0; sx <= X1; ) {
      uint32_t seg = (uint32_t)(X1 - sx + 1);
      if (seg > ST7735_CHUNK) seg = ST7735_CHUNK;
      for (uint32_t i = 0; i < seg; i++) {
        uint32_t col = (uint32_t)(sx + (int32_t)i - x);
        bool on = (ST7735_RD8(&src[col >> 3]) & (uint8_t)(0x80u >> (col & 7))) != 0;
        buf[2 * i]     = on ? fghi : bghi;
        buf[2 * i + 1] = on ? fglo : bglo;
      }
      st7735_spi_write(buf, seg * 2);
      sx += (int32_t)seg;
    }
  }
  endWrite();
}

void ST7735::drawBitmapRAM(int16_t x, int16_t y, int16_t w, int16_t h,
                           const uint8_t *bmp, uint16_t fg, uint16_t bg) {
  if (!bmp || w < 1 || h < 1) return;
  int32_t X0, Y0, X1, Y1;
  uint32_t skipL, skipT;
  if (!st7735_clipImage(x, y, w, h, _width, _height, X0, Y0, X1, Y1, skipL, skipT)) return;

  const uint32_t bpr = ((uint32_t)w + 7) >> 3;
  const uint8_t *src = bmp + skipT * bpr;
  const uint8_t fghi = (uint8_t)(fg >> 8), fglo = (uint8_t)fg;
  const uint8_t bghi = (uint8_t)(bg >> 8), bglo = (uint8_t)bg;
  uint8_t buf[ST7735_CHUNK * 2];

  startWrite();
  setAddrWindow((int16_t)X0, (int16_t)Y0, (int16_t)X1, (int16_t)Y1);
  for (int32_t sy = Y0; sy <= Y1; sy++, src += bpr) {
    for (int32_t sx = X0; sx <= X1; ) {
      uint32_t seg = (uint32_t)(X1 - sx + 1);
      if (seg > ST7735_CHUNK) seg = ST7735_CHUNK;
      for (uint32_t i = 0; i < seg; i++) {
        uint32_t col = (uint32_t)(sx + (int32_t)i - x);
        bool on = (src[col >> 3] & (uint8_t)(0x80u >> (col & 7))) != 0;  /* прямое чтение */
        buf[2 * i]     = on ? fghi : bghi;
        buf[2 * i + 1] = on ? fglo : bglo;
      }
      st7735_spi_write(buf, seg * 2);
      sx += (int32_t)seg;
    }
  }
  endWrite();
}
void ST7735::drawBitmapScaled(int16_t x, int16_t y, int16_t w, int16_t h,
                              const uint8_t *bmp, uint16_t fg, uint8_t scale,
                              bool invert) {
  if (!bmp || w < 1 || h < 1) return;
  if (scale == 0) scale = 1;
  const uint32_t bpr = ((uint32_t)w + 7) >> 3;
  startWrite();
  for (int32_t row = 0; row < h; row++) {
    int32_t sy = (int32_t)y + row * scale, ey = sy + scale - 1;
    if (ey < 0 || sy >= (int32_t)_height) continue;
    if (sy < 0) sy = 0;
    if (ey >= (int32_t)_height) ey = (int32_t)_height - 1;
    const uint8_t *src = bmp + (uint32_t)row * bpr;
    int32_t run = 0, runStart = 0;
    for (int32_t col = 0; col <= w; col++) {
      bool on = false;
      if (col < w) {
        on = (ST7735_RD8(&src[col >> 3]) & (uint8_t)(0x80u >> (col & 7))) != 0;
        if (invert) on = !on;
      }
      if (on) { if (!run) runStart = col; run++; }
      else if (run) {
        int32_t sx = (int32_t)x + runStart * scale;
        int32_t ex = (int32_t)x + (runStart + run) * scale - 1;
        run = 0;
        if (ex < 0 || sx >= (int32_t)_width) continue;
        if (sx < 0) sx = 0;
        if (ex >= (int32_t)_width) ex = (int32_t)_width - 1;
        setAddrWindow((int16_t)sx, (int16_t)sy, (int16_t)ex, (int16_t)ey);
        writeColor(fg, (uint32_t)(ex - sx + 1) * (uint32_t)(ey - sy + 1));
      }
    }
  }
  endWrite();
}

void ST7735::drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    if (r <= 0) return;
    int16_t x = 0, y = r;
    int32_t d = 1 - r;

    startWrite();
    while (x <= y) {
        drawPixel(x0 + x, y0 + y, color);
        drawPixel(x0 + x, y0 - y, color);
        if (x) {
            drawPixel(x0 - x, y0 + y, color);
            drawPixel(x0 - x, y0 - y, color);
        }
        if (x != y) {
            drawPixel(x0 + y, y0 + x, color);
            drawPixel(x0 - y, y0 + x, color);
            if (x) {
                drawPixel(x0 + y, y0 - x, color);
                drawPixel(x0 - y, y0 - x, color);
            }
        }
        if (d < 0) d += 2 * (int32_t)x + 3;
        else { d += 2 * ((int32_t)x - y) + 5; y--; }
        x++;
    }
    endWrite();
}

void ST7735::fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    if (r <= 0) return;
    if (r > 16000) r = 16000;          /* защита 2*r+1 от int16_t overflow */
    /* целиком вне экрана — выходим, не крутясь */
    if ((int32_t)x0 + r < 0 || (int32_t)x0 - r >= (int32_t)_width ||
        (int32_t)y0 + r < 0 || (int32_t)y0 - r >= (int32_t)_height) return;

    int16_t x = 0, y = r;
    int32_t d = 1 - r;

    startWrite();                                       /* одна сессия на весь круг */
    while (x <= y) {
        const bool dec = (d >= 0);
        drawFastVLines2(x0 + x, x0 - x, y0 - y, 2 * y + 1, color);
        if (dec && x != y)                              /* пара 2 только при декр. y */
            drawFastVLines2(x0 + y, x0 - y, y0 - x, 2 * x + 1, color);
        if (dec) { d += 2 * ((int32_t)x - y) + 5; y--; }
        else     { d += 2 * (int32_t)x + 3; }
        x++;
    }
    endWrite();
}

void ST7735::setScrollArea(int16_t topFixed, int16_t scrollHeight, int16_t botFixed) {
    /* ST7735 VSCRDEF (0x33): три 16-битных поля — верхняя фикс. область,
     * область прокрутки, нижняя фикс. область. Контроллер корректно работает,
     * если их сумма <= высоты дисплея: «лишние» строки снизу просто не
     * участвуют в прокрутке. Сумма > высоты даёт мусор при следующем
     * scrollTo, поэтому такие параметры отклоняем ДО отправки команд.
     *
     * Порядок проверок:
     *   1) clamp отрицательных фиксированных в 0;
     *   2) early-exit, если только фикс. уже превысили высоту;
     *   3) автовычисление scrollHeight, если <=0;
     *   4) финальный reject, если сумма всё ещё > высоты.
     * Все вычисления в int32_t — на AVR int 16-битный, сумма может
     * переполнить, если считать в int. */
    if (topFixed < 0) topFixed = 0;
    if (botFixed < 0) botFixed = 0;

    const int32_t screenHeight = (int32_t)_height;
    if ((int32_t)topFixed + botFixed > screenHeight) return;
    if (scrollHeight <= 0)
        scrollHeight = (int16_t)(screenHeight - topFixed - botFixed);

    /* Не допускаем область прокрутки за пределы физической высоты экрана. */
    if (scrollHeight <= 0 ||
        (int32_t)topFixed + scrollHeight + botFixed > screenHeight) return;

    startWrite();
    writeCommand(ST7735_VSCRDEF);
    writeData16((uint16_t)topFixed);
    writeData16((uint16_t)scrollHeight);
    writeData16((uint16_t)botFixed);
    endWrite();
}

void ST7735::scrollTo(int16_t offset) {
    startWrite();
    writeCommand(ST7735_VSCSAD);
    writeData16((uint16_t)offset);
    endWrite();
}
/* =====================================================================
    Управление
   ===================================================================== */
void ST7735::invertDisplay(bool on) {
  startWrite();
  writeCommand(on ? ST7735_INVON : ST7735_INVOFF);
  endWrite();
}
void ST7735::setDisplayOn(bool on) {
  startWrite();
  writeCommand(on ? ST7735_DISPON : ST7735_DISPOFF);
  endWrite();
}
void ST7735::sleep(void) {
  setDisplayOn(false);
  startWrite(); writeCommand(ST7735_SLPIN); endWrite();
  ST7735_DELAY_MS(120);            /* по даташиту: 120 мс до следующей команды */
}
void ST7735::wake(void) {
  startWrite(); writeCommand(ST7735_SLPOUT); endWrite();
  ST7735_DELAY_MS(120);
  setDisplayOn(true);
}

/* =====================================================================
    ТЕКСТ
   ===================================================================== */
/* Unicode -> CP1251: для renderCharGI и drawChar.
   Внутренние пути уже подают CP1251 из st7735_next_char.
   добавлена та же таблица символов, что и в st7735_next_char —
   раньше drawChar(0x2116) печатал '?', хотя print("№") печатал знак
   номера (крипто-несогласованность двух путей конвертации). */
static uint16_t st7735_to_cp1251(uint16_t code) {
  /* code < 256 — пропускаем как есть. Раньше тут стояла проверка
   *   if (code == 0xA8 || code == 0xB8) return '?';
   * которая ломала прямые вызовы drawChar(0xA8) (CP1251 Ё) и двойную
   * конверсию из next_char (UTF-8 'Ё' → 0xA8 → '?' вместо 0xA8).
   *
   * Семантика: эта функция КОНВЕРТИРУЕТ Unicode codepoint → CP1251 код.
   * Если code < 256 — это либо CP1251-код (от next_char, уже конвертированный),
   * либо Latin-1/Unicode codepoint. В CP1251 0xA8=Ё, 0xB8=ё, 0xC0-0xFF —
   * кириллица. Пропускаем без изменений — glyph_index сам разберётся,
   * есть ли в шрифте глиф для этого кода.
   *
   * Если на входе Unicode ¨ (U+00A8) — он попадёт сюда как 0xA8, и glyph_index
   * найдёт глиф Ё (потому что шрифт хранит CP1251). Это намеренное поведение:
   * наш драйвер оперирует CP1251-кодами, drawChar(0xA8) = Ё. */
  if (code < 256) return code;
  if (code >= 0x410 && code <= 0x44F) return code - 0x410 + 0xC0;
  if (code == 0x401) return 0xA8;                 /* Ё */
  if (code == 0x451) return 0xB8;                 /* ё */
  if (code == 0x40E) return 0xA1;                 /* Ў */
  if (code == 0x45E) return 0xA2;                 /* ў */
  if (code == 0x0406) return 0xB2;                /* І */
  if (code == 0x0456) return 0xB3;                /* і */
  if (code == 0x2116) return 0xB9;                /* №  */
  if (code == 0x2013 || code == 0x2014 || code == 0x2212) return '-';
  if (code == 0x2018 || code == 0x2019) return '\'';
  if (code == 0x201C || code == 0x201D) return '"';
  if (code == 0x2026) return '.';
  return '?';
}

/* CP1251-код -> индекс глифа. 0xFFFF = «глифа нет».
   Диапазон 2 лежит в данных сразу за диапазоном 1. */
static uint16_t st7735_glyph_index(const st7735_font_t *f, uint16_t c) {
  if (c >= f->first && c < f->first + f->count) return c - f->first;
  if (f->first2 && c >= f->first2 && c < f->first2 + f->count2)
    return f->count + (c - f->first2);
  return 0xFFFF;
}

/* Один бит глифа: col 0..w-1 слева направо, row 0..h-1 сверху вниз */
static uint8_t st7735_font_bit(const st7735_font_t *f, const uint8_t *g,
                               uint8_t col, uint8_t row) {
  if (f->flags & 1) {                 /* row-major (7x11): байт = строка, бит0 = левый столбец */
    if (col >= f->w) return 0;
    return (uint8_t)(ST7735_RD8(&g[(uint16_t)row * f->bpc + (col >> 3)]) >> (col & 7)) & 1;
  }                                    /* column-major (5x7): байт = колонка, бит0 = верхняя строка */
  if (row >= f->h) return 0;
  return (uint8_t)(ST7735_RD8(&g[(uint16_t)col * f->bpc + (row >> 3)]) >> (row & 7)) & 1;
}

/* Один и тот же масштаб нужен и для рисования, и для шага print()/терминала. */
static uint8_t st7735_scale(const st7735_font_t *f, uint8_t s) {
  if (s == 0) s = 1;
  uint16_t maxs = ST7735_TEXT_MAX_PX / ((uint16_t)f->w + 1);
  if (maxs == 0) maxs = 1;
  return (s > maxs) ? (uint8_t)maxs : s;
}

void ST7735::renderCharGI(int16_t x, int16_t y, uint16_t gi,
                          const st7735_font_t *f,
                          uint16_t fg, uint16_t bg, uint8_t scale, bool opaque)
{
  if (!f || !f->data || f->w == 0 || f->h == 0) return;
  if ((uint16_t)f->w + 1 > ST7735_TEXT_MAX_PX) return;
  if (f->h > ST7735_TEXT_MAX_H) return;

  const uint8_t *g = f->data +
    (uint32_t)gi * ((f->flags & 1) ? (uint32_t)f->h * f->bpc
                                   : (uint32_t)f->w * f->bpc);

  uint8_t sc = st7735_scale(f, scale);
  int32_t gw = (int32_t)f->w * sc;
  int32_t cw = (int32_t)(f->w + 1) * sc;
  int32_t gh = (int32_t)f->h * sc;

  int32_t vis = opaque ? cw : gw;
  if ((int32_t)x + vis <= 0 || (int32_t)y + gh <= 0 ||
      x >= (int16_t)_width || y >= (int16_t)_height) return;

  uint16_t rowBits[ST7735_TEXT_MAX_H];
  const bool fast = (f->w <= 16);
  if (fast) {
    if (f->flags & 1) {
      for (uint8_t row = 0; row < f->h; row++) {
        uint16_t bits = 0;
        for (uint8_t b = 0; b < f->bpc && (uint16_t)(b * 8) < f->w; b++)
          bits |= (uint16_t)((uint16_t)ST7735_RD8(&g[(uint16_t)row * f->bpc + b]) << (b * 8));
        rowBits[row] = bits;
      }
    } else {
      for (uint8_t row = 0; row < f->h; row++) rowBits[row] = 0;
      for (uint8_t col = 0; col < f->w; col++) {
        for (uint8_t b = 0; b < f->bpc; b++) {
          uint8_t v = ST7735_RD8(&g[(uint16_t)col * f->bpc + b]);
          if (!v) continue;
          for (uint8_t r = 0; r < 8 && (uint16_t)(b * 8) + r < f->h; r++)
            if (v & (uint8_t)(1u << r))
              rowBits[b * 8 + r] |= (uint16_t)(1u << col);
        }
      }
    }
  }

  const uint8_t fghi = (uint8_t)(fg >> 8), fglo = (uint8_t)fg;
  const uint8_t bghi = (uint8_t)(bg >> 8), bglo = (uint8_t)bg;

  startWrite();                                   /* ← безусловно */

  if (opaque) {
    int32_t wx0 = (x < 0) ? 0 : x;
    int32_t wy0 = (y < 0) ? 0 : y;
    int32_t wx1 = ((int32_t)x + cw - 1 >= (int32_t)_width)  ? (int32_t)_width  - 1 : (int32_t)x + cw - 1;
    int32_t wy1 = ((int32_t)y + gh - 1 >= (int32_t)_height) ? (int32_t)_height - 1 : (int32_t)y + gh - 1;
    int32_t  clipL   = wx0 - x;
    uint16_t sendPix = (uint16_t)(wx1 - wx0 + 1);

    setAddrWindow((int16_t)wx0, (int16_t)wy0, (int16_t)wx1, (int16_t)wy1);

    uint16_t prevBits = 0; bool havePrev = false;

    for (uint8_t row = 0; row < f->h; row++) {
      int32_t syBase = (int32_t)y + (int32_t)row * sc;
      int32_t syTop  = (syBase < wy0) ? wy0 : syBase;
      int32_t syBot  = (syBase + sc - 1 > wy1) ? wy1 : syBase + sc - 1;
      if (syTop > syBot) continue;

      const uint16_t bits = fast ? rowBits[row] : 0;
      if (!(fast && havePrev && bits == prevBits)) {
        uint16_t n = 0;
        for (uint8_t col = 0; col <= f->w; col++) {
          bool on = (col < f->w) &&
                    (fast ? ((bits >> col) & 1) != 0
                          : st7735_font_bit(f, g, col, row) != 0);
          uint8_t hi = on ? fghi : bghi, lo = on ? fglo : bglo;
          for (uint8_t k = 0; k < sc; k++) {
            _charBuf[n++] = hi;
            _charBuf[n++] = lo;
          }
        }
        prevBits = bits; havePrev = fast;
      }
      int32_t nRep = syBot - syTop + 1;
          while (nRep-- > 0) {
            st7735_spi_write(_charBuf + clipL * 2, (uint32_t)sendPix * 2);
        }
    }
  } else {
    for (uint8_t row = 0; row < f->h; row++) {
      int32_t syTop = (int32_t)y + (int32_t)row * sc;
      int32_t syBot = syTop + sc - 1;
      if (syTop < 0) syTop = 0;
      if (syBot >= (int32_t)_height) syBot = (int32_t)_height - 1;
      if (syTop > syBot) continue;

      const uint16_t bits = fast ? rowBits[row] : 0;
      int32_t run = 0, runStart = 0;
      for (uint8_t col = 0; col <= f->w; col++) {
        bool on = (col < f->w) &&
                  (fast ? ((bits >> col) & 1) != 0
                        : st7735_font_bit(f, g, col, row) != 0);
        if (on) { if (!run) runStart = col; run++; }
        else if (run) {
          int32_t sx = (int32_t)x + runStart * sc;
          int32_t ex = (int32_t)x + (int32_t)(runStart + run) * sc - 1;
          run = 0;
          if (ex < 0 || sx >= (int32_t)_width) continue;
          if (sx < 0) sx = 0;
          if (ex >= (int32_t)_width) ex = (int32_t)_width - 1;
          setAddrWindow((int16_t)sx, (int16_t)syTop, (int16_t)ex, (int16_t)syBot);
          writeColor(fg, (uint32_t)(ex - sx + 1) * (uint32_t)(syBot - syTop + 1));
        }
      }
    }
  }

  endWrite();                                     /* ← безусловно */
}

/* UTF-8 -> CP1251. Кириллица, Ё/ё, №, символы 0xA0-0xFF конвертируются.
   Одиночный байт 0x80-0xFF без валидного UTF-8-продолжения передаётся
   как есть — это совместимость со старыми CP1251-строками (не «теряется»!).
   Строка, оборванная на lead-байте, и последовательности вне таблицы -> '?'. */
static uint16_t st7735_next_char(const char *&p) {
  uint8_t c = (uint8_t) *p++;
  if (c < 0x80) return c;

  if ((c & 0xE0) == 0xC0) {                       /* 2 байта */
    uint8_t c2 = (uint8_t) *p;
    if (c2 && (c2 & 0xC0) == 0x80) {
      ++p;
      uint16_t u = ((uint16_t)(c & 0x1F) << 6) | (c2 & 0x3F);
      if (u >= 0x0410 && u <= 0x044F) return u - 0x0410 + 0xC0; /* А..я */
      if (u == 0x0401) return 0xA8;                             /* Ё   */
      if (u == 0x0451) return 0xB8;                              /* ё   */
      if (u == 0x040E) return 0xA1;                              /* Ў   */
      if (u == 0x045E) return 0xA2;                              /* ў   */
      if (u == 0x0406) return 0xB2;                              /* І   */
      if (u == 0x0456) return 0xB3;                              /* і   */
      /* Latin-1 диапазон 0xA0..0xBF: в font5x7.h теперь корректные глифы для
       * ВСЕХ кодов этого диапазона (¡¢£¤¥¦§...©...«¬­®¯°±²³´µ¶·¸¹º»¼½¾¿).
       * Возвращаем как есть — попадёт в правильную позицию фонта.
       * Исключения: 0xA8 и 0xB8 — в CP1251 это Ё и ё, не ¨ и ¸. Если
       * пользователь вводит Latin-1 ¨ (U+00A8) или ¸ (U+00B8) — возвращаем
       * '?', иначе ¨ отрендерился бы как Ё, что некорректно. */
      if (u >= 0xA0 && u <= 0xBF) {
        if (u == 0xA8 || u == 0xB8) return '?';
        return u;
      }
      /* × (U+00D7) и ÷ (U+00F7): в CP1251 коды 0xD7/0xF7 заняты
       * кириллицей (Ч/ч). В шрифте × и ÷ как отдельные глифы отсутствуют —
       * возвращаем '?'. Раньше здесь был pass-through, но он отрисовывал
       * × как Ч, что неверно. */
      if (u == 0xD7 || u == 0xF7) return '?';
      return '?';                            /* всё прочее -> '?' */
    }
    return c;                                  /* CP1251-совместимость */
  }

  if ((c & 0xF0) == 0xE0) {
    uint8_t c2 = (uint8_t)p[0];
    if (c2 && (c2 & 0xC0) == 0x80) {
      uint8_t c3 = (uint8_t)p[1];
      if (c3 && (c3 & 0xC0) == 0x80) {
        p += 2;
        uint16_t u = ((uint16_t)(c & 0x0F) << 12) |
                     ((uint16_t)(c2 & 0x3F) << 6) | (c3 & 0x3F);
        if (u == 0x2116) return 0xB9;                            /* №    */
        if (u == 0x2013 || u == 0x2014 || u == 0x2212) return '-'; /* – — − */
        if (u == 0x2018 || u == 0x2019) return '\'';
        if (u == 0x201C || u == 0x201D) return '"';
        if (u == 0x2026) return '.';
        return '?';
      }
    }
    return c;
  }

  if ((c & 0xF8) == 0xF0) {                      /* 4 байта: эмодзи и др. */
    /* чтение p[0..2] было безусловным — строка, кончающаяся байтом
     * 0xF0..0xF7 (в CP1251 это 'р'..'ч', например "Привет"), читала
     * p[1]/p[2] ЗА NUL-терминатор (OOB-чтение, ASAN: global-buffer-overflow).
     * Чтения разнесены каскадом: следующий байт читается только если
     * предыдущий был валидным байтом продолжения UTF-8. */
     uint8_t c2 = (uint8_t)p[0];
     if (c2 && (c2 & 0xC0) == 0x80) {
       uint8_t c3 = (uint8_t)p[1];
       if (c3 && (c3 & 0xC0) == 0x80) {
         uint8_t c4 = (uint8_t)p[2];
         if (c4 && (c4 & 0xC0) == 0x80) {
           p += 3;                                 /* съесть целиком */
           return '?';
        }
       }
     }
    return c;
  }

  return c;
}

/* смена шрифта/масштаба при живом терминале пересобирает его сетку.
   Валидация: w ∈ (0..MAX_PX], h ∈ (0..MAX_H] — закрывает и UB в pitch-арифметике,
   и выход за _charBuf в renderCharGI, и потенциальное переполнение
   (h-1)*scale в int16_t. При отказе _font остаётся прежним.
   для row-major (flags&1) bpc — это байты НА СТРОКУ: требуется
   bpc >= (w+7)/8. Раньше проверялась только col-major-ветка, и шрифт
   w=16,bpc=1,row-major принимался, а затем читал СЛЕДУЮЩУЮ строку глифа
   вместо старших столбцов (мусор на экране). */
void ST7735::setFont(const st7735_font_t *f) {
  if (f && f->data
      && f->w > 0 && (uint16_t)f->w + 1 <= ST7735_TEXT_MAX_PX   /* ячейка (w+1)*sc <= 64 px */
      && f->h > 0 && f->h <= ST7735_TEXT_MAX_H
      && ((f->flags & 1)
           ? f->bpc >= (uint8_t)((f->w + 7) / 8)   /* row-major: байт на строку */
           : f->bpc >= (uint8_t)((f->h + 7) / 8))) {
    _font = f;
#if ST7735_ENABLE_TERMINAL
    if (_termOn) setTerminal(_termX, _termY, _termW, _termH);
#endif
  }
}

void ST7735::setTextScale(uint8_t s) {
  _textScale = s ? s : 1;
#if ST7735_ENABLE_TERMINAL
  if (_termOn) setTerminal(_termX, _termY, _termW, _termH);
#endif
}
void ST7735::setTextColor(uint16_t fg)              {
  _textFg = fg;
  _textBgValid = false;
}
void ST7735::setTextColor(uint16_t fg, uint16_t bg) {
  _textFg = fg;
  _textBg = bg;
  _textBgValid = true;
}
void ST7735::drawFastVLines2(int16_t xa, int16_t xb, int16_t y, int16_t h,
                             uint16_t color) {
    if (h <= 0) return;

    /* Клиппинг по Y в int32_t — безопасно от int16_t overflow.
     * Сначала обрезаем верх, потом низ — порядок не критичен, т.к.
     * вычисления в int32_t и финальная проверка y0 > y1 ловит все случаи. */
    int32_t y0 = y;
    int32_t y1 = (int32_t)y + h - 1;
    if (y0 < 0) y0 = 0;
    if (y1 >= (int32_t)_height) y1 = (int32_t)_height - 1;
    if (y0 > y1) return;

    int16_t h_clipped = (int16_t)(y1 - y0 + 1);
    int16_t y_end     = (int16_t)(y0 + h_clipped - 1);   // вычислено в int32_t

    startWrite();
    /* Готовим буфер цвета ОДИН РАЗ — writeColor не делает это заново.
     * buf[128] = 64 пикселя RGB565, переиспользуется между линиями. 
     * buf[ST7735_CHUNK*2] = ST7735_CHUNK пикселей RGB565, переиспользуется между линиями. */
    uint8_t  buf[ST7735_CHUNK * 2];
    uint8_t  hi = (uint8_t)(color >> 8), lo = (uint8_t)color;
    uint16_t full = (h_clipped > ST7735_CHUNK) ? ST7735_CHUNK : h_clipped;
    for (uint16_t i = 0; i < full; i++) {
        buf[2 * i]     = hi;
        buf[2 * i + 1] = lo;
    }

    /* Если xa == xb — рисуем один раз (центральная ось в fillCircle при x=0
     * или при y=0). Иначе — обе. Масштабируется под N линий простым
     * расширением массива. */
    int16_t xs[2] = { xa, xb };
    uint8_t nLines = (xa == xb) ? 1 : 2;

    for (uint8_t i = 0; i < nLines; i++) {
        int16_t x = xs[i];
        if (x >= 0 && x < (int16_t)_width) {
            setAddrWindow(x, (int16_t)y0, x, y_end);
            /* Отправка подготовленного буфера — без повторного заполнения */
            uint32_t remaining = h_clipped;
            while (remaining >= ST7735_CHUNK) {
                st7735_spi_write(buf, ST7735_CHUNK * 2);
                remaining -= ST7735_CHUNK;
            }
            if (remaining) st7735_spi_write(buf, remaining * 2);
        }
    }
  endWrite();
}

uint16_t ST7735::drawChar(int16_t x, int16_t y, uint16_t code,
                          uint16_t fg, uint16_t bg, uint8_t scale)
{
  const st7735_font_t *f = _font;
  if (!f || f->w == 0 || (uint16_t)f->w + 1 > ST7735_TEXT_MAX_PX
      || f->h == 0 || f->h > ST7735_TEXT_MAX_H) return 0;

  scale = st7735_scale(f, scale);
  uint16_t pitch = (uint16_t)(f->w + 1) * scale;

  uint16_t c  = st7735_to_cp1251(code);          /* ← один раз конвертируем */
  uint16_t gi = st7735_glyph_index(f, c);        /* ← один раз ищем индекс */

  if (gi != 0xFFFF) {
    renderCharGI(x, y, gi, f, fg, bg, scale, true);   /* ← f — указатель, не uint16_t */
  } else {
    fillRect(x, y, (int16_t)pitch, (int16_t)(f->h * scale), bg);
  }
  return pitch;
}

uint16_t ST7735::drawChar(int16_t x, int16_t y, uint16_t code,
                          uint16_t fg, uint8_t scale)
{
  const st7735_font_t *f = _font;
  if (!f || f->w == 0 || (uint16_t)f->w + 1 > ST7735_TEXT_MAX_PX
      || f->h == 0 || f->h > ST7735_TEXT_MAX_H) return 0;

  scale = st7735_scale(f, scale);
  uint16_t pitch = (uint16_t)(f->w + 1) * scale;

  uint16_t c  = st7735_to_cp1251(code);
  uint16_t gi = st7735_glyph_index(f, c);

  if (gi != 0xFFFF)
    renderCharGI(x, y, gi, f, fg, 0, scale, false);   /* ← bg=0, opaque=false */
  return pitch;
}

void ST7735::drawString(const char *s, int16_t x, int16_t y,
                        uint16_t fg, uint16_t bg, uint8_t scale) {
    const st7735_font_t *f = _font;
    if (!f || !s) return;
    scale = st7735_scale(f, scale);
    uint16_t pitch = (uint16_t)(f->w + 1) * scale;
    uint16_t lineH = (uint16_t)(f->h + 1) * scale;
    uint16_t gh    = (uint16_t)f->h * scale;
    int16_t cx = x, cy = y;
    const char *p = s;

    startWrite();                                      /* ← ОДНА сессия */
    while (*p) {
        uint16_t ch = st7735_next_char(p);
        if (ch == '\n') { cx = x; cy += (int16_t)lineH; continue; }
        if (ch == '\r') { cx = x; continue; }
        /* Клиппинг по ширине самого глифа, не pitch: последний символ у
         * правого края экрана (например, при cx = _width - f->w*scale)
         * физически влезает, и его нужно напечатать целиком, даже если
         * его хвостовой зазор pitch выходит за _width. Без этого символа
         * просто не будет — он молча отбрасывался или переносился. */
        if (cx + (int16_t)f->w * scale > (int16_t)_width) {
            if (!_wrap) break;
            cx = x; cy += (int16_t)lineH;
        }
        if (cy + (int16_t)gh > (int16_t)_height) break;
        drawChar(cx, cy, ch, fg, bg, scale);
        cx += (int16_t)pitch;
    }
    endWrite();
}

void ST7735::printBase(long n, uint8_t base) {
    if (base == 0) base = 10;
    if (base < 2)  base = 2;
    if (base > 36) base = 36;

    /* sizeof(long)*8 бит + знак + NUL — достаточно для base==2 (худший случай) */
    char b[sizeof(long) * 8 + 2];
    char *p = b + sizeof(b) - 1;
    *p = 0;

    unsigned long u;
    bool negative = false;
    if (n < 0 && base == 10) {
        negative = true;
        u = (unsigned long)(-(n + 1)) + 1UL;   /* безопасное negation LONG_MIN */
    } else {
        u = (unsigned long)n;                  /* base != 10 — интерпретируем как битовый паттерн */
    }
    do {
        unsigned char d = (unsigned char)(u % base);
        *--p = (char)((d < 10) ? ('0' + d) : ('A' + d - 10));
        u /= base;
    } while (u);
    if (negative) *--p = '-';
    print(p);
}

/* Беззнаковый вариант для print(unsigned ..., base). Раньше путь
 * print(unsigned long n, uint8_t base) -> printBase((long)n, base) silently
 * преобразовывал unsigned в long: на AVR (long 32-битный) для значений
 * с установленным старшим битом (напр. 0x80000000UL) cast давал -2147483648,
 * а printBase, хотя и восстанавливал битовый паттерн через
 * u = (unsigned long)n, делал это окольным путём. Новый printUnsignedBase
 * работает с unsigned long напрямую — никаких преобразований знака,
 * никакой ветки negative, никакого «безопасного negation LONG_MIN». */
void ST7735::printUnsignedBase(unsigned long u, uint8_t base) {
    if (base == 0) base = 10;
    if (base < 2)  base = 2;
    if (base > 36) base = 36;

    char b[sizeof(unsigned long) * 8 + 1];   /* без знака — на 1 байт короче */
    char *p = b + sizeof(b) - 1;
    *p = 0;
    do {
        unsigned char d = (unsigned char)(u % base);
        *--p = (char)((d < 10) ? ('0' + d) : ('A' + d - 10));
        u /= base;
    } while (u);
    print(p);
}

void ST7735::print(const char *s) {
    if (!s) return;
#if ST7735_ENABLE_TERMINAL
    if (_termOn) {
        startWrite();                                  /* ← батчим терминал тоже */
        const char *p = s;
        while (*p) termPutChar(st7735_next_char(p));
        endWrite();
        return;
    }
#endif
    const st7735_font_t *f = _font;
    if (!f) return;
    uint8_t  sc    = st7735_scale(f, _textScale);
    uint16_t pitch = (uint16_t)(f->w + 1) * sc;
    uint16_t lineH = (uint16_t)(f->h + 1) * sc;
    const char *p = s;

    startWrite();                                      /* ← ОДНА сессия на всю строку */
    while (*p) {
        uint16_t ch = st7735_next_char(p);
        if (ch == '\n') {
            _cx = 0;
            _cy += (int16_t)lineH;
            if (_cy + (int16_t)lineH > (int16_t)_height) _cy = 0;
            continue;
        }
        if (ch == '\r') { _cx = 0; continue; }
        /* Клиппинг по ширине глифа, не pitch — см. комментарий в drawString().
         * Последний символ у правого края экрана печатается целиком. */
        if (_cx + (int16_t)f->w * sc > (int16_t)_width) {
            if (!_wrap) break;
            _cx = 0;
            _cy += (int16_t)lineH;
            if (_cy + (int16_t)lineH > (int16_t)_height) _cy = 0;
        }
        if (_textBgValid) drawChar(_cx, _cy, ch, _textFg, _textBg, sc);
        else              drawChar(_cx, _cy, ch, _textFg, sc);
        _cx += (int16_t)pitch;
    }
    endWrite();
}

/* старый API: ST7735_PutStr5x7(scale, X, Y, str, color, bgcolor) */
void ST7735::putStr5x7(uint8_t scale, uint16_t x, uint16_t y,
                       const char *str, uint16_t color, uint16_t bgcolor)
{
  const st7735_font_t *saved = _font;
  _font = &font5x7;               /* тихая подмена — setFont() не зовём! */
  drawString(str, (int16_t)x, (int16_t)y, color, bgcolor, scale);
  _font = saved;                  /* и вернули как было */
}

void ST7735::print(unsigned long n) {
    char b[11];
    char *p = b + 10;
    *p = 0;
    do { *--p = (char)('0' + n % 10); n /= 10; } while (n);
    print(p);
}

void ST7735::print(long n) {
    startWrite();
    if (n < 0) {
        print("-");
        print((unsigned long)(-(n + 1)) + 1UL);
    } else {
        print((unsigned long)n);
    }
    endWrite();
}

void ST7735::print(double d, uint8_t digits) {
  /* Делегируем в общий inline-форматер st7735_fmtDouble — устраняет
   * дублирование кода с ST7735Value::formatNumber, экономит ~150 Б flash.
   * Поведение: NaN/Inf/overflow → "OVF" (раньше "nan"/"ovf" раздельно). */
  char buf[24];
  st7735_fmtDouble(d, digits, buf, sizeof(buf));
  print(buf);
}

void ST7735::drawChangedString(int16_t x, int16_t y,
                               const char *new_str, const char *prev_str,
                               uint16_t fg, uint16_t bg, uint8_t scale) {
    const st7735_font_t *f = _font;
    if (!f || !new_str || !prev_str) return;
    scale = st7735_scale(f, scale);
    uint16_t pitch = (uint16_t)(f->w + 1) * scale;

    /* Считаем длины через st7735_next_char, чтобы правильно
     * обрабатывать UTF-8 (кириллицу). Сравнение — по логическим
     * символам, не байтам. */
    size_t new_len = 0, prev_len = 0;
    {
        const char *p = new_str;
        while (*p) { st7735_next_char(p); new_len++; }
        p = prev_str;
        while (*p) { st7735_next_char(p); prev_len++; }
    }
    size_t max_len = (new_len > prev_len) ? new_len : prev_len;

    /* Проход по обеим строкам одновременно */
    const char *pn = new_str, *pp = prev_str;
    startWrite();
    for (size_t i = 0; i < max_len; i++) {
        uint16_t new_ch = *pn ? st7735_next_char(pn) : (uint16_t)' ';
        uint16_t prev_ch = *pp ? st7735_next_char(pp) : (uint16_t)' ';
        if (new_ch != prev_ch) {
            drawChar(x + (int16_t)(i * pitch), y, new_ch, fg, bg, scale);
        }
    }
    endWrite();
}

#if ST7735_ENABLE_PRINTF
#include <stdio.h>      /* vsnprintf */
#include <stdarg.h>     /* va_list */

void ST7735::printf(const char *fmt, ...) {
    char buf[ST7735_CHUNK];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    print(buf);
}
#endif

#if defined(ST7735_PLAT_ARDUINO)
/* байт продолжения UTF-8 из flash; 0, если невалиден (NUL отсекается маской) */
static uint8_t st7735_rd_cont(const uint8_t *p) {
  uint8_t b = ST7735_RD8(p);
  return ((b & 0xC0) == 0x80) ? b : 0;
}

/* F("..."): байты из flash склеиваются в полные UTF-8-последовательности
   и уходят в print(const char*) — там st7735_next_char конвертирует в CP1251.
   4-байтовые (эмодзи) — вне CP1251, заменяются на '?'. */
void ST7735::print(const __FlashStringHelper *ifs) {
  const uint8_t *p = (const uint8_t *)ifs;
  for (;;) {
    uint8_t c = ST7735_RD8(p++);
    if (!c) return;

    char buf[4];                        /* до 3 байт UTF-8 + NUL */
    buf[0] = (char)c;
    uint8_t n = 1;

    if ((c & 0xE0) == 0xC0) {                                    /* 2 байта */
      uint8_t c2 = st7735_rd_cont(p);
      if (c2) { p++; buf[1] = (char)c2; n = 2; }
    } else if ((c & 0xF0) == 0xE0) {                             /* 3 байта */
      uint8_t c2 = st7735_rd_cont(p);
      if (c2) {
        uint8_t c3 = st7735_rd_cont(p + 1);
        if (c3) { p += 2; buf[1] = (char)c2; buf[2] = (char)c3; n = 3; }
      }
    } else if ((c & 0xF8) == 0xF0) {                             /* 4 байта */
      if (st7735_rd_cont(p) && st7735_rd_cont(p + 1) && st7735_rd_cont(p + 2)) {
        p += 3;
        print("?");
        continue;
      }
    }

    buf[n] = 0;
    print(buf);
  }
}
#endif

uint16_t ST7735::textWidth(const char *s) const {
  return textWidth(s, _textScale);
}

uint16_t ST7735::textWidth(const char *s, uint8_t scale) const
{
  const st7735_font_t *f = _font;
  if (!f || !s || !*s) return 0;
  scale = st7735_scale(f, scale);

  uint16_t n = 0;
  const char *p = s;
  while (*p) {
    uint16_t ch = st7735_next_char(p);
    if (ch == '\n' || ch == '\r') break;
    n++;
  }
  if (!n) return 0;

  /* n*w*scale + (n-1)*scale — глифы + интервалы, всё в масштабе.
     Максимум: 255*255*255 ≈ 16.5M — в uint32_t помещается без UB. */
  uint32_t w = ((uint32_t)n * f->w + (uint32_t)(n - 1)) * scale;
  return (w > 65535U) ? 65535U : (uint16_t)w;
}
uint16_t ST7735::textHeight(uint8_t scale) const {
  const st7735_font_t *f = _font;
  if (!f) return 0;
  return (uint16_t)f->h * st7735_scale(f, scale ? scale : _textScale);
}
uint16_t ST7735::lineHeight(uint8_t scale) const {
  const st7735_font_t *f = _font;
  if (!f) return 0;
  return (uint16_t)(f->h + 1) * st7735_scale(f, scale ? scale : _textScale);
}

/* Одна строка (\n и \r обрывают вывод). Область x..x+w, высота = textHeight():
 * поля слева и справа заливаются bg, так что старый текст стирается.
 * Если текст шире w — выравнивание влево, вывод прекращается на границе ячейки. */
void ST7735::drawStringAligned(const char *s, int16_t x, int16_t y, int16_t w,
                               uint8_t align, uint16_t fg, uint16_t bg, uint8_t scale) {
  const st7735_font_t *f = _font;
  if (!f || !s || w <= 0) return;
  scale = st7735_scale(f, scale);
  int32_t cw = *s ? (int32_t)textWidth(s, scale) : 0;  /* Fix: убран + scale — пиксельная точность RIGHT */   /* ширина с хвостовым зазором */
  int32_t off = 0;
  if (cw < w) {
    if (align == ST7735_ALIGN_CENTER)     off = (w - cw) / 2;
    else if (align == ST7735_ALIGN_RIGHT) off = w - cw;
  }
  const int16_t h = (int16_t)(f->h * scale);
  const int32_t xEnd = (int32_t)x + w;
  int32_t cx = (int32_t)x + off;

  startWrite();
  if (off > 0) fillRect(x, y, (int16_t)off, h, bg);
  const char *p = s;
  while (*p && cx < xEnd) {
    uint16_t ch = st7735_next_char(p);
    if (ch == '\n' || ch == '\r') break;
    if (cx + (int32_t)f->w * scale > xEnd) break;  /* glyph width only, not pitch — последний символ влезает */
    cx += drawChar((int16_t)cx, y, ch, fg, bg, scale);
  }
  if (cx < xEnd) fillRect((int16_t)cx, y, (int16_t)(xEnd - cx), h, bg);
  endWrite();
}

/* =====================================================================
    ТЕРМИНАЛ: print()/println() с автоскроллом строк вверх
    (модуль целиком под ST7735_ENABLE_TERMINAL — см. st7735.h)
   ===================================================================== */
#if ST7735_ENABLE_TERMINAL

void ST7735::setTerminal(int16_t x, int16_t y, int16_t w, int16_t h)
{
  if (w <= 0 || h <= 0) {
    _termOn = false;
    return;
  }
  /* При отрицательных x/y обрезаем не только позицию, но и размер — иначе
   * запрошенная область «сдвигалась» на |x| пикселей вправо, оставляя
   * справа пустой хвост ширины |x|. Например setTerminal(-10, 0, 50, 100)
   * давала x=0,w=50 (правый край в x=50), а должна x=0,w=40. */
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if ((int32_t)x + w > (int32_t)_width)  w = (int16_t)((int32_t)_width  - x);
  if ((int32_t)y + h > (int32_t)_height) h = (int16_t)((int32_t)_height - y);
  if (w <= 0 || h <= 0) {
    _termOn = false;
    return;
  }

  _termX = x; _termY = y; _termW = w; _termH = h;
  _termOn = true;
  termClear();
  if (_termHwReq) termHwActivate();   /* под новую геометрию (шрифт мог смениться) */
}

void ST7735::termClear(void)
{
  memset(_termBuf, 0, sizeof(_termBuf));
  _termRow = 0; _termCol = 0;
  if (_termOn) {
    if (_termHwOn) termHwReset();     /* натуральное отображение — заливка корректна */
    fillRect(_termX, _termY, _termW, _termH, _termBg);
  }
}

void ST7735::termRedraw(void) {
    const st7735_font_t *f = _font;
    if (!f) return;
    uint8_t  sc    = st7735_scale(f, _textScale);
    uint16_t pitch = (uint16_t)(f->w + 1) * sc;
    uint16_t lineH = (uint16_t)(f->h + 1) * sc;
    uint16_t rows  = (uint16_t)_termH / lineH;
    if (rows == 0) return;
    if (rows > ST7735_TERM_ROWS) rows = ST7735_TERM_ROWS;
    uint16_t cols = (uint16_t)_termW / pitch;
    if (cols > ST7735_TERM_COLS - 1) cols = ST7735_TERM_COLS - 1;

    /* Программная перерисовка рисует в «наивных» координатах: сначала
     * возвращаем контроллеру натуральное отображение. */
    if (_termHwOn) termHwReset();

    startWrite();                                       /* одна транзакция на редроу */
    for (uint16_t r = 0; r < rows; r++) {
        int16_t y  = _termY + (int16_t)(r * lineH);
        uint16_t c = 0;
        /* рисуем все символы строки — каждый закрашивает свою ячейку целиком */
        for (; c < cols && _termBuf[r][c]; c++)
            drawChar(_termX + (int16_t)(c * pitch), y,
                     _termBuf[r][c], _textFg, _termBg, sc);
        /* хвост строки после последнего символа — заливаем фоном */
        int16_t xe = _termX + (int16_t)(c * pitch);
        int16_t wt = _termX + _termW - xe;
        if (wt > 0)
            fillRect(xe, y, wt, (int16_t)(f->h * sc), _termBg);
    }
    endWrite();
}

void ST7735::termScroll(uint16_t rows) {
    if (rows > ST7735_TERM_ROWS) rows = ST7735_TERM_ROWS;
    if (rows == 0) return;
    /* ОДИН memmove вместо цикла memcpy. Старый код звал memcpy в цикле для
     * перекрывающихся областей (_termBuf[r-1] и _termBuf[r] — соседние строки,
     * memcpy копирует байт за байтом, и при copy-down это работает, но
     * технически UB по стандарту C. memmove корректен для перекрытий.
     * На AVR/STM32 memmove реализован ассемблерными блочными командами —
     * в десятки раз быстрее побайтового цикла.
     *
     * Сдвиг: строки 1..rows-1 → 0..rows-2 (длина (rows-1)*COLS байт).
     * При rows=1: memmove с длиной 0 — безопасный no-op, дальше memset
     * очищает строку 0 (как и в старом коде — для терминалов из 1 строки).
     * Последняя строка (rows-1) всегда очищается. */
    memmove(_termBuf[0], _termBuf[1], (uint32_t)(rows - 1) * ST7735_TERM_COLS);
    memset(_termBuf[rows - 1], 0, ST7735_TERM_COLS);

    /* Экранная перерисовка — реализовано: аппаратный вертикальный скролл
     * ST7735 (VSCRDEF 0x33 + VSCSAD 0x37) через setScrollArea/scrollTo.
     * Вместо перерисовки всей области — одна команда VSCSAD (4 байта) +
     * очистка новой нижней строки (w*lineH пикселей). Экономия до ~95%
     * SPI-трафика при лог-выводе. Включается termHwScroll(true) после
     * setTerminal(); см. ограничения в st7735.h. */
    const st7735_font_t *f = _font;
    if (_termHwOn && f) {
        uint8_t  sc    = st7735_scale(f, _textScale);
        uint16_t lineH = (uint16_t)(f->h + 1) * sc;
        /* rows*lineH == VSA — терминал без «неполной» строки (условие активации) */
        if (lineH == _termHwLineH && rows > 0 &&
            (uint32_t)rows * lineH == (uint32_t)_termVsa) {
            /* 1) сдвигаем окно прокрутки на одну текстовую строку вверх.
             * VSP уменьшается на lineH (масштаб GRAM-строк — см. termHwActivate). */
            uint32_t off = (uint32_t)_termVspOff + _termVsa - lineH;
            _termVspOff  = (uint16_t)(off % _termVsa);
            startWrite();
            scrollTo((int16_t)(_termTfa + (int16_t)_termVspOff));
            /* 2) новая нижняя строка: в неё «переехал» старый верх — чистим.
             * termHwRowLine возвращает ФИЗИЧЕСКУЮ GRAM-строку, в которую нужно
             * писать, чтобы пиксель появился на нужной экранной позиции
             * (с учётом текущего сдвига VSP). Раньше тут была инверсия
             * `159 - GRAM` — это двойная инверсия, давала сдвиг в обратную
             * сторону. */
            int16_t y = (int16_t)termHwRowLine((uint16_t)(rows - 1), rows, lineH);
            fillRect(_termX, y, _termW, (int16_t)lineH, _termBg);
            endWrite();
            return;
        }
        /* Конфигурация устарела (смена шрифта/масштаба в обход setTerminal) —
         * до следующего termHwScroll(true)/setTerminal работаем программно. */
        _termHwOn = false;
    }
    startWrite();
    termRedraw();
    endWrite();
}

/* ---------- аппаратный скролл: настройка и хелперы ---------- */

/* Настроить VSCRDEF под ТЕКУЩУЮ геометрию терминала и шрифт.
 * false = активация не удалась (мягкий отказ: остаётся софт-скролл).
 *
 * Система координат. Панель 160x128 в нативной ориентации (поворот 0,
 * MADCTL MX|MY) показывает ФИЗИЧЕСКУЮ строку GRAM 159-y на экранной
 * позиции y. Область прокрутки обязана накрыть экранную зону терминала
 * [termY, termY+termH) — в строках GRAM это [TFA, TFA+VSA), где
 *   TFA = yoff + height - termH - termY,  VSA = termH.
 * Контроллер сканирует строки в порядке возрастания номеров; VSCSAD
 * показывает строку VSP на НИЖНЕМ краю области (позиция termY+termH-1),
 * при VSP=TFA отображение натуральное. Скролл вверх: VSP -= lineH. */
bool ST7735::termHwActivate(void) {
    _termHwOn = false;
    if (!_termOn || !_font) return false;

    const st7735_font_t *f = _font;
    uint8_t  sc    = st7735_scale(f, _textScale);
    uint16_t lineH = (uint16_t)(f->h + 1) * sc;
    uint16_t rows  = (uint16_t)_termH / lineH;
    if (lineH == 0 || rows == 0) return false;
    if (rows > ST7735_TERM_ROWS) return false;                 /* буфер не вместит */
    if ((uint32_t)rows * lineH != (uint32_t)_termH) return false; /* неполная нижняя строка */
    if (_rotation != 0) return false;             /* HW-скролл только для портрета 0° */
    if (_termX != 0 || _termW != (int16_t)_width) return false; /* строка GRAM скроллится целиком */

    int32_t tfa = (int32_t)_yoff + _height - _termH - _termY;
    if (tfa < 0) return false;
    int32_t sum = tfa + _termH;                   /* + BFA = height - sum → сумма = _height */
    if (sum > (int32_t)_height) return false;    /* зелёный tab во весь экран: 161 > 160 */

    setScrollArea((int16_t)tfa, _termH, (int16_t)((int32_t)_height - sum));
    _termTfa     = (int16_t)tfa;
    _termVsa     = (uint16_t)_termH;
    _termVspOff  = 0;
    _termHwLineH = lineH;
    _termHwOn    = true;
    return true;
}

void ST7735::termHwReset(void) {
    scrollTo(_termTfa);
    _termVspOff = 0;
}

/* Физическая GRAM-строка, отображаемая на ВЕРХНЕЙ границе текстовой
 * строки r (после текущего сдвига _termVspOff). Из отображения:
 * экранная позиция d показывает TFA + ((VSP-TFA + posBottom-d) mod VSA). */
uint16_t ST7735::termHwRowLine(uint16_t r, uint16_t rows, uint16_t lineH) const {
    (void)rows;   /* VSA = rows*lineH по условию активации */
    int32_t k = (int32_t)_termVspOff + (int32_t)_termVsa - 1
              - (int32_t)r * lineH;
    k %= (int32_t)_termVsa;
    if (k < 0) k += _termVsa;
    return (uint16_t)((int32_t)_termTfa + k);
}

void ST7735::termHwScroll(bool on) {
    _termHwReq = on;
    if (!on) {
        if (_termHwOn) {
            /* Сброс в натуральное отображение, затем программную
             * перерисовку: контент после сброса «встаёт» на свои места. */
            termHwReset();
            _termHwOn = false;
            termRedraw();
        }
        return;
    }
    if (_termOn) termHwActivate();
    /* Активация не удалась (не те условия) — тихо остаёмся на софт-скролле;
     * запрос _termHwReq сохранится и сработает при следующем setTerminal(). */
}

void ST7735::termPutChar(uint16_t ch)
{
  const st7735_font_t *f = _font;
  if (!f) return;

  uint8_t  sc    = st7735_scale(f, _textScale);
  uint16_t pitch = (uint16_t)(f->w + 1) * sc;
  uint16_t lineH = (uint16_t)(f->h + 1) * sc;
  uint16_t cols  = (uint16_t)_termW / pitch;
  uint16_t rows  = (uint16_t)_termH / lineH;
  if (cols == 0 || rows == 0) return;
  if (cols > ST7735_TERM_COLS - 1) cols = ST7735_TERM_COLS - 1;
  if (rows > ST7735_TERM_ROWS)     rows = ST7735_TERM_ROWS;
  if (_termRow >= rows) _termRow = (uint8_t)(rows - 1);  /* масштаб сменили на лету */

  if (ch == '\r') {
    _termCol = 0;
    return;
  }

  if (ch == '\n') {
    _termCol = 0;
    if ((uint16_t)_termRow + 1 >= rows) termScroll(rows); else _termRow++;
    return;
  }

  if (_termCol >= cols) {                     /* длинная строка — автоперенос */
    _termCol = 0;
    if ((uint16_t)_termRow + 1 >= rows) termScroll(rows); else _termRow++;
  }

      _termBuf[_termRow][_termCol] = (uint8_t)ch;
      /* При активном HW-скролле дисплей сдвинут относительно GRAM:
       * пишем глиф в физическую GRAM-строку, отображаемую на месте
       * текстовой строки _termRow. termHwRowLine возвращает GRAM-строку
       * напрямую — без инверсии (раньше тут было `159 - GRAM`, что давало
       * обратный сдвиг — это был баг).
       *
       * ВНИМАНИЕ: направление VSP (_termVspOff + VSA - lineH ≡ - lineH
       * по модулю VSA) и формула termHwRowLine согласованы между собой,
       * но требуют проверки на реальном железе — семантика VSCSAD
       * (строка на нижнем или верхнем краю области) различается
       * между даташитами и реализациями контроллеров. */
      int16_t dy = _termY + (int16_t)(_termRow * lineH);
      if (_termHwOn)
          dy = (int16_t)termHwRowLine(_termRow, rows, lineH);
      drawChar(_termX + (int16_t)(_termCol * pitch),
           dy, ch, _textFg, _termBg, sc);
  _termCol++;
}

#endif /* ST7735_ENABLE_TERMINAL */

void ST7735::clear(uint16_t color) {
#if ST7735_ENABLE_TERMINAL
  if (_termOn) {
    _termBg = color;                              /* новый фон */
    setTerminal(_termX, _termY, _termW, _termH);  /* termClear зальёт _termBg */
    setCursor(0, 0);
    return;
  }
#endif
  fillScreen(color);
  setCursor(0, 0);
}

