// OPDS catalog browser for the CoolReader PocketBook port. See opdsbrowser.h.
#include "opdsbrowser.h"

#include <inkview.h>
#include <cri18n.h>
#include <lvstring.h>
#include <lvstream.h>
#include <lvfntman.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>

#include "mainwnd.h"
#include "fsmenu.h"
#include "settings.h"
#include "cr3pocketbook.h"
#include "web.h"
#include "opdsmenu.h"

extern V3DocViewWin * main_win;

using namespace opdsui;

namespace {

const size_t OPDS_MAX_ENTRIES = OPDS_CMD_FORMAT - OPDS_CMD_ENTRY;

// "Year: 2026" style lines that Flibusta appends to the description.
bool isFieldLine(const lString16 & paragraph, int & colon)
{
    colon = paragraph.pos(L": ");
    if (colon < 2 || colon > 30 || paragraph.length() > 120)
        return false;
    for (int i = 0; i < colon; i++) {
        lChar16 ch = paragraph[i];
        if (ch == L'.' || ch == L'!' || ch == L'?' || ch == L',')
            return false;
    }
    return true;
}

void urlKeyboardHandler(char * s)
{
    executeCommand((s && *s) ? OPDS_CMD_SET_URL_APPLY : OPDS_CMD_SHOW_LIST, 0);
}

void searchKeyboardHandler(char * s)
{
    executeCommand((s && *s) ? OPDS_CMD_SEARCH_APPLY : OPDS_CMD_SHOW_LIST, 0);
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

// ------------------------------------------------------------ firmware library scan
// The firmware library lists files from its database, which the system scanner fills in on
// USB disconnect or reboot. BookPreparing/BookReady did not add a downloaded file on
// InkPad 4 (6.11.1683), so after a download the scanner is started the way inkshelf does it
// (github.com/N-Combinator/inkshelf, library_scan.c): from a detached supervisor process that
// outlives CoolReader, stops the scanner once it reports "Scan finished" (it may stay alive
// afterwards) and kills it after two minutes. The supervisor is a fork of a multithreaded
// process, so it only makes async-signal-safe calls.

const char * const SCAN_LOG = "/tmp/cr3-library-scan.log";
const int SCAN_TIMEOUT_MS = 120000;

void sleepMs(int ms)
{
    struct timespec delay;
    delay.tv_sec = ms / 1000;
    delay.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {}
}

// True when the scanner output contains its completion line.
bool scanFinished()
{
    int fd = open(SCAN_LOG, O_RDONLY);
    if (fd < 0)
        return false;
    static const char marker[] = "Scan finished";
    const size_t keep = sizeof(marker) - 1;
    char buf[1024];
    size_t have = 0;
    bool found = false;
    for (;;) {
        ssize_t n = read(fd, buf + have, sizeof(buf) - have);
        if (n <= 0)
            break;
        have += (size_t)n;
        for (size_t i = 0; i + keep <= have && !found; i++)
            if (memcmp(buf + i, marker, keep) == 0)
                found = true;
        if (found)
            break;
        if (have > keep) { // keep a tail so a marker split between reads is still found
            memmove(buf, buf + have - keep, keep);
            have = keep;
        }
    }
    close(fd);
    return found;
}

// Supervisor body: runs the scanner and waits for it. Never returns.
void superviseScanner(const char * scanner)
{
    setsid();
    signal(SIGCHLD, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGHUP, SIG_IGN);
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);
    for (int fd = 3; fd < 1024; fd++) // sockets, files and pipes of the reader
        close(fd);
    int nullfd = open("/dev/null", O_RDWR);
    if (nullfd >= 0) {
        dup2(nullfd, 0);
        dup2(nullfd, 1);
        dup2(nullfd, 2);
        if (nullfd > 2) close(nullfd);
    }
    pid_t pid = fork();
    if (pid < 0)
        _exit(1);
    if (pid == 0) {
        int out = open(SCAN_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out >= 0) {
            dup2(out, 1);
            dup2(out, 2);
            if (out > 2) close(out);
        }
        execl(scanner, scanner, (char *)NULL);
        _exit(127);
    }
    int status;
    for (int waited = 0; ; waited += 200) {
        pid_t rc = waitpid(pid, &status, WNOHANG);
        if (rc == pid || (rc < 0 && errno != EINTR))
            _exit(0);
        if (scanFinished() || waited >= SCAN_TIMEOUT_MS) {
            kill(pid, SIGTERM);
            for (int i = 0; i < 20 && waitpid(pid, &status, WNOHANG) == 0; i++)
                sleepMs(100);
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            _exit(0);
        }
        sleepMs(200);
    }
}

// Starts a library scan in the background; does nothing when the firmware has no scanner.
void requestLibraryScan()
{
    const char * scanner = NULL;
    if (access("/ebrmain/bin/scanner.app", X_OK) == 0)
        scanner = "/ebrmain/bin/scanner.app";
    else if (access("/ebrmain/cramfs/bin/scanner.app", X_OK) == 0)
        scanner = "/ebrmain/cramfs/bin/scanner.app";
    if (!scanner) {
        CRLog::info("OPDS: no firmware scanner, the library lists the book after the next scan");
        return;
    }
    pid_t pid = fork();
    if (pid < 0) {
        CRLog::error("OPDS: cannot start the library scan: fork failed");
        return;
    }
    if (pid == 0) {
        // Intermediate child: the supervisor is its child, so it is not left as a zombie
        // of the reader and survives the reader exit.
        pid_t supervisor = fork();
        if (supervisor == 0)
            superviseScanner(scanner);
        _exit(supervisor < 0 ? 1 : 0);
    }
    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    CRLog::info("OPDS: library scan started with %s", scanner);
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

namespace {

// Wraps a paragraph into lines; the first line carries the label and the indent.
void addParagraph(std::vector<InfoLine> & lines, LVFontRef font, LVFontRef labelFont,
                  const lString16 & label, const lString16 & text, int width, int indent)
{
    int firstWidth = width - indent - (label.empty() ? 0 : textWidth(labelFont, label));
    std::vector<lString16> parts = wrapText(font, text, firstWidth, width);
    if (parts.empty() && !label.empty())
        parts.push_back(lString16::empty_str);
    for (size_t i = 0; i < parts.size(); i++) {
        InfoLine line;
        line.font = font;
        line.text = parts[i];
        if (i == 0) {
            line.label = label;
            line.indent = indent;
        }
        lines.push_back(line);
    }
}

void addGap(std::vector<InfoLine> & lines, LVFontRef font)
{
    if (lines.empty() || lines.back().text.empty())
        return;
    InfoLine gap;
    gap.font = font;
    lines.push_back(gap);
}

} // namespace

// Book description as a catalog screen, two lines per row: title and author in bold, then the
// book data ("Genre: ...", and the "Year: ..." lines Flibusta appends to the description) with
// bold labels, then the description paragraphs. A tap anywhere returns to the book screen.
// A firmware dialog is not used here: the reader page repaints over it as soon as the catalog
// menu closes.
void OpdsBrowser::showInfo(CRGUIWindowManager * wm)
{
    const opds::Entry * e = currentBook();
    if (!e) { showList(wm); return; }

    lvRect rc = wm->getScreen()->getRect();
    OpdsMenu * menu = new OpdsMenu(wm, u16(e->title), rc, false);
    int rows = menu->setupRows(true);
    menu->add(OPDS_CMD_SHOW_BOOK, lString16(MARK_PREV) + lString16(_("Back to the book")));

    int width = menu->rowTextWidth();
    std::vector<InfoLine> lines;
    addParagraph(lines, menu->titleFont, menu->labelFont, lString16::empty_str, u16(e->title), width, 0);
    addParagraph(lines, menu->labelFont, menu->labelFont, lString16::empty_str, u16(e->authorsText()), width, 0);
    addGap(lines, menu->subFont);

    if (!e->categories.empty()) {
        lString16 genres;
        for (size_t i = 0; i < e->categories.size(); i++) {
            if (i > 0) genres << L", ";
            genres << u16(e->categories[i]);
        }
        addParagraph(lines, menu->subFont, menu->labelFont, lString16(_("Genre")) + L": ", genres, width, 0);
    }
    lString16 summary = u16(e->summary);
    std::vector<lString16> paragraphs;
    for (int start = 0; start <= summary.length(); ) {
        int end = start;
        while (end < summary.length() && summary[end] != L'\n')
            end++;
        lString16 p = summary.substr(start, end - start);
        p.trim();
        if (!p.empty())
            paragraphs.push_back(p);
        start = end + 1;
    }
    for (size_t i = 0; i < paragraphs.size(); i++) {
        int colon;
        if (isFieldLine(paragraphs[i], colon))
            addParagraph(lines, menu->subFont, menu->labelFont, paragraphs[i].substr(0, colon + 2),
                         paragraphs[i].substr(colon + 2), width, 0);
    }
    addGap(lines, menu->subFont);
    for (size_t i = 0; i < paragraphs.size(); i++) {
        int colon;
        if (!isFieldLine(paragraphs[i], colon))
            addParagraph(lines, menu->bodyFont, menu->labelFont, lString16::empty_str, paragraphs[i],
                         width, menu->bodyFont->getHeight());
    }
    while (!lines.empty() && lines.back().text.empty() && lines.back().label.empty())
        lines.pop_back();

    size_t maxLines = (size_t)(rows - 1) * 2;
    if (lines.size() > maxLines) {
        lines.resize(maxLines);
        InfoLine & last = lines.back();
        last.text << ELLIPSIS;
        last.text = fitText(last.font, last.text, width - last.indent);
    }
    for (size_t i = 0; i < lines.size(); i += 2) {
        OpdsItem * item = new OpdsItem(menu, OPDS_CMD_SHOW_BOOK, lString16::empty_str, lString16::empty_str,
                                       OpdsItem::TEXT);
        item->first = lines[i];
        if (i + 1 < lines.size())
            item->second = lines[i + 1];
        menu->addItem(item);
    }
    activate(wm, menu);
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
        requestLibraryScan();
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
        showInfo(wm);
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
