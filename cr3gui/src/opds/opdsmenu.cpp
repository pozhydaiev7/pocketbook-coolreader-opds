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
    : CRFullScreenMenu(wm, OPDS_CMD_CLOSE, caption, 8, rc), _rows(0), _lastTouchX(-1), _paged(paged),
      _prevCommand(OPDS_CMD_PREV), _nextCommand(OPDS_CMD_NEXT)
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

bool OpdsMenu::onTouchEvent(int x, int y, CRGUITouchEventType evType)
{
    _lastTouchX = x;
    return CRFullScreenMenu::onTouchEvent(x, y, evType);
}

bool OpdsMenu::onCommand(int command, int params)
{
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
      leftCommand(0), rightCommand(0), centerCommand(OPDS_CMD_SERVICE),
      _owner(menu), _title(title), _subtitle(subtitle), _kind(kind)
{
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

    if (_kind == PAGER) {
        int third = r.width() / 3;
        int y = r.top + (r.height() - tf->getHeight()) / 2;
        lString16 l = fitText(tf, left, third);
        lString16 c = fitText(sf, center, third);
        lString16 rt = fitText(tf, right, third);
        tf->DrawTextString(&buf, r.left, y, l.c_str(), l.length(), L'?', NULL, false, 0);
        int cw = sf->getTextWidth(c.c_str(), c.length());
        sf->DrawTextString(&buf, r.left + (r.width() - cw) / 2, r.top + (r.height() - sf->getHeight()) / 2,
                           c.c_str(), c.length(), L'?', NULL, false, 0);
        int rw = tf->getTextWidth(rt.c_str(), rt.length());
        tf->DrawTextString(&buf, r.right - rw, y, rt.c_str(), rt.length(), L'?', NULL, false, 0);
    } else if (_kind == TEXT) {
        int gap = sf->getHeight() / 8;
        int h1 = first.font.isNull() ? 0 : first.font->getHeight();
        int h2 = second.font.isNull() ? 0 : second.font->getHeight();
        int total = h1 + (h2 > 0 ? gap + h2 : 0);
        int y = r.top + (r.height() - total) / 2;
        drawLine(buf, first, r, y);
        drawLine(buf, second, r, y + h1 + gap);
    } else if (_subtitle.empty()) {
        lString16 t = fitText(tf, _title, r.width());
        tf->DrawTextString(&buf, r.left, r.top + (r.height() - tf->getHeight()) / 2,
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

void OpdsItem::drawLine(LVDrawBuf & buf, const InfoLine & line, const lvRect & r, int y)
{
    if (line.font.isNull() || (line.text.empty() && line.label.empty()))
        return;
    int x = r.left + line.indent;
    if (!line.label.empty()) {
        LVFontRef lf = _owner->labelFont;
        lString16 l = fitText(lf, line.label, r.right - x);
        lf->DrawTextString(&buf, x, y, l.c_str(), l.length(), L'?', NULL, false, 0);
        x += textWidth(lf, l);
    }
    lString16 t = fitText(line.font, line.text, r.right - x);
    line.font->DrawTextString(&buf, x, y, t.c_str(), t.length(), L'?', NULL, false, 0);
}

int OpdsItem::onSelect()
{
    if (_kind != PAGER)
        return 0; // regular row: the menu closes and posts the item id
    int x = _owner->lastTouchX();
    int width = _owner->getWindowManager()->getScreen()->getWidth();
    int command = centerCommand;
    if (x >= 0 && x < width / 3)
        command = leftCommand;
    else if (x >= 0 && x > width * 2 / 3)
        command = rightCommand;
    if (command == 0)
        return 1; // disabled side: stay on this screen
    _owner->closeMenu(command);
    return 1;
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
