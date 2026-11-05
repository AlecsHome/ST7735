/*
 * st7735ui.h — Набор графических UI-компонентов для ST7735
 * Версия v5.0 (с Auto-Range и Peak-Hold):
 *   - ST7735Value: форматирование double через st7735_fmtDouble() (inline в
 *     st7735.h) — без dtostrf, без snprintf, без -lprintf_flt, без <math.h>.
 *     Работает одинаково на AVR / ESP32 / STM32 / ESP8266.
 *   - ST7735Value: единица измерения вынесена в настраиваемый параметр _unit
 *     (по умолчанию "V", можно задать "mV", "mA", "°C", "%" или "").
 *   - ST7735BarGauge: новый аналоговый шкальный индикатор (Bar Gauge / шкала)
 *     с поддержкой дифференциального (безмерцательного) обновления, динамической
 *     сменой цвета при превышении порога (warnColor) и делениями шкалы.
 *   - ST7735Plotter: реаловременный график/осциллограф (Waveform / Plotter)
 *     с кольцевым буфером, сеткой (grid), авто/фикс диапазоном. RAM на AVR:
 *     _rawMv[120] = 240 Б + ~30 Б полей класса ≈ 270 Б (не «~120 Б», как было
 *     написано в ранних комментариях — там считали только uint8_t-индексы).
 *
 *     Памятка: сколько символов влезает
 *     При portait 128px ширина, виджет _w=118px:
 *  ╔══════════╦═══════╦═══════╦══════════════════════╗
 *  ║  Шрифт   ║ scale ║ pitch ║ Max символов в 118px ║
 *  ╠══════════╬═══════╬═══════╬══════════════════════╣
 *  ║ font5x7  ║   1   ║  6px  ║       19             ║
 *  ║ font5x7  ║   2   ║  12px ║       9              ║
 *  ║ font7x11 ║   1   ║  8px  ║       14             ║
 *  ║*font7x11*║ **2** ║ *16px*║     **7**            ║
 *  ╚══════════╩═══════╩═══════╩══════════════════════╝
 */

#pragma once
#include <string.h>
#include "st7735.h"          /* st7735_fmtDouble — inline, доступен здесь */
/* <math.h> не нужен: NaN ловится через (v != v), Inf/overflow — через "OVF"
 * в самом st7735_fmtDouble. <stdio.h> не нужен — snprintf заменён на ручную
 * сборку через st7735_fmtDouble. Минус две зависимости, минус ~1 КБ flash
 * на AVR (libm + libprintf_flt). */

/* =====================================================================
 *  Включение виджетов — макросами ST7735_ENABLE_* (по образцу терминала).
 *  Каждый виджет компилируется только если его макрос = 1. Если 0 — класс
 *  не существует, его поля не занимают RAM, его методы не занимают flash.
 *
 *  ДЕФОЛТ = 0 ДЛЯ ВСЕХ — сознательный breaking change в стиле
 *  «включай только нужное». Каждый скетч явно декларирует зависимости,
 *  никто не тянет в бинарник мусор. Это обратное от «всё включено по
 *  умолчанию» — при обновлении библиотеки старые скетчи потребуют правки:
 *
 *      #define ST7735_ENABLE_CHECKBOX 1
 *      #define ST7735_ENABLE_VALUE    1
 *      #define ST7735_ENABLE_BARGAUGE 1
 *      #define ST7735_ENABLE_PLOTTER  1
 *      #include "st7735ui.h"
 *
 *  Ориентировочная экономия flash на AVR при включении только нужного
 *  (по сравнению с «всё включено»):
 *    PLOTTER   ~1.5 КБ (с парсером UTF-8 для overlay)
 *    BARGAUGE  ~0.8 КБ (drawTicks + дифф-перерисовка)
 *    VALUE     ~1.2 КБ (st7735_fmtDouble разнесён в st7735.h, но авто-масштаб)
 *    CHECKBOX  ~0.4 КБ
 *    TOGGLE    ~0.3 КБ
 *    BUTTON    ~0.3 КБ
 *
 *  Если в коде используется выключенный класс — компилятор даст ясную
 *  ошибку: 'ST7735Xxx' does not name a type. Это и подсказка: добавьте
 *  #define ST7735_ENABLE_XXX 1 перед #include.
 * ===================================================================== */
#ifndef ST7735_ENABLE_LOG
  #define ST7735_ENABLE_LOG      0
#endif
#ifndef ST7735_ENABLE_SCREENS
  #define ST7735_ENABLE_SCREENS  0
#endif
#ifndef ST7735_ENABLE_BUTTON
  #define ST7735_ENABLE_BUTTON   0
#endif
#ifndef ST7735_ENABLE_TOGGLE
  #define ST7735_ENABLE_TOGGLE   0
#endif
#ifndef ST7735_ENABLE_CHECKBOX
  #define ST7735_ENABLE_CHECKBOX 0
#endif
#ifndef ST7735_ENABLE_VALUE
  #define ST7735_ENABLE_VALUE    0
#endif
#ifndef ST7735_ENABLE_BARGAUGE
  #define ST7735_ENABLE_BARGAUGE 0
#endif
#ifndef ST7735_ENABLE_PLOTTER
  #define ST7735_ENABLE_PLOTTER  0
#endif


#if ST7735_ENABLE_BUTTON
/* ===== Кнопка ===== */
class ST7735Button {
public:
    ST7735Button(ST7735 &tft, int16_t x, int16_t y, int16_t w, int16_t h,
                 const char *label, uint16_t fg, uint16_t bg,
                 uint16_t border, uint16_t pressBg, uint8_t scale = 1)
        : _tft(tft), _x(x), _y(y), _w(w), _h(h),
          _fg(fg), _bg(bg), _border(border), _pressBg(pressBg),
          _scale(scale ? scale : 1), _pressed(false), _visible(false) {
        strncpy(_label, label ? label : "", sizeof(_label) - 1);
        _label[sizeof(_label) - 1] = 0;
    }

    void draw() {
        const uint16_t bg = _pressed ? _pressBg : _bg;
        _tft.startWrite();                     /* вся кнопка одной SPI-сессией */
        _tft.fillRoundRect(_x, _y, _w, _h, 3, bg);
        _tft.drawRoundRect(_x, _y, _w, _h, 3, _border);
        if (_label[0] && _tft.font()) {
            int16_t ty = _y + (_h - (int16_t)_tft.textHeight(_scale)) / 2;
            if (ty < _y) ty = _y;
            _tft.drawStringAligned(_label, _x + 1, ty, _w - 2,
                                   ST7735_ALIGN_CENTER, _fg, bg, _scale);
        }
        _tft.endWrite();
        _visible = true;
    }

    void setPressed(bool p) {
        if (p == _pressed) return;
        _pressed = p;
        if (_visible) draw();
    }

    bool isPressed() const { return _pressed; }

    bool contains(int16_t x, int16_t y) const {
        return x >= _x && x < _x + _w && y >= _y && y < _y + _h;
    }

private:
    ST7735 &_tft;
    int16_t _x, _y, _w, _h;
    char _label[24];
    uint16_t _fg, _bg, _border, _pressBg;
    uint8_t _scale;
    bool _pressed, _visible;
};
#endif /* ST7735_ENABLE_BUTTON */

#if ST7735_ENABLE_TOGGLE
/* ===== Тумблер ===== */
class ST7735Toggle {
public:
    ST7735Toggle(ST7735 &tft, int16_t x, int16_t y, int16_t w, int16_t h,
                 uint16_t onColor, uint16_t offColor, uint16_t knobColor,
                 uint16_t borderColor)
        : _tft(tft), _x(x), _y(y), _w(w), _h(h),
          _onColor(onColor), _offColor(offColor), _knobColor(knobColor),
          _borderColor(borderColor), _on(false), _visible(false) {}

