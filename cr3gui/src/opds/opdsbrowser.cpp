// OPDS catalog browser for the CoolReader PocketBook port. See opdsbrowser.h.
#include "opdsbrowser.h"

#include <inkview.h>
#include <cri18n.h>
#include <lvstring.h>
#include <lvstream.h>
#include <lvfntman.h>
#include <stdio.h>
#include <string.h>

#include "mainwnd.h"
#include "fsmenu.h"
#include "settings.h"
#include "cr3pocketbook.h"
#include "web.h"

extern V3DocViewWin * main_win;

namespace {

const size_t OPDS_MAX_ENTRIES = OPDS_CMD_FORMAT - OPDS_CMD_ENTRY;

// UTF-8 (lString16(const char*) decodes UTF-8).
const char * const MARK_UP   = "\xC2\xAB ";        // "« "  one catalog level up
const char * const MARK_PREV = "\xE2\x80\xB9 ";    // "‹ "  previous screen
const char * const MARK_NEXT = " \xE2\x80\xBA";    // " ›"  next screen
const char * const MARK_OPEN = "\xC2\xBB ";        // "» "  sub-catalog
const char * const DOT       = "  \xC2\xB7  ";     // "  ·  "
const char * const DASH      = " \xE2\x80\x94 ";   // " — "
const lChar16 ELLIPSIS = 0x2026;

lString16 u16(const std::string & s)
{
    return Utf8ToUnicode(lString8(s.c_str()));
}

// Cuts text to fit maxWidth, ending with an ellipsis.
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

class OpdsMenu;

// One row of the catalog list: title (bold) and an optional second line (author or description),
// or the paging row with three tap zones: previous | service menu | next.
class OpdsItem : public CRMenuItem
{
public:
    enum Kind { ROW, PAGER };

    OpdsItem(OpdsMenu * menu, int id, const lString16 & title, const lString16 & subtitle, Kind kind = ROW);
    virtual void Draw(LVDrawBuf & buf, lvRect & rc, CRRectSkinRef skin, CRRectSkinRef valueSkin, bool selected);
    virtual int onSelect();

    // Paging row texts; an empty side is disabled.
    lString16 left, center, right;
    int leftCommand, rightCommand;

private:
    OpdsMenu * _owner;
    lString16 _title;
    lString16 _subtitle;
    Kind _kind;
};

// Full-screen touch list in the touch-TOC style, with its own fonts and row count.
class OpdsMenu : public CRFullScreenMenu
{
    int _rows;
    int _lastTouchX;
    bool _paged;
public:
    LVFontRef titleFont;
    LVFontRef subFont;

    OpdsMenu(CRGUIWindowManager * wm, const lString16 & caption, lvRect & rc, bool paged)
        : CRFullScreenMenu(wm, OPDS_CMD_CLOSE, caption, 8, rc), _rows(0), _lastTouchX(-1), _paged(paged)
    {
        if (!fontAntiAliasingActivated())
            forcePartialBwUpdates = true;
        setSkinName(L"#touch-toc");
        _fullscreen = true;
        setAccelerators(wm->getAccTables().get("menu"));
        titleFont = fontMan->GetFont(pbScaledSize(26), 600, false, css_ff_sans_serif, lString8("Liberation Sans"));
        subFont = fontMan->GetFont(pbScaledSize(22), 400, false, css_ff_sans_serif, lString8("Liberation Sans"));
    }

    virtual ~OpdsMenu()
    {
        forcePartialBwUpdates = false;
    }

    // Shortens the caption so it does not run under the close button and the page counter.
    void fitCaption()
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

    // Number of rows that fit on the screen with two text lines per row.
    int setupRows(bool twoLines)
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

    virtual void reconfigure(int flags)
    {
        CRFullScreenMenu::reconfigure(flags);
        if (_rows > 0)
            _pageItems = _rows;
    }

    virtual bool onTouchEvent(int x, int y, CRGUITouchEventType evType)
    {
        _lastTouchX = x;
        return CRFullScreenMenu::onTouchEvent(x, y, evType);
    }

