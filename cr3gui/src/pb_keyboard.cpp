// On-screen touch keyboard drawn by CoolReader on PocketBook. See pb_keyboard.h.
#include "pb_keyboard.h"

#include <inkview.h>
#include <cri18n.h>
#include <lvfntman.h>
#include <string.h>

#include "mainwnd.h"

extern V3DocViewWin * main_win;
extern lString32 currentLang;

namespace {

// widths in quarters of a letter key; every layout is laid out on 13 letter keys, so the keyboard
// keeps its size when the layout changes
const int LETTER = 4;
const int WIDE = 6;
const int ROW_UNITS = 13 * LETTER;
const int MAX_TEXT = 100;

const char * const LETTERS[3][3] = {
    { "qwertyuiop", "asdfghjkl", "zxcvbnm" },
    { "йцукенгшщзхъ", "фывапролджэ", "ячсмитьбюё" },
    { "йцукенгшщзхї", "фівапролджє", "ячсмитьбюґ" },
};
const char * const SYMBOLS[3] = { "1234567890", "-/:;()&@\".", ",?!'*+=#_%" };
const char * const LAYOUT_NAMES[3] = { "EN", "RU", "UK" };
const char * const LETTERS_NAMES[3] = { "ABC", "АБВ", "АБВ" };

lString32 u16(const char * s)
{
    return Utf8ToUnicode(lString8(s));
}

// the layout for a text: Ukrainian or Russian letters in it, otherwise the language of the interface
int layoutFor(const lString32 & text)
{
    bool cyrillic = false;
    for (int i = 0; i < text.length(); i++) {
        lChar32 c = text[i];
        if (c == 0x0456 || c == 0x0457 || c == 0x0454 || c == 0x0491 ||
            c == 0x0406 || c == 0x0407 || c == 0x0404 || c == 0x0490)
            return 2; // і ї є ґ
        if (c >= 0x0400 && c <= 0x04FF)
            cyrillic = true;
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
            return 0;
    }
    if (cyrillic)
        return 1;
    return -1;
}

} // namespace

bool pbKeyboardUsable()
{
    return QueryTouchpanel() != 0;
}

PbKeyboard::PbKeyboard(CRGUIWindowManager * wm, const char * title, char * buffer, int maxlen,
                       void (*handler)(char *), bool inverted)
    : CRGUIWindowBase(wm), _buffer(buffer), _maxlen(maxlen), _handler(handler), _inverted(inverted),
      _textSelected(false), _layout(0), _symbols(false), _shift(false), _pressed(-1), _line(1)
{
    _title = title != NULL ? Utf8ToUnicode(lString8(title)) : lString32::empty_str;
    _text = Utf8ToUnicode(lString8(buffer != NULL ? buffer : ""));
    _textSelected = !_text.empty();
    _fullscreen = false;
    setAccelerators(_wm->getAccTables().get("dialog"));
    // the layout of the word being edited, then the last one used, then the interface language
    int layout = layoutFor(_text);
    if (layout < 0 && main_win != NULL)
        layout = main_win->getProps()->getIntDef(PROP_POCKETBOOK_KEYBOARD_LAYOUT, -1);
    if (layout < 0 || layout >= LAYOUT_COUNT)
        layout = currentLang.startsWith(lString32("uk")) ? LAYOUT_UK
                 : currentLang.startsWith(lString32("ru")) ? LAYOUT_RU : LAYOUT_EN;
    _layout = layout;
    build();
}

void PbKeyboard::addRow(const lString32 & letters, int row, bool shiftRow, int rows, int keyHeight,
                        int unitWidth, int maxUnits, const lvRect & area, std::vector<Key> & keys)
{
    (void)rows;
    int units = letters.length() * LETTER + (shiftRow ? 2 * WIDE : 0);
    int x = area.left + (maxUnits - units) * unitWidth / 2;
    int top = area.top + row * keyHeight;
    Key k;
    if (shiftRow) {
        k.code = PBK_SHIFT;
        k.units = WIDE;
        k.rc = lvRect(x, top, x + WIDE * unitWidth, top + keyHeight);
        keys.push_back(k);
        x += WIDE * unitWidth;
    }
    for (int i = 0; i < letters.length(); i++) {
        k.code = letters[i];
        k.units = LETTER;
        k.rc = lvRect(x, top, x + LETTER * unitWidth, top + keyHeight);
        keys.push_back(k);
        x += LETTER * unitWidth;
    }
    if (shiftRow) {
        k.code = PBK_BACKSPACE;
        k.units = WIDE;
        k.rc = lvRect(x, top, x + WIDE * unitWidth, top + keyHeight);
        keys.push_back(k);
    }
}