    void draw() {
        const uint16_t bg = _on ? _onColor : _offColor;
        const int16_t r = _h / 2;
        _tft.startWrite();
        _tft.fillRoundRect(_x, _y, _w, _h, r, bg);
        _tft.drawRoundRect(_x, _y, _w, _h, r, _borderColor);

        const int16_t knobR = (r > 2) ? r - 2 : 1;
        const int16_t knobX = _on ? (_x + _w - r - 1) : (_x + r);
        const int16_t knobY = _y + r;
        _tft.fillCircle(knobX, knobY, knobR, _knobColor);
        _tft.drawCircle(knobX, knobY, knobR, _borderColor);
        _tft.endWrite();
        _visible = true;
    }

    void setOn(bool on) {
        if (on == _on) return;
        _on = on;
        if (!_visible) return;          /* как Button/Checkbox: меняем состояние
                                         * молча, до явного draw(). Раньше тут
                                         * стояло draw() — противоречило другим
                                         * виджетам и форсировало отрисовку
                                         * до настройки цветов/геометрии. */
        /* Рамка (drawRoundRect) уже нарисована в draw() — заливаем ТОЛЬКО
         * внутренность (на 1 px внутрь), иначе fillRoundRect затирал бы рамку
         * (раньше после первого переключения рамка пропадала). */
        const int16_t r = _h / 2;
        const int16_t knobR = (r > 2) ? r - 2 : 1;
        const int16_t knobY = _y + r;
        const int16_t knobX = on ? (_x + _w - r - 1) : (_x + r);
        const uint16_t bg = on ? _onColor : _offColor;
        _tft.startWrite();
        _tft.fillRoundRect(_x + 1, _y + 1, _w - 2, _h - 2, (r > 0) ? r - 1 : 0, bg);
        _tft.fillCircle(knobX, knobY, knobR, _knobColor);
        _tft.drawCircle(knobX, knobY, knobR, _borderColor);
        _tft.endWrite();
    }

    bool isOn() const { return _on; }
    void toggle() { setOn(!_on); }

    bool contains(int16_t x, int16_t y) const {
        return x >= _x && x < _x + _w && y >= _y && y < _y + _h;
    }

private:
    ST7735 &_tft;
    int16_t _x, _y, _w, _h;
    uint16_t _onColor, _offColor, _knobColor, _borderColor;
    bool _on, _visible;
};
#endif /* ST7735_ENABLE_TOGGLE */

#if ST7735_ENABLE_CHECKBOX
/* ===== Чекбокс ===== */
class ST7735Checkbox {
public:
    ST7735Checkbox(ST7735 &tft, int16_t x, int16_t y, int16_t size,
                   uint16_t borderColor, uint16_t checkColor,
                   uint16_t bgColor, const char *label,
                   uint16_t textColor, uint8_t scale = 1)
        : _tft(tft), _x(x), _y(y), _size(size),
          _borderColor(borderColor), _checkColor(checkColor),
          _bgColor(bgColor), _textColor(textColor),
          _scale(scale ? scale : 1), _checked(false), _visible(false) {
        strncpy(_label, label ? label : "", sizeof(_label) - 1);
        _label[sizeof(_label) - 1] = 0;
    }

    void draw() {
        _tft.startWrite();
        _tft.fillRect(_x, _y, _size, _size, _bgColor);
        _tft.drawRect(_x, _y, _size, _size, _borderColor);

        if (_checked) {
            const int16_t x1 = _x + 2,             y1 = _y + _size / 2;
            const int16_t x2 = _x + _size * 2 / 5, y2 = _y + _size - 3;
            const int16_t x3 = _x + _size - 3,     y3 = _y + 2;
            _tft.drawLine(x1, y1, x2, y2, _checkColor);
            _tft.drawLine(x2, y2, x3, y3, _checkColor);
        }

        if (_label[0] && _tft.font()) {
            const int16_t ty = _y + (_size - (int16_t)_tft.textHeight(_scale)) / 2;
            _tft.drawString(_label, _x + _size + 4, ty,
                            _textColor, _bgColor, _scale);
        }
        _tft.endWrite();
        _visible = true;
    }

    void setChecked(bool c) {
        if (c == _checked) return;
        _checked = c;
        if (_visible) draw();
    }

    bool isChecked() const { return _checked; }
    void toggle() { setChecked(!_checked); }

    /* Нажатие по квадрату или по подписи. По y учитываем ФАКТИЧЕСКУЮ
     * позицию текста: ty = _y + (_size - textHeight)/2. Если шрифт крупнее
     * квадрата, текст выходит выше _y и ниже _y+_size — область нажатия
     * расширяется до min(_y, ty) сверху и max(_y+_size, ty+th) снизу. */
    bool contains(int16_t x, int16_t y) const {
        int16_t w = _size;
        int16_t top = _y;
        int16_t bottom = _y + _size;
        if (_label[0]) {
            w += 4 + (int16_t)_tft.textWidth(_label, _scale);
            int16_t th = (int16_t)_tft.textHeight(_scale);
            int16_t ty = _y + (_size - th) / 2;
            if (ty < top) top = ty;
            if (ty + th > bottom) bottom = ty + th;
        }
        return x >= _x && x < _x + w && y >= top && y < bottom;
    }

private:
    ST7735 &_tft;
    int16_t _x, _y, _size;
    char _label[24];
    uint16_t _borderColor, _checkColor, _bgColor, _textColor;
    uint8_t _scale;
    bool _checked, _visible;
};
#endif /* ST7735_ENABLE_CHECKBOX */

#if ST7735_ENABLE_VALUE
/* ===== Числовой индикатор =====
 * Фиксированная зона (x, y, w). Внутри — одна строка текста.
 * Перерисовка: ВСЕГДА полная очистка + drawStringAligned (drawString сам по
 * себе рисует только глифы, но заливку левого/правого поля делаем мы через
 * fillRect перед ним). Это устраняет артефакты «остатков от V» при смене
 * значения (например: 1.234 → 99.9 — правая часть виджета должна быть bg).
 *
 * Раньше в классе было поле _prev[24] для заявленного «диффа по символам»
 * через drawChangedString(), но оно НИКОГДА не читалось — только
 * записывалось `_prev[0] = 0;`. Удалено как мёртвый код: -24 Б RAM на
 * каждый виджет. Если понадобится посимвольная перерисовка, можно вернуть
 * и связать с ST7735::drawChangedString() — но это усложнит логику
 * авто-масштаба и смены align/bg.
 *
 * Совместимость: st7735_fmtDouble() — единый inline-форматер в st7735.h,
 * работает на всех платформах без флагов линкера.
 * Единица измерения вынесена в параметр _unit (по умолчанию "V").
 */
class ST7735Value {
public:
    ST7735Value(ST7735 &tft, int16_t x, int16_t y, int16_t w,
                uint8_t align, uint16_t fg, uint16_t bg, uint8_t scale = 2,
                const char *unit = "V", bool autoScale = true)
        : _tft(tft), _x(x), _y(y), _w(w),
          _align(align), _fg(fg), _bg(bg),
          _scale(scale ? scale : 1), _autoScale(autoScale),
          _prevValid(false), _prevWasError(false),
          _prevScale(0), _prevFg(0), _errFg(ST7735_RED), _digits(2),
          _type(VT_NONE), _lastV(0.0) {
        _cur[0] = 0;
        _prev[0] = 0;
        setUnit(unit);
    }

    void setAutoScale(bool enable) {
        if (_autoScale != enable) {
            _autoScale = enable;
            invalidate();
        }
    }
    bool autoScale() const { return _autoScale; }

    /* Изменение единицы измерения (например: "V", "mV", "mA", "°""C", "%", "").
     * Если сейчас отображается ЧИСЛО (последний set(double) был успешным) —
     * сразу переформатирует и перерисует с новым суффиксом.
     * Если сейчас ошибка (setError) или произвольный текст (setRaw) —
     * суффикс обновится в поле, но экран не перерисуется. */
    void setUnit(const char *unit) {
        if (!unit) { _unit[0] = 0; }
        else {
            strncpy(_unit, unit, sizeof(_unit) - 1);
            _unit[sizeof(_unit) - 1] = 0;
        }
        if (_type == VT_NUMBER) {
            formatNumber(_lastV, _cur, sizeof(_cur));
            /* formatNumber возвращает "OVF" для переполнения, но может дописать
             * суффикс единицы — "OVF V", "OVF °C" и т.п. strcmp(_cur, "OVF")
             * такие варианты пропускал — проверяем первые 3 байта. */
            if (!strncmp(_cur, "OVF", 3)) {
                setError("OVF", _errFg);
                return;
            }
            invalidate();
        }
    }