    virtual bool onCommand(int command, int params = 0)
    {
        if (_paged) {
            // Page keys of the reader flip the catalog screens.
            if (command == MCMD_SCROLL_FORWARD || command == MCMD_NEXT_PAGE) {
                closeMenu(OPDS_CMD_NEXT);
                return true;
            }
            if (command == MCMD_SCROLL_BACK || command == MCMD_PREV_PAGE) {
                closeMenu(OPDS_CMD_PREV);
                return true;
            }
        }
        return CRFullScreenMenu::onCommand(command, params);
    }

    int lastTouchX() const { return _lastTouchX; }

    void add(int command, const lString16 & title, const lString16 & subtitle = lString16::empty_str)
    {
        addItem(new OpdsItem(this, command, title, subtitle));
    }
};

OpdsItem::OpdsItem(OpdsMenu * menu, int id, const lString16 & title, const lString16 & subtitle, Kind kind)
    : CRMenuItem(menu, id, title, LVImageSourceRef(), LVFontRef()),
      leftCommand(0), rightCommand(0), _owner(menu), _title(title), _subtitle(subtitle), _kind(kind)
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

int OpdsItem::onSelect()
{
    if (_kind != PAGER)
        return 0; // regular row: the menu closes and posts the item id
    int x = _owner->lastTouchX();
    int width = _owner->getWindowManager()->getScreen()->getWidth();
    int command = OPDS_CMD_SERVICE;
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
    CRLog::info("OPDS: menu shown, %d items, top window is menu: %d",
                menu->getItems().length(), wm->getTopVisibleWindow() == menu ? 1 : 0);
}

void urlKeyboardHandler(char * s)
{
    executeCommand((s && *s) ? OPDS_CMD_SET_URL_APPLY : OPDS_CMD_SHOW_LIST, 0);
}

void searchKeyboardHandler(char * s)
{
    executeCommand((s && *s) ? OPDS_CMD_SEARCH_APPLY : OPDS_CMD_SHOW_LIST, 0);
}

void infoDialogHandler(int)
{
    executeCommand(OPDS_CMD_SHOW_BOOK, 0);
}

// Connects Wi-Fi through the firmware when it is not connected (system dialog / last network).
bool ensureNetwork()
{
    int state = QueryNetwork();
    CRLog::info("OPDS: QueryNetwork() = 0x%x", state);
    if (state & NET_CONNECTED)
        return true;
    int rc = NetConnect(NULL);
    CRLog::info("OPDS: NetConnect(NULL) = %d, QueryNetwork() = 0x%x", rc, QueryNetwork());
    return rc == NET_OK;
}

bool endsWith(const std::string & s, const char * suffix)
{
    size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// Rejects error pages the server may return instead of a book.
bool looksLikeBook(const std::string & path, const std::string & extension, std::string & reason)
{
    FILE * f = fopen(path.c_str(), "rb");
    if (!f) { reason = _("The book file was not written."); return false; }
    char head[16] = {0};
    size_t n = fread(head, 1, sizeof(head) - 1, f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    if (n == 0 || size <= 0) { reason = _("The downloaded file is empty."); return false; }
    if (endsWith(extension, "zip") || extension == "epub") {
        if (!(head[0] == 'P' && head[1] == 'K')) { reason = _("The server returned a web page instead of the book."); return false; }
        return true;
    }
    std::string start(head);
    for (size_t i = 0; i < start.size(); i++)
        if (start[i] >= 'A' && start[i] <= 'Z') start[i] = (char)(start[i] - 'A' + 'a');
    if (start.compare(0, 14, "<!doctype html") == 0 || start.compare(0, 5, "<html") == 0) {
        reason = _("The server returned a web page instead of the book.");
        return false;
    }
    return true;
}

} // namespace

OpdsBrowser & OpdsBrowser::instance()
{
    static OpdsBrowser browser;
    return browser;
}

std::string OpdsBrowser::rootUrl()
{
    lString16 url = main_win->getProps()->getStringDef(PROP_OPDS_URL, OPDS_DEFAULT_URL);
    url.trim();
    if (url.empty())
        url = lString16(OPDS_DEFAULT_URL);
    return std::string(UnicodeToUtf8(url).c_str());
}

void OpdsBrowser::open(CRGUIWindowManager * wm)
{
    if (_history.empty() || _history[0].baseUrl != rootUrl()) {
        _history.clear();
        _screens.clear();
        if (!load(rootUrl(), true))
            return;
    }
    _bookIndex = -1;
    showList(wm);
}

bool OpdsBrowser::fetch(const std::string & url, opds::Feed & feed)
{
    CRLog::info("OPDS: load %s", url.c_str());
    if (!ensureNetwork()) {
        Message(ICON_WARNING, "OPDS", _("No network connection. Turn on Wi-Fi and try again."), 4000);
        return false;
    }
    // No HideHourglass(): like the rest of CR3, the next screen update removes the hourglass.
    ShowHourglassForce();
    std::string body = web::get(url);
    CRLog::info("OPDS: got %d bytes", (int)body.size());

    if (body.empty()) {
        Message(ICON_WARNING, "OPDS", _("Cannot load the catalog. Check Wi-Fi and the catalog address."), 4000);
        return false;
    }
    std::string error;
    if (!feed.parse(body, url, error)) {
        CRLog::error("OPDS: parse error for %s: %s", url.c_str(), error.c_str());
        lString8 text(_("The server did not return an OPDS catalog."));
        text << "\n" << error.c_str();
        Message(ICON_WARNING, "OPDS", text.c_str(), 5000);
        return false;
    }
    if (feed.entries.size() > OPDS_MAX_ENTRIES)
        feed.entries.resize(OPDS_MAX_ENTRIES);
    return true;
}

bool OpdsBrowser::load(const std::string & url, bool push)
{
    opds::Feed feed;
    if (!fetch(url, feed))
        return false;
    if (push || _history.empty()) {
        _history.push_back(feed);
        _screens.push_back(0);
    } else {
        _history.back() = feed;
        _screens.back() = 0;
    }
    return true;
}

// Appends the next catalog page (rel="next") to the current list.
bool OpdsBrowser::loadMore()
{
    if (_history.empty() || _history.back().nextHref.empty())
        return false;
    opds::Feed more;
    if (!fetch(_history.back().nextHref, more))
        return false;
    opds::Feed & feed = _history.back();
    size_t before = feed.entries.size();
    for (size_t i = 0; i < more.entries.size() && feed.entries.size() < OPDS_MAX_ENTRIES; i++)
        feed.entries.push_back(more.entries[i]);
    feed.nextHref = more.nextHref;
    CRLog::info("OPDS: appended %d entries, next page: %s", (int)(feed.entries.size() - before),
                feed.nextHref.empty() ? "none" : "yes");
    return feed.entries.size() > before;
}

bool OpdsBrowser::hasSearchRow() const
{
    return _history.size() == 1 && !_history.back().searchTemplate.empty();
}

// Screen 0 holds the optional search row; every screen ends with the paging row.
int OpdsBrowser::entriesOnScreen(int screen) const
{
    int n = _rows - 1 - ((screen == 0 && hasSearchRow()) ? 1 : 0);
    return n < 1 ? 1 : n;
}

int OpdsBrowser::firstEntryOfScreen(int screen) const
{
    if (screen <= 0)
        return 0;
    return entriesOnScreen(0) + (screen - 1) * entriesOnScreen(1);
}

void OpdsBrowser::showList(CRGUIWindowManager * wm)
{
    if (_history.empty())
        return;
    _bookIndex = -1;
    const opds::Feed & feed = _history.back();
    int & screen = _screens.back();
    lvRect rc = wm->getScreen()->getRect();
    lString16 caption = u16(feed.title.empty() ? std::string("OPDS") : feed.title);
    OpdsMenu * menu = new OpdsMenu(wm, caption, rc, true);
    _rows = menu->setupRows(true);

    int total = (int)feed.entries.size();
    while (screen > 0 && firstEntryOfScreen(screen) >= total)
        screen--;
    int first = firstEntryOfScreen(screen);
    int count = entriesOnScreen(screen);
    int last = first + count; // exclusive
    if (last > total)
        last = total;

    if (screen == 0 && hasSearchRow())
        menu->add(OPDS_CMD_SEARCH, lString16(_("Search...")));

    for (int i = first; i < last; i++) {
        const opds::Entry & e = feed.entries[i];
        if (e.isBook()) {
            lString16 sub = u16(e.authorsText());
            if (!e.canDownload() && !e.unsupportedTypes.empty()) {
                if (!sub.empty()) sub << L"  ";
                sub << L"[" << u16(e.unsupportedTypes[0]) << L"]";
            }
            menu->add(OPDS_CMD_ENTRY + i, u16(e.title), sub);
        } else {
            menu->add(OPDS_CMD_ENTRY + i, lString16(MARK_OPEN) + u16(e.title), u16(e.summary));
        }
    }
    // Empty rows keep the paging row at the bottom of the screen.
    for (int i = last - first + ((screen == 0 && hasSearchRow()) ? 1 : 0); i < _rows - 1; i++)
        menu->addItem(new OpdsItem(menu, OPDS_CMD_FIRST + 9999, lString16::empty_str, lString16::empty_str));

    OpdsItem * pager = new OpdsItem(menu, OPDS_CMD_SERVICE, lString16::empty_str, lString16::empty_str, OpdsItem::PAGER);
    if (screen > 0) {
        pager->left = lString16(MARK_PREV) + lString16(_("Back"));
        pager->leftCommand = OPDS_CMD_PREV;
    } else if (_history.size() > 1) {
        pager->left = lString16(MARK_UP) + lString16(_("Back"));
        pager->leftCommand = OPDS_CMD_BACK;
    }
    bool more = last < total || !feed.nextHref.empty();
    if (more) {
        pager->right = lString16(_("Next")) + lString16(MARK_NEXT);
        pager->rightCommand = OPDS_CMD_NEXT;
    }
    pager->center = lString16(_("Page")) + L" " + lString16::itoa(screen + 1) + lString16(DOT) + lString16(_("Catalog menu"));
    menu->addItem(pager);
    activate(wm, menu);
}

// Service menu: actions that do not fit into the list screens.
void OpdsBrowser::showService(CRGUIWindowManager * wm)
{
    lvRect rc = wm->getScreen()->getRect();
    OpdsMenu * menu = new OpdsMenu(wm, lString16(_("Catalog menu")), rc, false);
    menu->setupRows(true);
    menu->add(OPDS_CMD_SHOW_LIST, lString16(MARK_PREV) + lString16(_("Back to the list")));
    if (_history.size() > 1)
        menu->add(OPDS_CMD_BACK, lString16(MARK_UP) + lString16(_("Back")));
    if (!_history.empty() && !_history.back().searchTemplate.empty())
        menu->add(OPDS_CMD_SEARCH, lString16(_("Search...")));
    if (_history.size() > 1)
        menu->add(OPDS_CMD_HOME, lString16(_("Catalog start page")));
    menu->add(OPDS_CMD_SET_URL, lString16(_("Catalog address...")), u16(rootUrl()));
    menu->add(OPDS_CMD_CLOSE, lString16(_("Close catalog")));
    activate(wm, menu);
}

const opds::Entry * OpdsBrowser::currentBook()
{
    if (_history.empty() || _bookIndex < 0 || _bookIndex >= (int)_history.back().entries.size())
        return NULL;
    return &_history.back().entries[_bookIndex];
}

void OpdsBrowser::showBook(CRGUIWindowManager * wm, int index)
{
    _bookIndex = index;
    const opds::Entry * e = currentBook();
    if (!e) { showList(wm); return; }

    lvRect rc = wm->getScreen()->getRect();
    OpdsMenu * menu = new OpdsMenu(wm, u16(e->title), rc, false);
    int rows = menu->setupRows(true);

    menu->add(OPDS_CMD_SHOW_LIST, lString16(MARK_PREV) + lString16(_("Back to the list")));
    // Full title and author; a tap shows the description.
    menu->add(e->summary.empty() ? OPDS_CMD_SHOW_BOOK : OPDS_CMD_INFO, u16(e->title), u16(e->authorsText()));
    for (size_t k = 0; k < e->formats.size(); k++)
        menu->add(OPDS_CMD_FORMAT + (int)k,
                  lString16(_("Download and open")) + L" " + u16(e->formats[k].label));
    if (!e->canDownload())
        menu->add(OPDS_CMD_SHOW_LIST, lString16(_("CoolReader cannot open this format")));
    for (size_t k = 0; k < e->related.size() && menu->getItems().length() < rows; k++) {
        std::string t = e->related[k].title.empty() ? std::string("...") : e->related[k].title;
        menu->add(OPDS_CMD_RELATED + (int)k, lString16(MARK_OPEN) + u16(t));
    }
    activate(wm, menu);
}

void OpdsBrowser::showInfo()
{
    const opds::Entry * e = currentBook();
    if (!e) return;
    std::string text = e->summary;
    if (text.size() > 2000) {
        size_t cut = 2000;
        while (cut > 0 && (((unsigned char)text[cut]) & 0xC0) == 0x80) cut--;
        text = text.substr(0, cut) + "...";
    }
    std::string title = e->title;
    if (!e->authors.empty()) title += " \xE2\x80\x94 " + e->authorsText();
    Dialog(ICON_INFORMATION, title.c_str(), text.c_str(), "OK", NULL, infoDialogHandler);
}

void OpdsBrowser::download(CRGUIWindowManager * wm, int formatIndex)
{
    const opds::Entry * e = currentBook();
    if (!e || formatIndex < 0 || formatIndex >= (int)e->formats.size()) {
        showList(wm);
        return;
    }
    const opds::Acquisition & a = e->formats[formatIndex];

    lString16 dir16 = main_win->getProps()->getStringDef(PROP_OPDS_DOWNLOAD_DIR, OPDS_DEFAULT_DIR);
    LVCreateDirectory(dir16);
    std::string dir = UnicodeToUtf8(dir16).c_str();
    if (!dir.empty() && dir[dir.size() - 1] != '/')
        dir += '/';
    std::string path = dir + opds::makeBookFileName(*e, a);

    if (!LVFileExists(u16(path))) {
        std::string part = path + ".part";
        CRLog::info("OPDS: download %s -> %s", a.href.c_str(), path.c_str());
        if (!ensureNetwork()) {
            Message(ICON_WARNING, "OPDS", _("No network connection. Turn on Wi-Fi and try again."), 4000);
            showBook(wm, _bookIndex);
            return;
        }
        ShowHourglassForce();
        bool ok = web::getBinary(a.href, part);
        std::string reason;
        if (!ok || !looksLikeBook(part, a.extension, reason)) {
            remove(part.c_str());
            lString8 text(_("Download failed."));
            if (!reason.empty()) text << "\n" << reason.c_str();
            Message(ICON_WARNING, "OPDS", text.c_str(), 5000);
            showBook(wm, _bookIndex);
            return;
        }
        if (rename(part.c_str(), path.c_str()) != 0) {
            remove(part.c_str());
            Message(ICON_WARNING, "OPDS", _("Cannot save the book file."), 4000);
            showBook(wm, _bookIndex);
            return;
        }
    } else {
        CRLog::info("OPDS: already downloaded %s", path.c_str());
    }
    pbOpenDownloadedBook(u16(path));
}

void OpdsBrowser::editUrl(CRGUIWindowManager * wm)
{
    (void)wm;
    lString8 url(rootUrl().c_str());
    strncpy(keyBuffer, url.c_str(), sizeof(keyBuffer) - 1);
    keyBuffer[sizeof(keyBuffer) - 1] = 0;
    pbOpenKeyboard(_("OPDS catalog address"), keyBuffer, sizeof(keyBuffer) - 1, urlKeyboardHandler);
}

bool OpdsBrowser::onCommand(CRGUIWindowManager * wm, int command, int params)
{
    (void)params;
    CRLog::trace("OpdsBrowser::onCommand(%d)", command);
    switch (command) {
    case OPDS_CMD_CLOSE:
        return true;
    case OPDS_CMD_SHOW_LIST:
        showList(wm);
        return true;
    case OPDS_CMD_SHOW_BOOK:
        showBook(wm, _bookIndex);
        return true;
    case OPDS_CMD_SERVICE:
        showService(wm);
        return true;
    case OPDS_CMD_BACK:
        if (_history.size() > 1) {
            _history.pop_back();
            _screens.pop_back();
        }
        showList(wm);
        return true;
    case OPDS_CMD_HOME:
        if (_history.size() > 1) {
            _history.resize(1);
            _screens.resize(1);
        }
        showList(wm);
        return true;
    case OPDS_CMD_PREV:
        if (!_screens.empty() && _screens.back() > 0) {
            _screens.back()--;
        } else if (_history.size() > 1) {
            _history.pop_back();
            _screens.pop_back();
        }
        showList(wm);
        return true;
    case OPDS_CMD_NEXT:
        if (!_history.empty()) {
            int screen = _screens.back();
            int total = (int)_history.back().entries.size();
            int nextFirst = firstEntryOfScreen(screen + 1);
            if (nextFirst < total || loadMore())
                _screens.back() = screen + 1;
        }
        showList(wm);
        return true;
    case OPDS_CMD_SEARCH:
        keyBuffer[0] = 0;
        pbOpenKeyboard(_("Search: author or title"), keyBuffer, sizeof(keyBuffer) - 1, searchKeyboardHandler);
        return true;
    case OPDS_CMD_SEARCH_APPLY:
        if (!_history.empty() && !_history.back().searchTemplate.empty() && keyBuffer[0]) {
            std::string url = opds::makeSearchUrl(_history.back().searchTemplate, keyBuffer);
            load(url, true);
        }
        showList(wm);
        return true;
    case OPDS_CMD_SET_URL:
        editUrl(wm);
        return true;
    case OPDS_CMD_SET_URL_APPLY:
        {
            lString16 url = Utf8ToUnicode(lString8(keyBuffer));
            url.trim();
            if (!url.empty()) {
                if (url.pos("://") < 0)
                    url = lString16("https://") + url;
                main_win->getProps()->setString(PROP_OPDS_URL, url);
                main_win->saveSettings(lString16::empty_str);
                _history.clear();
                _screens.clear();
                if (load(rootUrl(), true))
                    showList(wm);
            }
        }
        return true;
    case OPDS_CMD_INFO:
        showInfo();
        return true;
    case OPDS_CMD_FIRST + 9999: // empty filler row
        showList(wm);
        return true;
    default:
        break;
    }

    if (command >= OPDS_CMD_ENTRY && command < OPDS_CMD_FORMAT) {
        int index = command - OPDS_CMD_ENTRY;
        if (_history.empty() || index >= (int)_history.back().entries.size())
            return true;
        const opds::Entry & e = _history.back().entries[index];
        if (e.isBook()) {
            showBook(wm, index);
        } else if (!e.navigationHref.empty()) {
            std::string href = e.navigationHref;
            load(href, true);
            showList(wm);
        }
        return true;
    }
    if (command >= OPDS_CMD_FORMAT && command < OPDS_CMD_RELATED) {
        download(wm, command - OPDS_CMD_FORMAT);
        return true;
    }
    if (command >= OPDS_CMD_RELATED && command <= OPDS_CMD_LAST) {
        const opds::Entry * e = currentBook();
        int k = command - OPDS_CMD_RELATED;
        if (e && k < (int)e->related.size()) {
            std::string href = e->related[k].href;
            if (load(href, true)) {
                showList(wm);
                return true;
            }
        }
        showBook(wm, _bookIndex);
        return true;
    }
    return false;
}