void PbKeyboard::build()
{
    lvRect screen = _wm->getScreen()->getRect();
    int width = screen.width();
    int height = screen.height();
    int pad = width / 100 + 2;
    int unitWidth = (width - 2 * pad) / ROW_UNITS;
    int keyHeight = unitWidth * LETTER * 11 / 10;
    if (keyHeight > height / 10)
        keyHeight = height / 10;
    // text row, 4 rows of keys
    int total = pad + keyHeight + pad + 4 * keyHeight + pad;
    _rect = lvRect(0, height - total, width, height);
    lvRect area(pad, _rect.top + pad, pad + ROW_UNITS * unitWidth, _rect.bottom - pad);

    _keys.clear();
    // text row: Cancel and the text field
    Key k;
    k.code = PBK_CANCEL;
    k.units = 2 * WIDE;
    k.rc = lvRect(area.left, area.top, area.left + 2 * WIDE * unitWidth, area.top + keyHeight);
    _keys.push_back(k);
    _textRc = lvRect(k.rc.right + pad, area.top, area.right, area.top + keyHeight);

    lvRect keysArea(area.left, area.top + keyHeight + pad, area.right, area.bottom);
    if (_symbols) {
        addRow(u16(SYMBOLS[0]), 0, false, 4, keyHeight, unitWidth, ROW_UNITS, keysArea, _keys);
        addRow(u16(SYMBOLS[1]), 1, false, 4, keyHeight, unitWidth, ROW_UNITS, keysArea, _keys);
        addRow(u16(SYMBOLS[2]), 2, true, 4, keyHeight, unitWidth, ROW_UNITS, keysArea, _keys);
    } else {
        addRow(u16(LETTERS[_layout][0]), 0, false, 4, keyHeight, unitWidth, ROW_UNITS, keysArea, _keys);
        addRow(u16(LETTERS[_layout][1]), 1, false, 4, keyHeight, unitWidth, ROW_UNITS, keysArea, _keys);
        addRow(u16(LETTERS[_layout][2]), 2, true, 4, keyHeight, unitWidth, ROW_UNITS, keysArea, _keys);
    }
    // bottom row: 123 | layout | ' | space | - | OK
    static const int codes[] = { PBK_SYMBOLS, PBK_LAYOUT, '\'', PBK_SPACE, '-', PBK_OK };
    static const int widths[] = { WIDE, WIDE, LETTER, 4 * LETTER, LETTER, 2 * WIDE };
    int units = 0;
    for (int i = 0; i < 6; i++)
        units += widths[i];
    int x = keysArea.left + (ROW_UNITS - units) * unitWidth / 2;
    int top = keysArea.top + 3 * keyHeight;
    for (int i = 0; i < 6; i++) {
        k.code = codes[i];
        k.units = widths[i];
        k.rc = lvRect(x, top, x + widths[i] * unitWidth, top + keyHeight);
        _keys.push_back(k);
        x += widths[i] * unitWidth;
    }
    _keyFont = fontMan->GetFont(keyHeight * 45 / 100, 400, false, css_ff_sans_serif, lString8("Liberation Sans"));
    _keyBoldFont = fontMan->GetFont(keyHeight * 40 / 100, 700, false, css_ff_sans_serif, lString8("Liberation Sans"));
    _line = width / 600 + 1;
    _textFont = fontMan->GetFont(keyHeight * 50 / 100, 400, false, css_ff_sans_serif, lString8("Liberation Sans"));
    setDirty();
}

lString32 PbKeyboard::label(const Key & key) const
{
    switch (key.code) {
    case PBK_SHIFT: return lString32(U"\x2191");      // up arrow
    case PBK_BACKSPACE: return lString32(U"\x2190");  // left arrow
    case PBK_SPACE: return lString32::empty_str;
    case PBK_SYMBOLS: return _symbols ? u16(LETTERS_NAMES[_layout]) : lString32("123");
    case PBK_LAYOUT: return lString32(LAYOUT_NAMES[_layout]);
    case PBK_OK: return lString32("OK");
    case PBK_CANCEL: return lString32(_("Cancel"));
    default:
        break;
    }
    lChar32 c[2] = { (lChar32)key.code, 0 };
    lString32 s(c);
    if (_shift)
        s.uppercase();
    return s;
}