    const char* unit() const { return _unit; }

    /* Значение. digits = 0..6; 0xFF = "не менять digits".
     * NaN ловим через (v != v) — не нужен <math.h>/isfinite. Inf и overflow
     * обрабатывает st7735_fmtDouble — возвращает "OVF", рисуем как ошибку.
     * _prevWasError НЕ меняем здесь — это делает redraw() в конце,
     * иначе forceFull-проверка не поймает переход «ошибка → число». */
    void set(double v, uint8_t digits = 0xFF) {
        if (digits != 0xFF) _digits = (digits > 6) ? 6 : digits;
        if (v != v) { setError("----"); return; }      /* NaN */

        formatNumber(v, _cur, sizeof(_cur));
        /* См. setUnit(): formatNumber может вернуть "OVF" или "OVF V". */
        if (!strncmp(_cur, "OVF", 3)) { setError("OVF", _errFg); return; }

        _type = VT_NUMBER;
        _lastV = v;
        redraw(false);
    }

    /* Текст ошибки/состояния. Цвет — errFg (по умолчанию красный).
     * _prevWasError НЕ меняем здесь — это делает redraw() в конце. */
    void setError(const char *msg, uint16_t errFg = ST7735_RED) {
        if (_errFg != errFg) _prevValid = false;
        _errFg = errFg;
        strncpy(_cur, msg ? msg : "", sizeof(_cur) - 1);
        _cur[sizeof(_cur) - 1] = 0;
        _type = VT_ERROR;
        redraw(true);
    }

    /* Готовая строка — форматирование на стороне приложения.
     * Используется для статусов вроде "READY", "DONE", "WAIT".
     * setUnit() на таком состоянии НЕ пересчитывает — setRaw() хранит
     * уже готовую строку. */
    void setRaw(const char *s, bool force = false) {
        if (!s) return;
        strncpy(_cur, s, sizeof(_cur) - 1);
        _cur[sizeof(_cur) - 1] = 0;
        _type = VT_RAW;
        if (force) _prevValid = false;
        redraw(false);
    }

    /* Смена цвета — форсирует перерисовку */
    void setTextColor(uint16_t fg)              { _fg = fg;  invalidate(); }
    void setTextColor(uint16_t fg, uint16_t bg) { _fg = fg; _bg = bg; invalidate(); }

    /* Принудительная перерисовка */
    void invalidate() { _prevValid = false; redraw(_prevWasError); }

    /* Первая отрисовка виджета. Единообразный API с другими виджетами
     * (Button/Toggle/Checkbox/BarGauge/Plotter тоже имеют draw()).
     * Эквивалентен invalidate() — перерисовывает текущее состояние _cur.
     * Если _cur пустой (свежий виджет) — рисует пустую ячейку. Чтобы сразу
     * показать значение, вызовите set(...) вместо draw(). */
    void draw() { invalidate(); }

    const char *text() const { return _cur; }

private:
    /* Платформенно-независимо через st7735_fmtDouble — без dtostrf, без snprintf,
     * без -lprintf_flt, без <math.h>. Те же правила округления, что в
     * ST7735::print(double). Inf/overflow → "OVF" (от formatNumber возвращается
     * в _cur, дальше set() решает: ошибка или нет).
     * Переполнение итогового буфера (число + суффикс) — тоже "OVF". */
    void formatNumber(double v, char *buf, size_t n) const {
        char tmp[20];
        size_t len  = st7735_fmtDouble(v, _digits, tmp, sizeof(tmp));
        size_t ulen = strlen(_unit);
        size_t extra = ulen ? ulen + 1 : 0;          /* пробел + единица */

        if (len + extra >= n) {
            if (n >= 4) memcpy(buf, "OVF", 4); else if (n) buf[0] = 0;
            return;
        }
        memcpy(buf, tmp, len);
        if (extra) {
            buf[len] = ' ';
            memcpy(buf + len + 1, _unit, ulen + 1);
        } else {
            buf[len] = 0;
        }
    }

    void redraw(bool isError) {
        const uint16_t fg = isError ? _errFg : _fg;

        /* Авто-масштабирование: понижаем scale если текст не влезает в _w */
        uint8_t effScale = _scale;
        if (_autoScale && effScale > 1 && _tft.font()) {
            while (effScale > 1 && (int32_t)_tft.textWidth(_cur, effScale) > _w)
                effScale--;
        }

        /* Проверка изменения масштаба */
        const bool scaleChanged = (_prevScale != 0 && effScale != _prevScale);
        const bool forceFull = (!_prevValid || isError != _prevWasError ||
                                _align != ST7735_ALIGN_LEFT || scaleChanged);

        /* Skip-redraw оптимизация: если _cur, effScale, isError, fg не изменились
         * с прошлой перерисовки — пропускаем SPI-трафик целиком. Это критично
         * для polling-датчиков (ADS1115, термопары): value.set(12.45) × 100
         * раз в секунду — теперь рисуется 1 раз, а не 100. Сравниваем уже
         * ОТФОРМАТИРОВАННУЮ строку (а не double) — так «12.454» и «12.455»
         * при digits=2 дают «12.45»/«12.46» — разные, а «12.4541»/«12.4542»
         * дают «12.45»/«12.45» — skip срабатывает. */
        if (_prevValid && !forceFull && effScale == _prevScale &&
            isError == _prevWasError && fg == _prevFg &&
            strcmp(_cur, _prev) == 0) {
            return;
        }

        _tft.startWrite();
        /* drawStringAligned сам заливает всю область _w x textHeight (поля
         * слева/справа тоже), поэтому отдельный fillRect всей строки не нужен —
         * раньше каждый пиксель писался дважды (мерцание + 2x SPI-трафик).
         * Дочищаем только «хвост» по высоте, если предыдущий масштаб был крупнее. */
        if (forceFull) {
            uint8_t clearScale = (_prevScale > effScale) ? _prevScale : effScale;
            if (clearScale < _scale) clearScale = _scale;
            const int16_t hNew  = (int16_t)_tft.textHeight(effScale);
            const int16_t hFull = (int16_t)_tft.lineHeight(clearScale);
            if (hFull > hNew)
                _tft.fillRect(_x, _y + hNew, _w, hFull - hNew, _bg);
        }
        _tft.drawStringAligned(_cur, _x, _y, _w, _align, fg, _bg, effScale);
        _tft.endWrite();

        _prevValid = true;
        _prevWasError = isError;
        _prevScale = effScale;
        _prevFg = fg;
        strcpy(_prev, _cur);          /* кэш для skip-redraw проверки */
    }

    ST7735 &_tft;
    int16_t _x, _y, _w;
    uint8_t _align;
    uint16_t _fg, _bg;
    uint8_t _scale;
    bool _autoScale;
    char _cur[24];
    char _unit[8];
    bool _prevValid;
    bool _prevWasError;
    uint8_t _prevScale;
    uint16_t _prevFg;                  /* для skip-redraw: прошлый fg/isError-цвет */
    uint16_t _errFg;
    uint8_t _digits;
    char _prev[24];                    /* для skip-redraw: прошлая отформатированная строка */

    /* Тип текущего содержимого _cur — нужен для setUnit(): если последнее
     * set() было числом, при смене unit переформатируем. Иначе (ошибка или
     * raw-текст) — оставляем как есть. */
    enum ValueType { VT_NONE, VT_NUMBER, VT_ERROR, VT_RAW };
    ValueType _type;
    double _lastV;           /* последнее значение для переформатирования */
};
#endif /* ST7735_ENABLE_VALUE */

#if ST7735_ENABLE_BARGAUGE
/* ===== Аналоговый шкальный индикатор (Bar Gauge / Шкала) =====
 * Горизонтальная линейная шкала с дифференциальным (безмерцательным)
 * обновлением и предупреждающим цветом (порог перегрузки).
 * Вертикальная ориентация — требуется отдельная ветка геометрии
 * (fillRect по Y вместо X, перерасчёт fillPix через _h и т.д.).
 */
