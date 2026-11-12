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

// The touch layouts of the firmware keyboard (language/keyboard/EN.kbd, RU.kbd, UA.kbd), in the
// notation of those files: 5 rows of 11 cells, the keys of a row separated by spaces; cn - cancel,
// bs - backspace, sh - shift, sp - space, lg - next layout, ok; ":N" - a key N cells wide.
// The second page of a layout is the one shown with Shift: the capital letters are listed there,
// lString32::uppercase() knows only the Russian alphabet without its letter io.
const int COLUMNS = 11;
const int ROWS = 5;
const int MAX_TEXT = 100;

const char * const PAGES[3][2][ROWS] = {
    { { "1 2 3 4 5 6 7 8 9 0 cn",
        "q w e r t y u i o p bs",
        "a s d f g h j k l ; '",
        "z x c v b n m . , - +",
        "sh [ ] sp:3 ? / lg ok:2" },
      { "! @ # $ % ^ & * ( ) cn",
        "Q W E R T Y U I O P bs",
        "A S D F G H J K L : \"",
        "Z X C V B N M < > _ =",
        "sh { } sp:3 ` \\ lg ok:2" } },
    { { "1 2 3 4 5 6 7 8 9 0 cn",
        "й ц у к е н г ш щ з bs",
        "ф ы в а п р о л д ж э",
        "я ч с м и т ь б ю . ,",
        "sh ё sp:4 х ъ lg ok:2" },
      { "! \" № ; % : ? * ( ) cn",
        "Й Ц У К Е Н Г Ш Щ З bs",
        "Ф Ы В А П Р О Л Д Ж Э",
        "Я Ч С М И Т Ь Б Ю . ,",
        "sh Ё sp:4 Х Ъ lg ok:2" } },
    { { "1 2 3 4 5 6 7 8 9 0 cn",
        "й ц у к е н г ш щ з bs",
        "ф і в а п р о л д ж є",
        "я ч с м и т ь б ю . ,",
        "sh ' sp:3 ґ х ї lg ok:2" },
      { "! \" № ; % : ? * ( ) cn",
        "Й Ц У К Е Н Г Ш Щ З bs",
        "Ф І В А П Р О Л Д Ж Є",
        "Я Ч С М И Т Ь Б Ю . ,",
        "sh ' sp:3 Ґ Х Ї lg ok:2" } },
};
const char * const LAYOUT_NAMES[3] = { "EN", "RU", "UA" };

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
      _textSelected(false), _layout(0), _shift(false), _pressed(-1), _line(1)
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

// Adds the keys of one row written in the notation of the .kbd files (see PAGES).
void PbKeyboard::addRow(const char * row, int left, int top, int cellWidth, int cellHeight)
{
    lString32 text = Utf8ToUnicode(lString8(row));
    int x = left;
    int pos = 0;
    while (pos < text.length()) {
        if (text[pos] == ' ') {
            pos++;
            continue;
        }
        int end = pos;
        while (end < text.length() && text[end] != ' ')
            end++;
        int length = end - pos;
        Key k;
        int span = 1;
        if (length == 1) {
            k.code = text[pos];
        } else {
            // a named key: "bs", "sp:3"
            if (length == 4 && text[pos + 2] == ':' && text[pos + 3] >= '1' && text[pos + 3] <= '9')
                span = text[pos + 3] - '0';
            switch ((text[pos] << 8) | text[pos + 1]) {
            case ('c' << 8) | 'n': k.code = PBK_CANCEL; break;
            case ('b' << 8) | 's': k.code = PBK_BACKSPACE; break;
            case ('s' << 8) | 'h': k.code = PBK_SHIFT; break;
            case ('s' << 8) | 'p': k.code = PBK_SPACE; break;
            case ('l' << 8) | 'g': k.code = PBK_LAYOUT; break;
            case ('o' << 8) | 'k': k.code = PBK_OK; break;
            default: k.code = 0; break;
            }
        }
        pos = end;
        if (k.code == 0)
            continue;
        k.rc = lvRect(x, top, x + span * cellWidth, top + cellHeight);
        _keys.push_back(k);
        x += span * cellWidth;
    }
}

void PbKeyboard::build()
{
    lvRect screen = _wm->getScreen()->getRect();
    int width = screen.width();
    int height = screen.height();
    int pad = width / 100 + 2;
    // the keys are as wide as the keys of the firmware keyboard: 11 in a row
    int cellWidth = (width - 2 * pad) / COLUMNS;
    int cellHeight = cellWidth * 90 / 100;
    if (cellHeight > height / 12)
        cellHeight = height / 12; // landscape: the keyboard takes about half of the screen
    // the text field, 5 rows of keys
    int total = pad + cellHeight + ROWS * cellHeight + pad;
    _rect = lvRect(0, height - total, width, height);
    int left = (width - COLUMNS * cellWidth) / 2;
    int top = _rect.top + pad;

    // the text field: its frame is in line with the frames of the keys
    int gap = keyGap(cellHeight);
    _textRc = lvRect(left + gap, top + gap, left + COLUMNS * cellWidth - gap, top + cellHeight - gap);

    _keys.clear();
    _pressed = -1;
    for (int row = 0; row < ROWS; row++)
        addRow(PAGES[_layout][_shift ? 1 : 0][row], left, top + (row + 1) * cellHeight, cellWidth, cellHeight);
    _keyFont = fontMan->GetFont(cellHeight * 48 / 100, 400, false, css_ff_sans_serif, lString8("Liberation Sans"));
    _keyBoldFont = fontMan->GetFont(cellHeight * 42 / 100, 700, false, css_ff_sans_serif, lString8("Liberation Sans"));
    _line = width / 600 + 1;
    _textFont = fontMan->GetFont(cellHeight * 50 / 100, 400, false, css_ff_sans_serif, lString8("Liberation Sans"));
    setDirty();
}

// space between the cell of a key and its frame
int PbKeyboard::keyGap(int cellHeight)
{
    return cellHeight / 14 + 1;
}

lString32 PbKeyboard::label(const Key & key) const
{
    switch (key.code) {
    case PBK_SHIFT: return lString32(U"\x2191");      // up arrow
    case PBK_BACKSPACE: return lString32(U"\x2190");  // left arrow
    case PBK_SPACE: return lString32::empty_str;
    case PBK_LAYOUT: return lString32(LAYOUT_NAMES[_layout]);
    case PBK_OK: return lString32("OK");
    case PBK_CANCEL: return lString32(U"\x00D7");     // multiplication sign
    default:
        break;
    }
    // a letter key of the Shift page already holds the capital letter
    lChar32 c[2] = { (lChar32)key.code, 0 };
    return lString32(c);
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
    int gap = keyGap(k.rc.height());
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
        build(); // the other page of the layout
        return CHANGED_ALL;
    case PBK_LAYOUT:
        _layout = (_layout + 1) % LAYOUT_COUNT;
        _shift = false;
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
        _text << lString32(c);
    }
    if (_shift) {
        _shift = false; // one capital letter or sign: back to the first page
        build();
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