void PbKeyboard::drawTextField(LVDrawBuf & buf, bool invert)
{
    int line = _line;
    // text field: the text with the cursor; the starting text is selected (inverted)
    buf.FillRect(_textRc, 0xFFFFFF);
    buf.DrawFrame(_textRc, 0x000000, line);
    lvRect inner = _textRc;
    int textPad = _textFont->getHeight() / 3;
    inner.left += textPad;
    inner.right -= textPad;
    int y = inner.top + (inner.height() - _textFont->getHeight()) / 2;
    lString32 shown = _text;
    int caret = _textFont->getHeight() / 12 + 1;
    while (!shown.empty() && _textFont->getTextWidth(shown.c_str(), shown.length()) > inner.width() - 2 * caret)
        shown = shown.substr(1); // the end of a long text stays visible
    buf.SetBackgroundColor(0xFFFFFF);
    if (shown.empty() && !_title.empty()) {
        buf.SetTextColor(0x888888);
        _textFont->DrawTextString(&buf, inner.left, y, _title.c_str(), _title.length(), U'?', NULL, false, 0);
    }
    buf.SetTextColor(0x000000);
    int textWidth = _textFont->getTextWidth(shown.c_str(), shown.length());
    _textFont->DrawTextString(&buf, inner.left, y, shown.c_str(), shown.length(), U'?', NULL, false, 0);
    if (_textSelected && textWidth > 0)
        buf.InvertRect(inner.left - caret, y, inner.left + textWidth + caret, y + _textFont->getHeight());
    else
        buf.FillRect(inner.left + textWidth + caret, y, inner.left + textWidth + 2 * caret,
                     y + _textFont->getHeight(), 0x000000);
    // night mode: inverted like the rest of CoolReader
    if (invert)
        buf.InvertRect(_textRc.left, _textRc.top, _textRc.right, _textRc.bottom);
}

// Keys are black on white only: gray keys blink on e-ink when the screen is updated around them.
// The service keys have bold labels; Shift turned on is inverted.
void PbKeyboard::drawKey(LVDrawBuf & buf, int index, bool invert)
{
    const Key & k = _keys[index];
    buf.FillRect(k.rc, 0xFFFFFF);
    int gap = k.rc.height() / 14 + 1;
    lvRect rc = k.rc;
    rc.left += gap;
    rc.right -= gap;
    rc.top += gap;
    rc.bottom -= gap;
    buf.DrawFrame(rc, 0x000000, _line);
    lString32 l = label(k);
    if (!l.empty()) {
        LVFontRef font = k.code < 0 ? _keyBoldFont : _keyFont;
        buf.SetTextColor(0x000000);
        buf.SetBackgroundColor(0xFFFFFF);
        int w = font->getTextWidth(l.c_str(), l.length());
        font->DrawTextString(&buf, rc.left + (rc.width() - w) / 2, rc.top + (rc.height() - font->getHeight()) / 2,
                             l.c_str(), l.length(), U'?', NULL, false, 0);
    }
    bool dark = (k.code == PBK_SHIFT && _shift) != (index == _pressed);
    if (dark)
        buf.InvertRect(rc.left, rc.top, rc.right, rc.bottom);
    if (invert)
        buf.InvertRect(k.rc.left, k.rc.top, k.rc.right, k.rc.bottom);
}

void PbKeyboard::draw()
{
    LVDrawBuf & buf = *_wm->getScreen()->getCanvas();
    lvRect saveClip;
    buf.GetClipRect(&saveClip);
    buf.SetClipRect(&_rect);
    buf.FillRect(_rect, 0xFFFFFF);
    buf.FillRect(_rect.left, _rect.top, _rect.right, _rect.top + 2 * _line, 0x000000);
    drawTextField(buf, false);
    for (int i = 0; i < (int)_keys.size(); i++)
        drawKey(buf, i, false);
    // night mode: inverted like the rest of CoolReader
    if (_inverted)
        buf.InvertRect(_rect.left, _rect.top, _rect.right, _rect.bottom);
    buf.SetClipRect(&saveClip);
}

void PbKeyboard::refresh(int key, bool text)
{
    CRGUIScreen * screen = _wm->getScreen();
    LVDrawBuf & buf = *screen->getCanvas();
    lvRect saveClip;
    buf.GetClipRect(&saveClip);
    buf.SetClipRect(&_rect);
    if (key >= 0 && key < (int)_keys.size()) {
        drawKey(buf, key, _inverted);
        screen->invalidateRect(_keys[key].rc);
        screen->flush(false);
    }
    if (text) {
        drawTextField(buf, _inverted);
        screen->invalidateRect(_textRc);
        screen->flush(false);
    }
    buf.SetClipRect(&saveClip);
}