class ST7735BarGauge {
public:
    ST7735BarGauge(ST7735 &tft, int16_t x, int16_t y, int16_t w, int16_t h,
                   double minVal = 0.0, double maxVal = 30.0,
                   uint16_t barColor = ST7735_GREEN,
                   uint16_t bgColor = ST7735_BLACK,
                   uint16_t borderColor = ST7735_WHITE,
                   uint16_t warnColor = ST7735_RED,
                   double warnThreshold = 28.0)
        : _tft(tft), _x(x), _y(y), _w(w), _h(h),
          _minVal(st7735_range_ok(minVal, maxVal) ? minVal : 0.0),
          _maxVal(st7735_range_ok(minVal, maxVal) ? maxVal : 30.0),
          _barColor(barColor), _bgColor(bgColor),
          _borderColor(borderColor), _warnColor(warnColor),
          _warnThreshold(warnThreshold),
          _prevFillPix(-1), _prevColor(0xFFFF), _ticks(10), _visible(false) {}

    /* Установка диапазона измеряемой величины.
     * Конечность обоих значений проверяется через st7735_range_ok():
     *   • NaN:        v != v
     *   • ±Inf:       v - v != 0  (Inf - Inf = NaN)
     * Без этой проверки setRange(0, INFINITY) проходил бы тихо — Inf == Inf
     * делал NaN-тест ложным, и finite < +Inf делал >= тест ложным.
     *
     * ВНИМАНИЕ: метод НЕ перерисовывает виджет. Новая шкала вступит в силу
     * при следующем setValue() (или после явного draw()). Это сделано
     * намеренно: BarGauge обычно обновляется периодически, и принудительная
     * перерисовка при каждом setRange() вела бы к лишнему мерцанию. */
    void setRange(double minVal, double maxVal, double warnThreshold = 1e30) {
        if (!st7735_range_ok(minVal, maxVal)) return;
        _minVal = minVal;
        _maxVal = maxVal;
        if (warnThreshold == warnThreshold) _warnThreshold = warnThreshold;
        _prevFillPix = -1;
    }

    /* Число делений шкалы (0 = без риска/делений) */
    void setTicks(uint8_t ticks) { _ticks = ticks; }

    /* Полная отрисовка рамки, делений и фона */
    void draw() {
        if (_w < 4 || _h < 4) return;
        _tft.startWrite();
        _tft.fillRect(_x, _y, _w, _h, _bgColor);
        _tft.drawRect(_x, _y, _w, _h, _borderColor);

        drawTicks(0, _w - 2);
        _tft.endWrite();
        _prevFillPix = -1;
        _visible = true;
    }

    /* Обновление значения полосы без перерисовки всей рамки.
     * Геометрическая проверка _w < 4 || _h < 4 в начале дублирует draw(),
     * но нужна: если draw() отказался рисовать при слишком малых размерах,
     * _visible остаётся false, и без этой защиты setValue продолжил бы
     * рисовать полосу поверх пустой области. */
    void setValue(double val) {
        if (_w < 4 || _h < 4) return;
        if (!_visible) draw();

        if (val != val) val = _minVal;     /* NaN — заменяем на минимум, без <math.h> */
        if (val < _minVal) val = _minVal;
        if (val > _maxVal) val = _maxVal;

        int16_t innerW = _w - 2;
        int16_t innerH = _h - 2;
        if (innerW <= 0 || innerH <= 0) return;

        double span = _maxVal - _minVal;
        if (span <= 0.0) span = 1.0;

        int16_t fillPix = (int16_t)( (val - _minVal) / span * innerW );
        if (fillPix < 0) fillPix = 0;
        if (fillPix > innerW) fillPix = innerW;

        uint16_t currentColor = (val >= _warnThreshold) ? _warnColor : _barColor;

        if (fillPix == _prevFillPix && currentColor == _prevColor) return;

        _tft.startWrite();

        if (_prevFillPix < 0) {
            /* первый draw — заливаем целиком */
            if (fillPix > 0)
                _tft.fillRect(_x + 1, _y + 1, fillPix, innerH, currentColor);
            if (innerW - fillPix > 0)
                _tft.fillRect(_x + 1 + fillPix, _y + 1, innerW - fillPix, innerH, _bgColor);
        } else if (currentColor != _prevColor) {
            /* смена цвета — заливаем fillPix новым цветом поверх старого.
             * НЕ очищаем предварительно — новый цвет непрозрачен. */
            if (fillPix > 0)
                _tft.fillRect(_x + 1, _y + 1, fillPix, innerH, currentColor);
            if (fillPix < _prevFillPix)
                _tft.fillRect(_x + 1 + fillPix, _y + 1, _prevFillPix - fillPix, innerH, _bgColor);
            else if (fillPix > _prevFillPix && innerW - fillPix > 0)
                _tft.fillRect(_x + 1 + fillPix, _y + 1, innerW - fillPix, innerH, _bgColor);
        } else {
            /* тот же цвет — только дельта */
            if (fillPix > _prevFillPix) {
                _tft.fillRect(_x + 1 + _prevFillPix, _y + 1, fillPix - _prevFillPix, innerH, currentColor);
            } else if (fillPix < _prevFillPix) {
                _tft.fillRect(_x + 1 + fillPix, _y + 1, _prevFillPix - fillPix, innerH, _bgColor);
            }
        }
        /* Перерисовываем деления в изменённой зоне — стёрты заливкой. */
        if (_prevFillPix < 0 || currentColor != _prevColor)
            drawTicks(0, innerW);
        else
            drawTicks((fillPix < _prevFillPix) ? fillPix : _prevFillPix,
                      (fillPix > _prevFillPix) ? fillPix : _prevFillPix);

        _tft.endWrite();

        _prevFillPix = fillPix;
        _prevColor = currentColor;
    }

private:
    void drawTicks(int16_t from, int16_t to) {
        if (_ticks < 2) return;
        int16_t innerW = _w - 2;
        int16_t innerH = _h - 2;
        for (uint8_t i = 1; i < _ticks; i++) {
            int16_t off = (int16_t)((int32_t)innerW * i / _ticks);
            if (off >= from && off <= to)
                _tft.drawFastVLine(_x + 1 + off, _y + 1, innerH, _borderColor);
        }
    }

    ST7735 &_tft;
    int16_t _x, _y, _w, _h;
    double _minVal, _maxVal;
    uint16_t _barColor, _bgColor, _borderColor, _warnColor;
    double _warnThreshold;
    int16_t _prevFillPix;
    uint16_t _prevColor;
    uint8_t _ticks;
    bool _visible;
};
#endif /* ST7735_ENABLE_BARGAUGE */

#if ST7735_ENABLE_PLOTTER
/* ===== График напряжения / Плоттер (ST7735Plotter / Waveform) =====
 * График реального времени с поддержкой:
 *   - Кольцевого буфера измерений в милливольтах (всего ~240 байт RAM);
 *   - Автоматического выбора диапазона (Auto-Range) под уровень сигнала;
 *   - Отслеживания и сохранения пиковых значений (Min / Max / Peak-Hold / P-P);
 *   - Фоновой сетки (Grid).
 * Оверлейный текст (AUTO, P-P, Mx) ВЫНЕСЕН ИЗ КЛАССА — рисуйте его
 * отдельными ST7735Value над/под окном графика, используя геттеры
 * peakMin()/peakMax()/peakToPeak()/effMinVal()/effMaxVal(). Это убирает
 * мелькание в углу графика (раньше putStr5x7 рисовал поверх линии) и
 * сокращает код: ушли strcpy/strncat/st7735_fmtDouble/putStr5x7 из Plotter.
 */
#ifndef ST7735_PLOTTER_MAX_PTS
  #define ST7735_PLOTTER_MAX_PTS 120
#endif
/* _head и _count имеют тип uint8_t (1 байт RAM экономии), поэтому ёмкость
 * буфера ограничена 255. Если задать больше — индексы перестанут
 * согласовываться с размером массива _rawMv. На этапе компиляции падаем с
 * понятным сообщением вместо тихого переполнения в рантайме. */
