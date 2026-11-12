// Full-screen touch lists of the CoolReader PocketBook port. See opdsmenu.h.
#include "opdsmenu.h"

#include <inkview.h>
#include <cri18n.h>
#include <lvstring.h>
#include <lvfntman.h>

#include "mainwnd.h"
#include "settings.h"
#include "cr3pocketbook.h"
#include "opdsbrowser.h"

namespace opdsui {

const char * const MARK_UP   = "\xC2\xAB ";        // "« "
const char * const MARK_PREV = "\xE2\x80\xB9 ";    // "‹ "
const char * const MARK_NEXT = " \xE2\x80\xBA";    // " ›"
const char * const MARK_OPEN = "\xC2\xBB ";        // "» "
const char * const DOT       = "  \xC2\xB7  ";     // "  ·  "
const char * const DASH      = " \xE2\x80\x94 ";   // " — "
const lChar16 ELLIPSIS = 0x2026;

lString16 u16(const std::string & s)
{
    return Utf8ToUnicode(lString8(s.c_str()));
}

lString16 fitText(LVFontRef font, const lString16 & text, int maxWidth)
{
    if (maxWidth <= 0 || (int)font->getTextWidth(text.c_str(), text.length()) <= maxWidth)
        return text;
    lChar16 ell[2] = { ELLIPSIS, 0 };
    int ellWidth = font->getTextWidth(ell, 1);
    int lo = 0, hi = text.length();
    while (lo < hi) { // longest prefix that fits together with the ellipsis
        int mid = (lo + hi + 1) / 2;
        if ((int)font->getTextWidth(text.c_str(), mid) + ellWidth <= maxWidth)
            lo = mid;
        else
            hi = mid - 1;
    }
    while (lo > 0 && (text[lo - 1] == ' ' || text[lo - 1] == ',' || text[lo - 1] == '.'))
        lo--;
    lString16 res = text.substr(0, lo);
    res << ell;
    return res;
}

int textWidth(LVFontRef font, const lString16 & text)
{
    return font->getTextWidth(text.c_str(), text.length());
}

std::vector<lString16> wrapText(LVFontRef font, const lString16 & text, int firstWidth, int width)
{
    std::vector<lString16> lines;
    lString16 line, word;
    for (int i = 0; i <= text.length(); i++) {
        lChar16 ch = i < text.length() ? text[i] : L' ';
        if (ch != L' ' && ch != L'\t' && ch != L'\r' && ch != L'\n') {
            word << ch;
            continue;
        }
        if (word.empty())
            continue;
        lString16 candidate = line.empty() ? word : line + L" " + word;
        int limit = lines.empty() ? firstWidth : width;
        if (line.empty() || textWidth(font, candidate) <= limit) {
            line = candidate;
        } else {
            lines.push_back(line);
            line = word;
        }
        word.clear();
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

lString16 menuSkinName(CRGUIWindowManager * wm)
{
    static const lChar16 * const ids[] = { L"touch-toc", L"settings", L"main" };
    CRSkinRef skin = wm->getSkin();
    if (!skin.isNull()) {
        for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
            if (!skin->pathById(ids[i]).empty())
                return lString16(L"#") + ids[i];
    }
    return lString16(L"#main");
}

// ------------------------------------------------------------ OpdsMenu

OpdsMenu::OpdsMenu(CRGUIWindowManager * wm, const lString16 & caption, lvRect & rc, bool paged)
    : CRFullScreenMenu(wm, OPDS_CMD_CLOSE, caption, 8, rc), _rows(0), _paged(paged),
      _prevCommand(OPDS_CMD_PREV), _nextCommand(OPDS_CMD_NEXT), _closeCommand(OPDS_CMD_CLOSE),
      _screen(0), _screens(0)
{
    if (!fontAntiAliasingActivated())
        forcePartialBwUpdates = true;
    setSkinName(menuSkinName(wm));
    _fullscreen = true;
    setAccelerators(wm->getAccTables().get("menu"));
    titleFont = fontMan->GetFont(pbScaledSize(26), 600, false, css_ff_sans_serif, lString8("Liberation Sans"));
    subFont = fontMan->GetFont(pbScaledSize(22), 400, false, css_ff_sans_serif, lString8("Liberation Sans"));
    labelFont = fontMan->GetFont(pbScaledSize(22), 600, false, css_ff_sans_serif, lString8("Liberation Sans"));
    bodyFont = fontMan->GetFont(pbScaledSize(24), 400, false, css_ff_sans_serif, lString8("Liberation Sans"));
}

OpdsMenu::~OpdsMenu()
{
    forcePartialBwUpdates = false;
}

void OpdsMenu::setPageCommands(int prevCommand, int nextCommand)
{
    _paged = true;
    _prevCommand = prevCommand;
    _nextCommand = nextCommand;
}

void OpdsMenu::setScreens(int screen, int screens)
{
    _screens = screens < 1 ? 1 : screens;
    _screen = screen < 0 ? 0 : (screen >= _screens ? _screens - 1 : screen);
}

bool OpdsMenu::isScrollEnabled(bool down)
{
    if (!_paged || _screens <= 0)
        return CRFullScreenMenu::isScrollEnabled(down);
    return down ? (_nextCommand != 0 && _screen + 1 < _screens) : (_prevCommand != 0 && _screen > 0);
}

void OpdsMenu::drawTitleBar()
{
    // CRMenu::draw() has counted the pages of the rows on the screen: one page
    if (_paged && _screens > 0) {
        _page = _screen + 1;
        _pages = _screens;
    }
    CRFullScreenMenu::drawTitleBar();
}

void OpdsMenu::setPlainRows()
{
    // the touch TOC with page numbers has the same rows without separator lines
    CRSkinRef skin = _wm->getSkin();
    if (!skin.isNull() && !skin->pathById(L"touch-toc-dots").empty())
        setSkinName(lString16(L"#touch-toc-dots"));
}

void OpdsMenu::fitCaption()
{
    CRMenuSkinRef skin = getSkin();
    if (skin.isNull() || skin->getTitleSkin().isNull())
        return;
    LVFontRef font = skin->getTitleSkin()->getFont();
    if (font.isNull())
        return;
    int avail = _wm->getScreen()->getWidth() * 60 / 100;
    if (!skin->getCloseButton().isNull())
        avail -= skin->getCloseButton()->getMinSize().x;
    _label = fitText(font, _label, avail);
}

int OpdsMenu::setupRows(bool twoLines)
{
    reconfigure(0);
    fitCaption();
    lvRect client;
    getClientRect(client);
    int pad = titleFont->getHeight() / 3;
    int rowHeight = titleFont->getHeight() + pad * 2 + (twoLines ? subFont->getHeight() : 0);
    _rows = rowHeight > 0 ? client.height() / rowHeight : 8;
    if (_rows < 4)
        _rows = 4;
    if (_rows > 30)
        _rows = 30;
    _pageItems = _rows;
    return _rows;
}

void OpdsMenu::reconfigure(int flags)
{
    CRFullScreenMenu::reconfigure(flags);
    if (_rows > 0)
        _pageItems = _rows;
}

bool OpdsMenu::onCommand(int command, int params)
{
    // the arrow at the caption and the Back key: one level up
    if ((command == DCMD_BUTTON_PRESSED || command == DCMD_BUTTON_PRESSED_LONG) && params == BTN_CLOSE) {
        closeMenu(_closeCommand);
        return true;
    }
    // the arrows of the title turn the screens of the list, as the page keys do
    if (_paged && (command == DCMD_BUTTON_PRESSED || command == DCMD_BUTTON_PRESSED_LONG) &&
        (params == BTN_SCROLL_DOWN || params == BTN_SCROLL_UP)) {
        if (!isScrollEnabled(params == BTN_SCROLL_DOWN))
            return true;
        command = params == BTN_SCROLL_DOWN ? MCMD_NEXT_PAGE : MCMD_PREV_PAGE;
        params = 1;
    }
    if (command == MCMD_CANCEL && _closeCommand != getId()) {
        closeMenu(_closeCommand);
        return true;
    }
    if (_paged) {
        // Page keys of the reader flip the screens of the list.
        if ((command == MCMD_SCROLL_FORWARD || command == MCMD_NEXT_PAGE) && _nextCommand != 0) {
            closeMenu(_nextCommand);
            return true;
        }
        if ((command == MCMD_SCROLL_BACK || command == MCMD_PREV_PAGE) && _prevCommand != 0) {
            closeMenu(_prevCommand);
            return true;
        }
    }
    return CRFullScreenMenu::onCommand(command, params);
}

int OpdsMenu::rowTextWidth()
{
    lvRect client;
    getClientRect(client);
    int width = client.width() - 2 * (titleFont->getHeight() / 3);
    CRMenuSkinRef skin = getSkin();
    if (!skin.isNull() && !skin->getItemSkin().isNull()) {
        lvRect borders = skin->getItemSkin()->getBorderWidths();
        width -= borders.left + borders.right;
    }
    return width;
}

void OpdsMenu::add(int command, const lString16 & title, const lString16 & subtitle)
{
    addItem(new OpdsItem(this, command, title, subtitle));
}

// ------------------------------------------------------------ OpdsItem

OpdsItem::OpdsItem(OpdsMenu * menu, int id, const lString16 & title, const lString16 & subtitle, Kind kind)
    : CRMenuItem(menu, id, title, LVImageSourceRef(), LVFontRef()),
      indent(0), mark(false), _owner(menu), _title(title), _subtitle(subtitle), _kind(kind)
{
}

int drawRowMark(LVDrawBuf & buf, int x, int top, int height, int size, lUInt32 color)
{
    int half = size * 5 / 24;          // the triangle is about 0.4 of the font height
    if (half < 3)
        half = 3;
    int width = half * 3 / 2;
    // the middle of the lower-case letters of a text centred in the row
    int cy = top + height / 2 + size / 16;
    for (int dy = -half; dy <= half; dy++) {
        int len = width * (half + 1 - (dy < 0 ? -dy : dy)) / (half + 1);
        if (len > 0)
            buf.FillRect(x, cy + dy, x + len, cy + dy + 1, color);
    }
    return width + size / 4;
}

void OpdsItem::Draw(LVDrawBuf & buf, lvRect & rc, CRRectSkinRef skin, CRRectSkinRef valueSkin, bool selected)
{
    (void)valueSkin;
    (void)selected;
    _itemDirty = false;
    skin->draw(buf, rc);
    buf.SetTextColor(skin->getTextColor());
    buf.SetBackgroundColor(skin->getBackgroundColor());

    LVFontRef tf = _owner->titleFont;
    LVFontRef sf = _owner->subFont;
    lvRect borders = skin->getBorderWidths();
    int pad = tf->getHeight() / 3;
    lvRect r = rc;
    r.left += borders.left + pad;
    r.right -= borders.right + pad;
    r.top += borders.top;
    r.bottom -= borders.bottom;
    if (r.width() <= 0 || r.height() <= 0)
        return;

    lvRect oldClip;
    buf.GetClipRect(&oldClip);
    buf.SetClipRect(&r);

    if (_kind == TEXT) {
        int gap = sf->getHeight() / 8;
        int h1 = first.font.isNull() ? 0 : first.font->getHeight();
        int h2 = second.font.isNull() ? 0 : second.font->getHeight();
        int total = h1 + (h2 > 0 ? gap + h2 : 0);
        int y = r.top + (r.height() - total) / 2;
        if (first.rule) {
            // section line at the top of the row
            int t = sf->getHeight() / 16;
            if (t < 1)
                t = 1;
            buf.FillRect(r.left, rc.top + t, r.right, rc.top + 2 * t, skin->getTextColor());
        }
        drawLine(buf, first, r, y, first.font.isNull() ? 0 : first.font->getBaseline());
        drawLine(buf, second, r, y + h1 + gap, second.font.isNull() ? 0 : second.font->getBaseline());
    } else if (_subtitle.empty()) {
        lvRect tr = r;
        tr.left += indent;
        if (!aside.empty()) {
            // page number at the right edge, on the baseline of the title
            int aw = textWidth(sf, aside);
            int base = r.top + (r.height() - tf->getHeight()) / 2 + tf->getBaseline();
            sf->DrawTextString(&buf, r.right - aw, base - sf->getBaseline(),
                               aside.c_str(), aside.length(), L'?', NULL, false, 0);
            tr.right -= aw + sf->getHeight();
        }
        if (mark)
            tr.left += drawRowMark(buf, tr.left, r.top, r.height(), tf->getHeight(), skin->getTextColor());
        lString16 t = fitText(tf, _title, tr.width());
        tf->DrawTextString(&buf, tr.left, r.top + (r.height() - tf->getHeight()) / 2,
                           t.c_str(), t.length(), L'?', NULL, false, 0);
    } else {
        int gap = sf->getHeight() / 8;
        int total = tf->getHeight() + gap + sf->getHeight();
        int y = r.top + (r.height() - total) / 2;
        lString16 t = fitText(tf, _title, r.width());
        lString16 s = fitText(sf, _subtitle, r.width());
        tf->DrawTextString(&buf, r.left, y, t.c_str(), t.length(), L'?', NULL, false, 0);
        sf->DrawTextString(&buf, r.left, y + tf->getHeight() + gap, s.c_str(), s.length(), L'?', NULL, false, 0);
    }
    buf.SetClipRect(&oldClip);
}

void OpdsItem::drawLine(LVDrawBuf & buf, const InfoLine & line, const lvRect & r, int y, int baseline)
{
    if (line.font.isNull() || (line.text.empty() && line.label.empty()))
        return;
    // the label and the text share the font and the baseline; the label is gray
    int top = y + baseline - line.font->getBaseline();
    int x = r.left + line.indent;
    if (!line.label.empty()) {
        lUInt32 color = buf.GetTextColor();
        buf.SetTextColor(OPDSUI_LABEL_COLOR);
        lString16 l = fitText(line.font, line.label, r.right - x);
        line.font->DrawTextString(&buf, x, top, l.c_str(), l.length(), L'?', NULL, false, 0);
        buf.SetTextColor(color);
        x += textWidth(line.font, l);
    }
    lString16 t = fitText(line.font, line.text, r.right - x);
    line.font->DrawTextString(&buf, x, top, t.c_str(), t.length(), L'?', NULL, false, 0);
}

void activate(CRGUIWindowManager * wm, OpdsMenu * menu)
{
    menu->reconfigure(0);
    menu->setCurPage(0);
    wm->activateWindow(menu);
    wm->update(true);
    CRLog::info("Touch list shown, %d items, top window is the list: %d",
                menu->getItems().length(), wm->getTopVisibleWindow() == menu ? 1 : 0);
}

} // namespace opdsui