int PbKeyboard::keyAt(int x, int y) const
{
    for (int i = 0; i < (int)_keys.size(); i++)
        if (_keys[i].rc.isPointInside(lvPoint(x, y)))
            return i;
    return -1;
}

// OK and Cancel close the keyboard (CHANGED_CLOSED): the window is deleted, nothing may follow
PbKeyboard::Changed PbKeyboard::press(const Key & key)
{
    switch (key.code) {
    case PBK_OK:
        finish(true);
        return CHANGED_CLOSED;
    case PBK_CANCEL:
        finish(false);
        return CHANGED_CLOSED;
    case PBK_SHIFT:
        _shift = !_shift;
        return CHANGED_ALL; // the labels of the letters change
    case PBK_SYMBOLS:
        _symbols = !_symbols;
        _shift = false;
        build();
        return CHANGED_ALL;
    case PBK_LAYOUT:
        _layout = (_layout + 1) % LAYOUT_COUNT;
        _symbols = false;
        build();
        return CHANGED_ALL;
    case PBK_BACKSPACE:
        if (_textSelected)
            _text.clear();
        else if (!_text.empty())
            _text.erase(_text.length() - 1, 1);
        _textSelected = false;
        return CHANGED_TEXT;
    default:
        break;
    }
    if (_textSelected)
        _text.clear();
    _textSelected = false;
    if (_text.length() < MAX_TEXT) {
        lChar32 c[2] = { (lChar32)(key.code == PBK_SPACE ? ' ' : key.code), 0 };
        lString32 s(c);
        if (_shift)
            s.uppercase();
        _text << s;
    }
    if (_shift) {
        _shift = false; // one capital letter: the labels change back
        return CHANGED_ALL;
    }
    return CHANGED_TEXT;
}

void PbKeyboard::finish(bool accept)
{
    CRGUIWindowManager * wm = _wm;
    void (*handler)(char *) = _handler;
    char * buffer = _buffer;
    bool call = accept && !_text.empty() && handler != NULL && buffer != NULL;
    if (call) {
        lString8 s = UnicodeToUtf8(_text);
        strncpy(buffer, s.c_str(), _maxlen - 1);
        buffer[_maxlen - 1] = 0;
    }
    if (main_win != NULL && main_win->getProps()->getIntDef(PROP_POCKETBOOK_KEYBOARD_LAYOUT, -1) != _layout) {
        main_win->getProps()->setInt(PROP_POCKETBOOK_KEYBOARD_LAYOUT, _layout);
        main_win->saveSettings(lString32::empty_str);
    }
    wm->closeWindow(this); // deletes this window
    if (call)
        handler(buffer);
    else
        wm->update(true);
}

bool PbKeyboard::onCommand(int command, int params)
{
    (void)params;
    switch (command) {
    case MCMD_CANCEL:
        finish(false);
        return true;
    case MCMD_OK:
        finish(true);
        return true;
    default:
        return true; // the windows below do not get keys while the keyboard is open
    }
}

bool PbKeyboard::onTouch(int x, int y, CRGUITouchEventType evType)
{
    // every touch is the keyboard's: a tap over the book does not reach the windows below
    CRGUIEvent * event = new CRGUITouchEvent(x, y, evType);
    event->setTargetWindow(this);
    _wm->postEvent(event);
    return true;
}

bool PbKeyboard::onTouchEvent(int x, int y, CRGUITouchEventType evType)
{
    if (evType == CRTOUCH_DOWN || evType == CRTOUCH_DOWN_LONG) {
        int k = keyAt(x, y);
        if (k != _pressed) {
            int old = _pressed;
            _pressed = k;
            refresh(old, false);
            refresh(k, false);
        }
        return true;
    }
    if (evType == CRTOUCH_UP) {
        int k = keyAt(x, y);
        int pressed = _pressed;
        _pressed = -1;
        if (k >= 0 && k == pressed) {
            Key key = _keys[k];
            Changed changed = press(key);
            if (changed == CHANGED_CLOSED)
                return true; // the keyboard is deleted: nothing after it
            if (changed == CHANGED_ALL) {
                setDirty();
                _wm->update(false);
            } else {
                refresh(k, true);
            }
            return true;
        }
        refresh(pressed, false);
    }
    return true;
}

void pbOpenCrKeyboard(CRGUIWindowManager * wm, const char * title, char * buffer, int maxlen,
                      void (*handler)(char *), bool inverted)
{
    PbKeyboard * keyboard = new PbKeyboard(wm, title, buffer, maxlen, handler, inverted);
    wm->activateWindow(keyboard);
    wm->update(false);
}