#if ST7735_PLOTTER_MAX_PTS > 255 || ST7735_PLOTTER_MAX_PTS < 2
  #error "ST7735_PLOTTER_MAX_PTS must be in [2, 255] (uint8_t _head/_count)"
#endif

/* Ширина ПОЛОСЫ рендера графика (px), она же размер static-буферов lo[]/hi[]
 * в redrawGraph. График ШИРЕ этого значения больше не обрезается: поле
 * рисуется несколькими полосами (одно окно setAddrWindow на полосу), RAM
 * не растёт. Дефолт 160 = одна полоса для ST7735 160x128; на 320x240 будет
 * 2 полосы, на 480x320 — 3. Увеличьте перед #include, если лишние окна
 * критичнее лишней RAM (каждые +1 к ширине полосы = +2 Б static-RAM). */
#ifndef ST7735_PLOTTER_MAX_W
  #define ST7735_PLOTTER_MAX_W  160
#endif

/* Инкрементальная перерисовка графика: кэш предыдущего кадра (lo/hi по
 * колонкам) — перерисовываются ТОЛЬКО изменившиеся колонки, и только их
 * вертикальный диапазон union(старое lo/hi, новое lo/hi). Для медленного
 * сигнала это единицы процентов полного кадра; для быстрого — плавная
 * деградация до полного кадра (не хуже текущего).
 * RAM-цена: +2*ST7735_PLOTTER_MAX_W байт static (320 Б при 160).
 * На AVR с 2 КБ SRAM по умолчанию ВЫКЛ (0), на остальных платформах ВКЛ.
 * Ограничение: если между addValue рисовать что-то поверх графика,
 * кэш устареет — после этого вызовите draw(). */
#ifndef ST7735_PLOTTER_INCREMENTAL
  #if defined(__AVR__)
    #define ST7735_PLOTTER_INCREMENTAL  0
  #else
    #define ST7735_PLOTTER_INCREMENTAL  1
  #endif
#endif

class ST7735Plotter {
public:
    ST7735Plotter(ST7735 &tft, int16_t x, int16_t y, int16_t w, int16_t h,
                  double minVal = 0.0, double maxVal = 30.0,
                  uint16_t lineColor = ST7735_YELLOW,
                  uint16_t bgColor = ST7735_BLACK,
                  uint16_t gridColor = 0x2104,
                  uint16_t borderColor = ST7735_WHITE)
        : _tft(tft), _x(x), _y(y), _w(w), _h(h),
          _minVal(st7735_range_ok(minVal, maxVal) ? minVal : 0.0),
          _maxVal(st7735_range_ok(minVal, maxVal) ? maxVal : 30.0),
          _lineColor(lineColor), _bgColor(bgColor),
          _gridColor(gridColor), _borderColor(borderColor),
          _autoRange(false),
          _peakMinVal(1e9), _peakMaxVal(-1e9),
          _effMinVal(st7735_range_ok(minVal, maxVal) ? minVal : 0.0),
          _effMaxVal(st7735_range_ok(minVal, maxVal) ? maxVal : 30.0),
          _head(0), _count(0), _visible(false) {
        memset(_rawMv, 0, sizeof(_rawMv));
    }

    /* Конечность обоих значений проверяется через st7735_range_ok():
     *   • NaN:        v != v
     *   • ±Inf:       v - v != 0
     * Старая проверка (minVal >= maxVal) пропускала setRange(0, +Inf),
     * т.к. finite < +Inf делал >= ложным. После setRange() вызывается clear(),
     * что эквивалентно перерисовке с пустым буфером — для Plotter это
     * безопасно (в отличие от BarGauge). */
    void setRange(double minVal, double maxVal) {
        if (!st7735_range_ok(minVal, maxVal)) return;
        _minVal = minVal;
        _maxVal = maxVal;
        if (!_autoRange) {
            _effMinVal = minVal;
            _effMaxVal = maxVal;
        }
        clear();
    }

    void setAutoRange(bool enable) {
        _autoRange = enable;
        if (!_autoRange) {
            _effMinVal = _minVal;
            _effMaxVal = _maxVal;
        }
        if (_visible) redrawGraph();
    }

    bool autoRange() const { return _autoRange; }

    /* Эффективный диапазон (с учётом Auto-Range). Для внешних надписей —
     * например, ST7735Value над окном графика: "AUTO 0.0-30.0V". */
    double effMinVal() const { return _effMinVal; }
    double effMaxVal() const { return _effMaxVal; }

    void resetPeakHold() {
        _peakMinVal = 1e9;
        _peakMaxVal = -1e9;
        if (_visible) redrawGraph();
    }

    double peakMin() const { return (_peakMinVal < 1e8) ? _peakMinVal : 0.0; }
    double peakMax() const { return (_peakMaxVal > -1e8) ? _peakMaxVal : 0.0; }
    double peakToPeak() const {
        if (_peakMinVal < 1e8 && _peakMaxVal > -1e8) {
            double p2p = _peakMaxVal - _peakMinVal;
            return (p2p > 0.0) ? p2p : 0.0;
        }
        return 0.0;
    }

    void clear() {
        _head = 0;
        _count = 0;
        memset(_rawMv, 0, sizeof(_rawMv));
        _peakMinVal = 1e9;
        _peakMaxVal = -1e9;
        if (_visible) redrawGraph();     /* только внутренность, рамка остаётся */
    }

    /* Показать окно: рамка + график. Внутренность (фон, сетка, линия) пишется
     * ОДНИМ потоком — предварительная заливка fillRect не нужна, мерцания нет. */
    void show() {
        if (_w < 10 || _h < 10) return;
        _tft.startWrite();
        _tft.drawRect(_x, _y, _w, _h, _borderColor);
        _visible = true;
        redrawGraph();
        _tft.endWrite();
        invalidate();   /* экран перезаписан — кэш кадра устарел */
    }

    /* Принудительный сброс кэша инкрементального вывода — БЕЗ перерисовки.
     * Вызывайте, если поверх графика что-то нарисовали извне (текст, другой
     * виджет, fillRect...): иначе следующий addValue посчитает разницу с
     * устаревшим кэшем и оставит артефакты. После invalidate() ближайший
     * addValue выведет полный кадр (фон+сетка+линия) и затрёт их.
     * Восстанавливается внутреннее ПОЛЕ графика; если внешняя отрисовка
     * задела рамку — используйте draw() (он перерисует и рамку).
     * O(1), нулевой SPI-трафик. Работает и при ST7735_PLOTTER_INCREMENTAL=0
     * (тогда просто ничего не делает). */
    void invalidate() {
#if ST7735_PLOTTER_INCREMENTAL
        incFrame().valid = false;
#endif
    }
    void draw() { show(); }                       /* совместимость */

    /* Скрыть: данные продолжают копиться в addValue(), но НА ЭКРАН не выводятся
     * (нужно при переключении на другой экран — иначе график рисовался бы
     * поверх чужого экрана). */
    void hide() { _visible = false; }
    bool isVisible() const { return _visible; }

    /* Добавление новой точки на график и сдвиг назад.
     *
     * redrawGraph пишет каждый пиксель один раз (мерцания нет), а при
     * включённом ST7735_PLOTTER_INCREMENTAL — только изменившиеся колонки
     * и только их вертикальный диапазон: для медленного сигнала трафик
     * падает в разы-десятки раз.
     */
    void addValue(double val) {
        if (_w < 10 || _h < 10) return;
        /* Работает и при скрытом окне: сбор данных не прерывается. */

        if (val != val) val = _minVal;     /* NaN — заменяем на минимум */

        /* Буфер хранит только беззнаковые 0..65535 мВ. Ограничиваем ДО
         * преобразования, чтобы Inf/очень большие значения не попадали
         * в lround() и не вызывали неопределённое/непредставимое преобразование. */
        if (val < 0.0) val = 0.0;
        if (val > 65.535) val = 65.535;

        // Отслеживание глобальных пиков Peak-Hold в пределах хранимого диапазона
        if (val < _peakMinVal) _peakMinVal = val;
        if (val > _peakMaxVal) _peakMaxVal = val;

        int16_t innerW = _w - 2;
        uint8_t maxCap = (innerW > ST7735_PLOTTER_MAX_PTS) ? ST7735_PLOTTER_MAX_PTS : (uint8_t)innerW;

        // Округление до ближайшего мВ без зависимости от libm/lround.
        uint16_t mv = (uint16_t)(val * 1000.0 + 0.5);

        _rawMv[_head] = (uint16_t)mv;
        _head = (_head + 1) % maxCap;
        if (_count < maxCap) _count++;

        // Расчёт эффективного диапазона при Auto-Range
        if (_autoRange && _count > 0) {
            uint16_t minMv = 65535, maxMv = 0;
            uint8_t startIdx = (_count < maxCap) ? 0 : _head;
            for (uint8_t i = 0; i < _count; i++) {
                uint8_t idx = (startIdx + i) % maxCap;
                uint16_t v = _rawMv[idx];
                if (v < minMv) minMv = v;
                if (v > maxMv) maxMv = v;
            }
            // Гарантируем минимальный диапазон 200 мВ, чтобы шум не раздувал шкалу
            if ((uint32_t)maxMv - (uint32_t)minMv < 200U) {
                uint32_t mid = ((uint32_t)minMv + (uint32_t)maxMv) / 2U;
                uint32_t low = (mid > 100U) ? (mid - 100U) : 0U;
                uint32_t high = low + 200U;
                if (high > 65535U) {
                    high = 65535U;
                    low = high - 200U;
                }
                minMv = (uint16_t)low;
                maxMv = (uint16_t)high;
            }
            _effMinVal = minMv / 1000.0;
            _effMaxVal = maxMv / 1000.0;
        } else if (!_autoRange) {
            _effMinVal = _minVal;
            _effMaxVal = _maxVal;
        }

        if (_visible) redrawGraph();
    }

private:
#if ST7735_PLOTTER_INCREMENTAL
    /* ---- Кэш предыдущего кадра для инкрементального вывода ----
     * Колонка графика полностью определяется парой (lo,hi) — если lo/hi
     * колонки не изменились, её пиксели на экране уже верны. Кэш хранит
     * lo/hi того, что РЕАЛЬНО на экране, и подпись кадра (геометрия,
     * диапазон, цвета, _count — влияет на раскладку точек по колонкам).
     * Кэшируется состояние экрана в позиции (x,y): для нескольких
     * плоттеров корректно; смена подписи = один полный кадр.
     * Общий для всех экземпляров (RAM!), не реентерабело. */
    struct IncFrame {
        uint8_t lo[ST7735_PLOTTER_MAX_W];
        uint8_t hi[ST7735_PLOTTER_MAX_W];
        int16_t x, y, w, h;
        int32_t minMv, spanMv;
        uint8_t count;
        uint16_t lineC, bgC, gridC;
        bool valid;
    };
    static IncFrame& incFrame() { static IncFrame f; return f; }
#endif

    /* Буфер НОВОГО кадра (lo/hi по колонкам) — static, размером MAX_W. */
    struct PlotBuf { uint8_t lo[ST7735_PLOTTER_MAX_W]; uint8_t hi[ST7735_PLOTTER_MAX_W]; };
    static PlotBuf& curBuf() { static PlotBuf b; return b; }

#if ST7735_PLOTTER_INCREMENTAL
    bool incSigOk(int16_t x, int16_t y, int16_t w, int16_t h,
                  int32_t minMv, int32_t spanMv, uint8_t count) const {
        const IncFrame &f = incFrame();
        return f.valid && f.x == x && f.y == y && f.w == w && f.h == h &&
               f.minMv == minMv && f.spanMv == spanMv && f.count == count &&
               f.lineC == _lineColor && f.bgC == _bgColor && f.gridC == _gridColor;
    }

    void incSave(int16_t x, int16_t y, int16_t w, int16_t h,
                 int32_t minMv, int32_t spanMv, uint8_t count) {
        IncFrame &f = incFrame();
        memcpy(f.lo, curBuf().lo, sizeof(f.lo));
        memcpy(f.hi, curBuf().hi, sizeof(f.hi));
        f.x = x; f.y = y; f.w = w; f.h = h;
        f.minMv = minMv; f.spanMv = spanMv; f.count = count;
        f.lineC = _lineColor; f.bgC = _bgColor; f.gridC = _gridColor;
        f.valid = true;
    }

    /* Вывод одного прогона подряд идущих изменившихся колонок:
     * колонки colBase..colBase+wCols-1, строки r0..r1. */
    void drawRun(int16_t x0, int16_t innerY, int16_t wCols,
                 int16_t r0, int16_t r1, int16_t colBase,
                 const int16_t *gx, const int16_t *gy) {
        const uint8_t *lo = curBuf().lo;
        const uint8_t *hi = curBuf().hi;
        _tft.startWrite();
        _tft.setAddrWindow(x0, (int16_t)(innerY + r0),
                           (int16_t)(x0 + wCols - 1),
                           (int16_t)(innerY + r1));
        /* Байты RGB565 формируем прямо в буфере (старший байт первым) —
         * минуя промежуточный uint16_t col[] и второй проход writePixels. */
        uint8_t col[ST7735_CHUNK * 2];
        for (int16_t r = r0; r <= r1; r++) {
            const bool grow = (r == gy[0] || r == gy[1] || r == gy[2]);
            for (int16_t cx0 = 0; cx0 < wCols; cx0 += ST7735_CHUNK) {
                int16_t n = wCols - cx0;
                if (n > (int16_t)ST7735_CHUNK) n = ST7735_CHUNK;
                for (int16_t i = 0; i < n; i++) {
                    const int16_t cx = colBase + cx0 + i;
                    uint16_t c;
                    if (lo[cx] <= hi[cx] && r >= lo[cx] && r <= hi[cx])
                        c = _lineColor;
                    else if (grow || cx == gx[0] || cx == gx[1] || cx == gx[2])
                        c = _gridColor;
                    else
                        c = _bgColor;
                    col[2 * i]     = (uint8_t)(c >> 8);
                    col[2 * i + 1] = (uint8_t)c;
                }
                _tft.writePixelsBytes(col, (uint32_t)n);
            }
        }
        _tft.endWrite();
    }

    /* Сравниваем новый кадр с кэшем, группируем изменившиеся колонки в
     * прогоны, для каждого прогона — объединённый вертикальный span
     * union(старое, новое) и одно окно вывода. */
    void incDrawRuns(int16_t innerX, int16_t innerY, int16_t innerW,
                     const int16_t *gx, const int16_t *gy) {
        const uint8_t *lo = curBuf().lo;
        const uint8_t *hi = curBuf().hi;
        const IncFrame &f = incFrame();
        int16_t run0 = -1;
        int16_t spanLo = 0, spanHi = -1;
        for (int16_t c = 0; c <= innerW; c++) {
            bool changed = false;
            int16_t clo = 0, chi = -1;
            if (c < innerW && (lo[c] != f.lo[c] || hi[c] != f.hi[c])) {
                changed = true;
                clo = 32767; chi = -32768;
                if (lo[c] <= hi[c]) {
                    if (lo[c] < clo) clo = lo[c];
                    if (hi[c] > chi) chi = hi[c];
                }
                if (f.lo[c] <= f.hi[c]) {
                    if (f.lo[c] < clo) clo = f.lo[c];
                    if (f.hi[c] > chi) chi = f.hi[c];
                }
            }
            if (changed) {
                if (run0 < 0) { run0 = c; spanLo = clo; spanHi = chi; }
                else {
                    if (clo < spanLo) spanLo = clo;
                    if (chi > spanHi) spanHi = chi;
                }
            } else if (run0 >= 0) {
                drawRun((int16_t)(innerX + run0), innerY, (int16_t)(c - run0),
                        spanLo, spanHi, run0, gx, gy);
                run0 = -1;
            }
        }
    }
#endif
    /* ---- Безмерцательная перерисовка (поток по строкам, полосами) ----
     * Раньше: column-strip — innerW раз открывалось setAddrWindow. Теперь:
     * ОДИН setAddrWindow на ПОЛОСУ шириной ST7735_PLOTTER_MAX_W (график
     * шире — несколькими полосами, без обрезания), затем пиксели идут потоком
     * по строкам чанками по ST7735_CHUNK. SPI-накладные на адреса сокращены
     * с innerW команд до ceil(innerW / MAX_W).
     *
     * Алгоритм:
     *   1) Для каждой полосы [sx0..sx1] предвычисляем lo/hi — диапазон строк,
     *      занятых линией в колонках полосы. Буферы static размером MAX_W.
     *   2) Открываем окно полосы innerX+sx0..innerX+sx1 × innerY..innerY+innerH-1.
     *   3) Для каждой строки r и каждой колонки cx определяем итоговый цвет:
     *        линия (r в [lo,hi]) > сетка (gy или gx) > фон.
     *      Пишем чанками по ST7735_CHUNK пикселей в одну SPI-транзакцию.
     *
     * Оверлея нет — он вынесен из класса (см. комментарий к классу). */
    static int32_t divRound(int32_t num, int32_t den) {
        return (num >= 0) ? (num + den / 2) / den : -((-num + den / 2) / den);
    }

    int16_t pointY(uint8_t k, uint8_t startIdx, uint8_t maxCap,
                   int32_t minMv, int32_t spanMv, int16_t innerH) const {
        int32_t mv = _rawMv[(uint8_t)((startIdx + k) % maxCap)];
        int32_t yRel = (mv - minMv) * (innerH - 1) / spanMv;
        if (yRel < 0) yRel = 0;
        if (yRel > innerH - 1) yRel = innerH - 1;
        return (int16_t)(innerH - 1 - yRel);          /* относительно innerY */
    }

    /* Диапазон строк, занятых отрезком (xs,ys)-(xe,ye) в колонке cx.
     * Записывает в lo/hi (через min/max). */
    static void segExtent(int16_t cx, int16_t xs, int16_t ys, int16_t xe, int16_t ye,
                          uint8_t &lo, uint8_t &hi) {
        int32_t dx = (int32_t)xe - xs;
        int16_t a, b;
        if (dx <= 0) { a = ys; b = ye; }
        else {
            int32_t den = 2 * dx, dy = (int32_t)ye - ys;
            int32_t t0 = 2 * ((int32_t)cx - xs) - 1, t1 = t0 + 2;
            if (t0 < 0) t0 = 0; else if (t0 > den) t0 = den;
            if (t1 < 0) t1 = 0; else if (t1 > den) t1 = den;
            a = (int16_t)(ys + divRound(dy * t0, den));
            b = (int16_t)(ys + divRound(dy * t1, den));
        }
        if (a > b) { int16_t t = a; a = b; b = t; }
        if (a < 0) a = 0;
        if (b > 255) b = 255;
        if ((uint8_t)a < lo) lo = (uint8_t)a;
        if ((uint8_t)b > hi) hi = (uint8_t)b;
    }

    void redrawGraph() {
        const int16_t innerX = _x + 1;
        const int16_t innerY = _y + 1;
        const int16_t innerH = _h - 2;
        int16_t innerW = _w - 2;               /* шире MAX_W — рисуем полосами */
        if (innerW <= 0 || innerH <= 0) return;

        const uint8_t maxCap = (innerW > ST7735_PLOTTER_MAX_PTS) ? ST7735_PLOTTER_MAX_PTS : (uint8_t)innerW;
        const uint8_t startIdx = (_count < maxCap) ? 0 : _head;

        /* Округление к ближайшему с корректной обработкой отрицательных. */
        double vMin = _effMinVal * 1000.0;
        double vSpan = (_effMaxVal - _effMinVal) * 1000.0;
        int32_t minMv  = (int32_t)(vMin  + (vMin  >= 0 ? 0.5 : -0.5));
        int32_t spanMv = (int32_t)(vSpan + (vSpan >= 0 ? 0.5 : -0.5));
        if (spanMv <= 0) spanMv = 1000;

        /* Предвычисляем lo/hi и пишем поток по строкам ПОЛОСАМИ по
         * ST7735_PLOTTER_MAX_W колонок: static-буферы не растут с шириной
         * графика, а ширина больше MAX_W больше не обрезается — поле
         * рисуется несколькими полосами (одно окно на полосу). */
        uint8_t *loArr = curBuf().lo;
        uint8_t *hiArr = curBuf().hi;

        /* Координаты сетки (по полной ширине поля): 3 вертикальные + 3 горизонтальные. */
        int16_t gx[3], gy[3];
        for (uint8_t i = 0; i < 3; i++) {
            gx[i] = (int16_t)((int32_t)innerW * (i + 1) / 4);
            gy[i] = (int16_t)((int32_t)innerH * (i + 1) / 4);
        }

        for (int16_t sx0 = 0; sx0 < innerW; sx0 += ST7735_PLOTTER_MAX_W) {
            const int16_t sw = (innerW - sx0 > ST7735_PLOTTER_MAX_W)
                              ? (int16_t)ST7735_PLOTTER_MAX_W
                              : (int16_t)(innerW - sx0);
            const int16_t sx1 = sx0 + sw - 1;

            /* Инициализация полосы: lo=255, hi=0 → lo > hi означает «линии нет». */
            for (int16_t i = 0; i < sw; i++) { loArr[i] = 255; hiArr[i] = 0; }

            if (_count >= 2) {
                const int32_t cm1 = (int32_t)_count - 1;
                int16_t prevX = 0;
                int16_t prevY = pointY(0, startIdx, maxCap, minMv, spanMv, innerH);
                for (uint8_t i = 1; i < _count; i++) {
                    int16_t curX = (int16_t)((int32_t)i * (innerW - 1) / cm1);
                    int16_t curY = pointY(i, startIdx, maxCap, minMv, spanMv, innerH);
                    /* Колонки отрезка, пересекающие полосу, — обновить lo/hi. */
                    int16_t xa = (prevX < curX) ? prevX : curX;
                    int16_t xb = (prevX < curX) ? curX : prevX;
                    if (xb >= sx0 && xa <= sx1) {
                        int16_t cxa = (xa > sx0) ? xa : sx0;
                        int16_t cxb = (xb < sx1) ? xb : sx1;
                        for (int16_t cx = cxa; cx <= cxb; cx++) {
                            segExtent(cx, prevX, prevY, curX, curY,
                                      loArr[cx - sx0], hiArr[cx - sx0]);
                        }
                    }
                    prevX = curX;
                    prevY = curY;
                }
            }

#if ST7735_PLOTTER_INCREMENTAL
            /* Инкрементальный путь: только для одной полосы (кэш размером
             * MAX_W). Подпись совпала — на экране именно прошлый кадр этого
             * плоттера, выводим только изменившиеся колонки. */
            bool inc = (innerW <= ST7735_PLOTTER_MAX_W) &&
                       incSigOk(innerX, innerY, innerW, innerH, minMv, spanMv, _count);
            if (inc) {
                incDrawRuns(innerX, innerY, innerW, gx, gy);
            } else
#endif
            {
            /* ОДНО setAddrWindow на полосу. Дальше — поток по строкам. */
            _tft.startWrite();
            _tft.setAddrWindow((int16_t)(innerX + sx0), innerY,
                               (int16_t)(innerX + sx1),
                               (int16_t)(innerY + innerH - 1));

            /* Прямое формирование байтов RGB565 (MSB-first), без col[uint16_t]. */
            uint8_t col[ST7735_CHUNK * 2];
            for (int16_t r = 0; r < innerH; r++) {
                const bool grow = (r == gy[0] || r == gy[1] || r == gy[2]);
                for (int16_t cx0 = 0; cx0 < sw; cx0 += ST7735_CHUNK) {
                    int16_t n = sw - cx0;
                    if (n > (int16_t)ST7735_CHUNK) n = ST7735_CHUNK;
                    for (int16_t i = 0; i < n; i++) {
                        const int16_t cx = sx0 + cx0 + i;   /* абсолютная колонка — для сетки */
                        uint16_t c;
                        if (loArr[cx0 + i] <= hiArr[cx0 + i] &&
                            r >= loArr[cx0 + i] && r <= hiArr[cx0 + i])
                            c = _lineColor;
                        else if (grow || cx == gx[0] || cx == gx[1] || cx == gx[2])
                            c = _gridColor;
                        else
                            c = _bgColor;
                        col[2 * i]     = (uint8_t)(c >> 8);
                        col[2 * i + 1] = (uint8_t)c;
                    }
                    _tft.writePixelsBytes(col, (uint32_t)n);
                }
            }

            _tft.endWrite();
            }

#if ST7735_PLOTTER_INCREMENTAL
            /* Новый кадр теперь на экране — обновляем кэш (только одна полоса). */
            if (innerW <= ST7735_PLOTTER_MAX_W)
                incSave(innerX, innerY, innerW, innerH, minMv, spanMv, _count);
#endif
        }
    }

    ST7735 &_tft;
    int16_t _x, _y, _w, _h;
    double _minVal, _maxVal;
    uint16_t _lineColor, _bgColor, _gridColor, _borderColor;
    bool _autoRange;
    double _peakMinVal;
    double _peakMaxVal;
    double _effMinVal;
    double _effMaxVal;
    uint16_t _rawMv[ST7735_PLOTTER_MAX_PTS]; // Кольцевой буфер точек в мВ (240 Б)
    uint8_t _head;
    uint8_t _count;
    bool _visible;
};
#endif /* ST7735_ENABLE_PLOTTER */

#if ST7735_ENABLE_LOG
/* ===== Журнал сообщений (ST7735Log) =====
 * Кольцо из ST7735_LOG_LINES строк по ST7735_LOG_COLS символов (+ цвет строки).
 * add() работает и при скрытом окне — сообщения копятся, на экран попадают
 * при show(). Перерисовка без мерцания: каждая строка пишется непрозрачно
 * (drawStringAligned заливает свою ячейку и поля), общая заливка — только в show().
 * RAM: LINES*(COLS+3) байт (по умолчанию 8*(30+3) = 264 Б). */
#ifndef ST7735_LOG_LINES
  #define ST7735_LOG_LINES 8
#endif
#ifndef ST7735_LOG_COLS
  #define ST7735_LOG_COLS 30
#endif
class ST7735Log {
public:
    ST7735Log(ST7735 &tft, int16_t x, int16_t y, int16_t w, int16_t h,
              uint16_t fg = ST7735_WHITE, uint16_t bg = ST7735_BLACK, uint8_t scale = 1)
        : _tft(tft), _x(x), _y(y), _w(w), _h(h), _fg(fg), _bg(bg),
          _scale(scale ? scale : 1), _total(0), _visible(false) {
        memset(_txt, 0, sizeof(_txt));
    }

    /* fg = 0 → цвет по умолчанию (для чёрного текста на белом используйте 0x0001) */
    void add(const char *s, uint16_t fg = 0) {
        if (!s) return;
        const uint8_t slot = (uint8_t)(_total % ST7735_LOG_LINES);
        size_t n = strlen(s);
        if (n > ST7735_LOG_COLS) {
            n = ST7735_LOG_COLS;
            while (n > 0 && (((uint8_t)s[n]) & 0xC0) == 0x80) n--;   /* не рвём UTF-8 */
        }
        memcpy(_txt[slot], s, n);
        _txt[slot][n] = 0;
        _col[slot] = fg ? fg : _fg;
        const uint16_t before = _total;
        _total++;
        if (!_visible || !_tft.font()) return;
        const uint8_t rows = visibleRows();
        if (rows == 0) return;
        if (before < rows) drawRow((uint8_t)before, slot);   /* ещё не скроллится: одна строка */
        else drawAll();                                       /* сдвиг: все строки, непрозрачно */
    }

    void clear() {
        _total = 0;
        memset(_txt, 0, sizeof(_txt));
        if (_visible) show();
    }

    void show() {
        if (_w <= 0 || _h <= 0) return;
        _tft.startWrite();
        _tft.fillRect(_x, _y, _w, _h, _bg);
        _visible = true;
        if (_tft.font()) drawAll();
        _tft.endWrite();
    }
    void hide() { _visible = false; }
    bool isVisible() const { return _visible; }
    uint16_t total() const { return _total; }

private:
    uint8_t visibleRows() const {
        const uint16_t lh = _tft.lineHeight(_scale);
        if (!lh) return 0;
        uint16_t r = (uint16_t)_h / lh;
        return (r > ST7735_LOG_LINES) ? (uint8_t)ST7735_LOG_LINES : (uint8_t)r;
    }
    void drawRow(uint8_t row, uint8_t slot) {
        const int16_t y = _y + (int16_t)(row * _tft.lineHeight(_scale));
        _tft.drawStringAligned(_txt[slot], _x, y, _w, ST7735_ALIGN_LEFT,
                               _col[slot], _bg, _scale);
    }
    void drawAll() {
        const uint8_t rows = visibleRows();
        if (!rows) return;
        _tft.startWrite();
        const uint16_t first = (_total > rows) ? (uint16_t)(_total - rows) : 0;
        for (uint8_t k = 0; k < rows; k++) {
            const uint16_t ln = first + k;
            if (ln >= _total) break;                       /* пустой хвост уже залит в show() */
            drawRow(k, (uint8_t)(ln % ST7735_LOG_LINES));
        }
        _tft.endWrite();
    }

    ST7735 &_tft;
    int16_t _x, _y, _w, _h;
    uint16_t _fg, _bg;
    uint8_t _scale;
    uint16_t _total;
    bool _visible;
    char _txt[ST7735_LOG_LINES][ST7735_LOG_COLS + 1];
    uint16_t _col[ST7735_LOG_LINES];
};
#endif /* ST7735_ENABLE_LOG */

#if ST7735_ENABLE_SCREENS
/* ===== Переключатель экранов (ST7735Screens) =====
 * Циклически переключает N экранов: main → график → сообщения → main …
 * Каждый экран — три функции-колбэка (без std::function, работает на AVR):
 *   onEnter(ctx)  — нарисовать статику (рамки, подписи), show() виджетов;
 *   onUpdate(ctx) — периодическое обновление (вызывается из update());
 *   onLeave(ctx)  — hide() виджетов (сбор данных при этом продолжается).
 * При переключении экран один раз заливается bg, затем зовётся onEnter. */
#ifndef ST7735_MAX_SCREENS
  #define ST7735_MAX_SCREENS 4
#endif
typedef void (*ST7735ScreenFn)(void *ctx);

class ST7735Screens {
public:
    explicit ST7735Screens(ST7735 &tft, uint16_t bg = ST7735_BLACK)
        : _tft(tft), _bg(bg), _n(0), _cur(0), _started(false) {}

    /* Возвращает индекс экрана или -1, если места нет. */
    int8_t add(ST7735ScreenFn onEnter, ST7735ScreenFn onUpdate = 0,
               ST7735ScreenFn onLeave = 0, void *ctx = 0) {
        if (_n >= ST7735_MAX_SCREENS) return -1;
        _s[_n].enter = onEnter; _s[_n].update = onUpdate;
        _s[_n].leave = onLeave; _s[_n].ctx = ctx;
        return (int8_t)_n++;
    }

    void select(uint8_t i) {
        if (_n == 0 || i >= _n) return;
        if (_started && _s[_cur].leave) _s[_cur].leave(_s[_cur].ctx);
        _cur = i;
        _started = true;
        _tft.fillScreen(_bg);
        if (_s[_cur].enter) _s[_cur].enter(_s[_cur].ctx);
    }
    void begin(uint8_t first = 0) { select(first); }
    void next() { select(_started ? (uint8_t)((_cur + 1) % _n) : 0); }
    void prev() { select(_started ? (uint8_t)((_cur + _n - 1) % _n) : 0); }

    /* Вызывать из loop() */
    void update() {
        if (_started && _s[_cur].update) _s[_cur].update(_s[_cur].ctx);
    }

    uint8_t current() const { return _cur; }
    uint8_t count()   const { return _n; }
    bool    is(uint8_t i) const { return _started && _cur == i; }

private:
    struct Entry { ST7735ScreenFn enter, update, leave; void *ctx; };
    ST7735 &_tft;
    uint16_t _bg;
    Entry _s[ST7735_MAX_SCREENS];
    uint8_t _n, _cur;
    bool _started;
};
#endif /* ST7735_ENABLE_SCREENS */
