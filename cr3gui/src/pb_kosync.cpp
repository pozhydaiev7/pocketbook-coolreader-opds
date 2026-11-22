// The position sync of KOReader in CoolReader on PocketBook. See pb_kosync.h.
#include "pb_kosync.h"

#include <inkview.h>
#include <cri18n.h>
#include <lvstring.h>
#include <lvtinydom.h>
#include <lvdocview.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#include "mainwnd.h"
#include "cr3pocketbook.h"
#include "opds/opdsmenu.h"
#include "web.h"
#ifdef CR3_PAGEDOC
#include "pb_pageview.h"
#include "pb_pdfbook.h"
#endif

extern V3DocViewWin * main_win;

using namespace opdsui;

namespace {

lString32 tr(const char * s)
{
    return Utf8ToUnicode(lString8(_(s)));
}

void serverKeyboardHandler(char * s)
{
    executeCommand((s && *s) ? PB_KOSYNC_CMD_SERVER_APPLY : PB_KOSYNC_CMD_MENU, 0);
}

void userKeyboardHandler(char * s)
{
    executeCommand((s && *s) ? PB_KOSYNC_CMD_USER_APPLY : PB_KOSYNC_CMD_MENU, 0);
}

void passwordKeyboardHandler(char * s)
{
    executeCommand((s && *s) ? PB_KOSYNC_CMD_PASSWORD_APPLY : PB_KOSYNC_CMD_MENU, 0);
}

// "Yes" of the question of a place of another device
void offeredHandler(int button)
{
    if (button == 1)
        executeCommand(PB_KOSYNC_CMD_GO_OFFERED, 0);
}

// the timers: a book opened or the program back (Wi-Fi turned on for it), Wi-Fi turned on
void pullConnectTimer()
{
    PbKosync::instance().autoPull(true);
}

void pullTimer()
{
    PbKosync::instance().autoPull(false);
}

bool networkOn()
{
    return (QueryNetwork() & NET_CONNECTED) != 0;
}

// The firmware may take the screen to connect (its dialog of the networks): the program in the
// background and back meanwhile is no book closed and opened (pbNetConnecting, cr3pocketbook.h).

// Wi-Fi through the firmware for an action asked by the reader (the last network or its dialog)
bool ensureNetwork()
{
    if (networkOn())
        return true;
    const int rc = pbNetConnect();
    CRLog::info("kosync: NetConnect(NULL) = %d, QueryNetwork() = 0x%x", rc, QueryNetwork());
    return rc == NET_OK && networkOn();
}

// Wi-Fi for the automatic sync: KOReader turns it on only where that asks nothing of the reader
// (wifi_enable_action "turn_on"); here through NetConnectSilent of the firmware, looked up at run
// time (not every firmware has it). Without it the automatic sync waits for Wi-Fi turned on.
bool connectQuietly()
{
    if (networkOn())
        return true;
    typedef int (*NetConnectSilentFn)(const char *);
    static NetConnectSilentFn connectSilent = NULL;
    static bool looked = false;
    if (!looked) {
        looked = true;
        void * lib = dlopen("libinkview.so", RTLD_LAZY);
        if (lib != NULL)
            *(void **)(&connectSilent) = dlsym(lib, "NetConnectSilent");
        CRLog::info("kosync: NetConnectSilent %s", connectSilent ? "found" : "not found: no Wi-Fi turned on by the sync");
    }
    if (connectSilent == NULL)
        return false;
    ShowHourglassForce();
    pbNetConnecting = true;
    const int rc = connectSilent(NULL);
    pbNetConnecting = false;
    HideHourglass();
    CRLog::info("kosync: NetConnectSilent(NULL) = %d, QueryNetwork() = 0x%x", rc, QueryNetwork());
    return networkOn();
}

// The open book: its file (the one given to the program, an archive as it is, as KOReader takes
// it) and its place as KOReader sends it: the XPointer of the top of the page, the percentage of
// the page (the page from 1 out of the pages, as KOReader reckons it in the page mode).

bool currentBook(lString32 & file, kosync::Progress & place)
{
    if (main_win == NULL)
        return false;
#ifdef CR3_PAGEDOC
    // the pages of a PDF or a DjVu, and a book made of one: the file and its page
    if (PbPageWindow::instance != NULL) {
        const int pages = PbPageWindow::instance->pageCount();
        if (pages < 1)
            return false;
        file = PbPageWindow::instance->path();
        kosync::pagePlace(PbPageWindow::instance->page(), pages, place);
        return true;
    }
    int page = 0, pages = 0;
    if (pbPdfBookPlace(file, page, pages)) {
        kosync::pagePlace(page, pages, place);
        return true;
    }
#endif
    LVDocView * view = main_win->getDocView();
    if (view == NULL || view->getDocument() == NULL || view->getPageCount() < 1)
        return false;
    file = main_win->getBookFileName();
    if (file.empty())
        return false;
#ifdef CR3_PAGEDOC
    // a book made of a file that is no longer the one of the window: the place is not of that file
    if (pbPdfBookIsMade(file))
        return false;
#endif
    ldomXPointer xp = view->getBookmark();
    place.progress = xp.isNull() ? std::string() : std::string(UnicodeToUtf8(xp.toString()).c_str());
    place.percentage = kosync::roundPercent((double)(view->getCurPage() + 1) / view->getPageCount());
    return true;
}

std::string documentOf(const lString32 & file)
{
    return kosync::partialMd5(UnicodeToUtf8(file).c_str());
}

// The book goes to a place: its XPointer, or its percentage when the XPointer is not one of this
// book (another file, a place of a PDF read as pages).
bool goToPlace(const kosync::Progress & place)
{
#ifdef CR3_PAGEDOC
    if (PbPageWindow::instance != NULL) {
        const int page = kosync::pageOfPlace(place, PbPageWindow::instance->pageCount());
        if (page < 0)
            return false;
        PbPageWindow::instance->showPage(page);
        main_win->getWindowManager()->update(true);
        return true;
    }
    lString32 source;
    int now = 0, pages = 0;
    if (pbPdfBookPlace(source, now, pages)) {
        const int page = kosync::pageOfPlace(place, pages);
        if (page < 0 || !pbPdfBookGoPage(page))
            return false;
        main_win->getWindowManager()->update(true);
        return true;
    }
#endif
    LVDocView * view = main_win != NULL ? main_win->getDocView() : NULL;
    if (view == NULL || view->getDocument() == NULL)
        return false;
    ldomXPointer xp;
    if (!place.progress.empty() && place.progress[0] == '/')
        xp = view->getDocument()->createXPointer(Utf8ToUnicode(place.progress.c_str()));
    if (!xp.isNull()) {
        view->goToBookmark(xp);
    } else if (place.percentage > 0) {
        int page = (int)(place.percentage * view->getPageCount() + 0.5) - 1;
        if (page < 0)
            page = 0;
        if (page >= view->getPageCount())
            page = view->getPageCount() - 1;
        view->goToPage(page);
    } else {
        return false;
    }
    main_win->getWindowManager()->update(true);
    return true;
}

lString32 percentText(double percentage)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", (int)(percentage * 100 + 0.5));
    return lString32(buf);
}

const char * strategyName(int strategy)
{
    return strategy == kosync::STRATEGY_SILENT ? "Go there"
        : strategy == kosync::STRATEGY_DISABLE ? "Ignore" : "Ask";
}

// ask, go, ignore, ask...
int nextStrategy(int strategy)
{
    return strategy == kosync::STRATEGY_PROMPT ? kosync::STRATEGY_SILENT
        : strategy == kosync::STRATEGY_SILENT ? kosync::STRATEGY_DISABLE : kosync::STRATEGY_PROMPT;
}

std::string queueKey(int i)
{
    char key[64];
    snprintf(key, sizeof(key), "%s%d", PROP_KOSYNC_QUEUE, i);
    return key;
}

// the lines of the queue as the settings file keeps them, the oldest first
std::vector<std::string> readQueue(CRPropRef props)
{
    std::vector<std::string> lines;
    for (int i = 1; i <= KOSYNC_QUEUE_MAX; i++) {
        const std::string line = UnicodeToUtf8(props->getStringDef(queueKey(i).c_str(), "")).c_str();
        if (!line.empty())
            lines.push_back(line);
    }
    return lines;
}

void writeQueue(CRPropRef props, const std::vector<std::string> & lines)
{
    for (int i = 1; i <= KOSYNC_QUEUE_MAX; i++) {
        const std::string key = queueKey(i);
        const lString32 value = i <= (int)lines.size() ? Utf8ToUnicode(lines[i - 1].c_str()) : lString32::empty_str;
        // a slot never used is not written
        if (!value.empty() || props->hasProperty(key.c_str()))
            props->setString(key.c_str(), value);
    }
    main_win->saveSettings(lString32::empty_str);
}

} // namespace

PbKosync & PbKosync::instance()
{
    static PbKosync sync;
    return sync;
}

void PbKosync::setDefaults(CRPropRef props)
{
    props->setStringDef(PROP_KOSYNC_SERVER, kosync::DEFAULT_SERVER);
    props->setIntDef(PROP_KOSYNC_ENABLED, 0);
    props->setIntDef(PROP_KOSYNC_FORWARD, kosync::DEFAULT_FORWARD);
    props->setIntDef(PROP_KOSYNC_BACKWARD, kosync::DEFAULT_BACKWARD);
}

bool PbKosync::enabled()
{
    return main_win != NULL && main_win->getProps()->getIntDef(PROP_KOSYNC_ENABLED, 0) != 0;
}

kosync::Account PbKosync::account()
{
    CRPropRef props = main_win->getProps();
    kosync::Account a;
    a.server = UnicodeToUtf8(props->getStringDef(PROP_KOSYNC_SERVER, kosync::DEFAULT_SERVER)).c_str();
    a.user = UnicodeToUtf8(props->getStringDef(PROP_KOSYNC_USER, "")).c_str();
    a.key = UnicodeToUtf8(props->getStringDef(PROP_KOSYNC_KEY, "")).c_str();
    return a;
}

kosync::Account PbKosync::accountFor(bool progress)
{
    kosync::Account a = account();
    if (progress) {
        // KOReader: "very tight timeouts to avoid blocking for too long"
        a.connectTimeout = 2;
        a.timeout = 5;
    }
    a.caInfo = web::caFile();
    if (!_lookupLogged) {
        _lookupLogged = true;
        CRLog::info("kosync: libcurl looks up names %s", kosync::asyncNameLookup()
                    ? "in a thread of its own (the limits hold for the lookup)"
                    : "in the request (a lookup is not limited by the timeouts)");
    }
    return a;
}

bool PbKosync::loggedIn()
{
    kosync::Account a = account();
    return !a.user.empty() && !a.key.empty();
}

std::string PbKosync::deviceModel()
{
    const char * model = GetDeviceModel();
    return model && *model ? model : "PocketBook";
}

// 32 hex digits made once, as KOReader keeps an id of its device
std::string PbKosync::deviceId()
{
    CRPropRef props = main_win->getProps();
    lString32 id = props->getStringDef(PROP_KOSYNC_DEVICE_ID, "");
    if (id.length() != 32) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        srand((unsigned)(tv.tv_sec ^ (tv.tv_usec << 12) ^ getpid()));
        char buf[40];
        for (int i = 0; i < 32; i++)
            buf[i] = "0123456789ABCDEF"[rand() & 15];
        buf[32] = 0;
        id = lString32(buf);
        props->setString(PROP_KOSYNC_DEVICE_ID, id);
        main_win->saveSettings(lString32::empty_str);
    }
    return std::string(UnicodeToUtf8(id).c_str());
}

void PbKosync::pageShown(int page)
{
    if (_shownPage >= 0 && page != _shownPage)
        _lastTurn = (long)time(NULL);
    _shownPage = page;
}

void PbKosync::message(const lString32 & text)
{
    // after the screen the command shows is drawn, or it is drawn over the message
    pbMessageLater(ICON_INFORMATION, "CoolReader", UnicodeToUtf8(text).c_str(), 3000);
}

void PbKosync::editText(const char * title, const lString32 & value, void (*handler)(char *))
{
    lString8 text = UnicodeToUtf8(value);
    strncpy(keyBuffer, text.c_str(), sizeof(keyBuffer) - 1);
    keyBuffer[sizeof(keyBuffer) - 1] = 0;
    pbOpenKeyboard(title, keyBuffer, sizeof(keyBuffer) - 1, handler);
}

// ---- the queue

int PbKosync::queueCount()
{
    return (int)readQueue(main_win->getProps()).size();
}

void PbKosync::keepInQueue(const std::string & document, const kosync::Progress & place)
{
    CRPropRef props = main_win->getProps();
    std::vector<std::string> lines = readQueue(props);
    // the time the place was read here: the last page turned, or the opening of the book
    kosync::queueKeep(lines, document, place, _lastTurn != 0 ? _lastTurn : _openedAt, KOSYNC_QUEUE_MAX);
    writeQueue(props, lines);
    CRLog::info("kosync: the place %s of %s is kept to be sent later (%d kept)", place.progress.c_str(),
                document.c_str(), (int)lines.size());
}

// The places kept are sent, the oldest first (kosync::queueDrain).
int PbKosync::drainQueue()
{
    CRPropRef props = main_win->getProps();
    std::vector<std::string> lines = readQueue(props);
    if (lines.empty())
        return 0;
    const size_t kept = lines.size();
    int newer = 0;
    const int sent = kosync::queueDrain(accountFor(true), deviceModel(), deviceId(), lines, &newer);
    writeQueue(props, lines);
    CRLog::info("kosync: of the %d places kept %d sent, %d left for a newer place on the server, %d still kept",
                (int)kept, sent, newer, (int)lines.size());
    return sent + newer;
}

// ---- push and pull

bool PbKosync::pushPlace(const std::string & document, kosync::Progress place, bool interactive)
{
    place.device = deviceModel();
    place.deviceId = deviceId();
    if (interactive)
        ShowHourglassForce();
    kosync::Result r = kosync::push(accountFor(true), document, place);
    if (interactive)
        HideHourglass();
    CRLog::info("kosync: place %s (%.4f) of %s sent: HTTP %ld %s", place.progress.c_str(), place.percentage,
                document.c_str(), r.http, r.error.c_str());
    if (r.ok()) {
        _sentDocument = document;
        _sentProgress = place.progress;
        // a place kept of the same book is older: it is not sent over this one later
        CRPropRef props = main_win->getProps();
        std::vector<std::string> lines = readQueue(props);
        if (kosync::queueDrop(lines, document))
            writeQueue(props, lines);
    } else if (r.http != 401 && r.http != 403) {
        // KOReader: queued for retry unless it is an auth failure (401; a server may say 403)
        keepInQueue(document, place);
    }
    if (interactive)
        message(r.ok() ? tr("The place has been sent.")
                : (r.http == 401 || r.http == 403) ? tr("The server did not accept the login and the password.")
                : r.http >= 300 && r.http < 400 ? tr("The server sends the requests to another address. Check the address of the server.")
                : web::isCertificateError(r.curl) ? tr("The certificate of the server is not accepted.")
                : tr("The place was not sent: the server does not answer. It will be sent later."));
    return r.ok();
}

bool PbKosync::pullPlace(const std::string & document, const kosync::Progress & here, bool interactive)
{
    if (interactive)
        ShowHourglassForce();
    kosync::Progress there;
    kosync::Result r = kosync::pull(accountFor(true), document, there);
    if (interactive)
        HideHourglass();
    CRLog::info("kosync: place of %s asked: HTTP %ld, found %d, %s (%.4f) at %ld from %s %s", document.c_str(),
                r.http, there.found ? 1 : 0, there.progress.c_str(), there.percentage, there.timestamp,
                there.device.c_str(), r.error.c_str());
    if (!r.ok()) {
        if (interactive)
            message((r.http == 401 || r.http == 403) ? tr("The server did not accept the login and the password.")
                    : r.http >= 300 && r.http < 400 ? tr("The server sends the requests to another address. Check the address of the server.")
                    : web::isCertificateError(r.curl) ? tr("The certificate of the server is not accepted.")
                    : tr("The server does not answer. Check its address and the network."));
        return false;
    }
    CRPropRef props = main_win->getProps();
    bool newer = false;
    const kosync::Decision d = kosync::decide(here, there, deviceModel(), deviceId(), _lastTurn,
                                              props->getIntDef(PROP_KOSYNC_FORWARD, kosync::DEFAULT_FORWARD),
                                              props->getIntDef(PROP_KOSYNC_BACKWARD, kosync::DEFAULT_BACKWARD),
                                              interactive, &newer);
    CRLog::info("kosync: decision %d (newer %d, last page turned at %ld)", (int)d, newer ? 1 : 0, _lastTurn);
    switch (d) {
    case kosync::DECIDE_NONE:
        if (interactive)
            message(tr("The server has no place of this book yet."));
        return false;
    case kosync::DECIDE_FROM_HERE:
        if (interactive)
            message(tr("The place on the server is the one sent from this device."));
        return false;
    case kosync::DECIDE_SAME:
        if (interactive)
            message(tr("The place is the same already."));
        return false;
    case kosync::DECIDE_IGNORE:
        return false;
    case kosync::DECIDE_GO:
        if (goToPlace(there)) {
            movedTo();
            message(tr("The book has gone to the place sent by another device."));
            return true;
        }
        return false;
    case kosync::DECIDE_ASK: {
        _offered = there;
        _offeredDocument = document;
        _offeredValid = true;
        lString32 text = newer ? tr("Go to the newer place $1 sent by \"$2\"?")
                               : tr("Go to the older place $1 sent by \"$2\"?");
        text.replaceParam(1, percentText(there.percentage));
        text.replaceParam(2, Utf8ToUnicode(there.device.c_str()));
        static lString8 question;
        question = UnicodeToUtf8(text);
        Dialog(ICON_QUESTION, const_cast<char *>("CoolReader"), question.c_str(), _("Yes"), _("No"), offeredHandler);
        return false;
    }
    }
    return false;
}

// ---- what a book does

void PbKosync::bookOpened()
{
    _offeredValid = false;
    _pagesOverBook = false;
    // the pages of the other view are not those of this one: the first page shown is no turn
    _shownPage = -1;
    // the same file in another view: the same reading goes on
    if (_held)
        return;
    _lastTurn = 0;
    _openedAt = (long)time(NULL);
    _pushAt = _pullAt = 0;
    _sentDocument.clear();
    _sentProgress.clear();
    if (!enabled() || !loggedIn())
        return;
    // KOReader asks right after the book is ready; here after it is on the screen
    SetWeakTimer("KosyncPull", pullConnectTimer, 1500);
}

void PbKosync::foregrounded()
{
    if (pbNetConnecting || !enabled() || !loggedIn())
        return;
    lString32 file;
    kosync::Progress here;
    if (!currentBook(file, here))
        return;
    SetWeakTimer("KosyncPull", pullConnectTimer, 1000);
}

void PbKosync::networkConnected()
{
    if (!enabled() || !loggedIn())
        return;
    SetWeakTimer("KosyncPull", pullTimer, 500);
}

void PbKosync::autoPull(bool connect)
{
    if (!enabled() || !loggedIn())
        return;
    lString32 file;
    kosync::Progress here;
    const bool book = currentBook(file, here);
    const long now = (long)time(NULL);
    if (book && kosync::debounced(now, _pullAt))
        return;
    if (!(connect ? connectQuietly() : networkOn()))
        return;
    // the places kept first, then the place of the book (KOReader: drainQueue, then getProgress)
    drainQueue();
    if (!book)
        return;
    const std::string document = documentOf(file);
    if (document.empty())
        return;
    _pullAt = now;
    pullPlace(document, here, false);
}

#ifdef CR3_PAGEDOC
void PbKosync::pagesOpened(int page)
{
    bookOpened();
    _pagesOverBook = true;
    // The page the file is opened at is on the screen: the next one shown is a page turned. (The
    // window tells its pages as they are turned, not as they are drawn; without this the first
    // turn only gave the page to turn from, and a file closed after one page turned sent nothing.)
    // Held, the page comes from the other view right after (showPage).
    if (!_held)
        _shownPage = page;
}

void PbKosync::pagesClosing()
{
    bookClosing();
}
#endif

void PbKosync::bookClosing()
{
    if (_held || pbNetConnecting || !enabled() || !loggedIn())
        return;
#ifdef CR3_PAGEDOC
    // the pages shown over the book made of the same file sent the newer place when they went
    if (PbPageWindow::instance == NULL && _pagesOverBook && pbPdfBookIsOpen())
        return;
#endif
    lString32 file;
    kosync::Progress place;
    if (!currentBook(file, place))
        return;
    // no page turned since the book was opened: its place is the one it was opened at, nothing
    // read here is to be sent (and a newer place of another device is not written over)
    if (_lastTurn == 0)
        return;
    const std::string document = documentOf(file);
    if (document.empty())
        return;
    const long now = (long)time(NULL);
    // KOReader: online, the place is sent; otherwise it is queued, not waited for
    switch (kosync::closeAction(networkOn(), kosync::debounced(now, _pushAt),
                                document == _sentDocument && place.progress == _sentProgress)) {
    case kosync::CLOSE_SKIP:
        return;
    case kosync::CLOSE_KEEP:
        keepInQueue(document, place);
        return;
    case kosync::CLOSE_PUSH:
        _pushAt = now;
        pushPlace(document, place, false);
        return;
    }
}

void PbKosync::sendNow()
{
    if (!enabled()) {
        message(tr("The reading position sync is off: Settings, Reading position sync."));
        return;
    }
    if (!loggedIn()) {
        message(tr("Set the login of the sync server first: Settings, Reading position sync."));
        return;
    }
    lString32 file;
    kosync::Progress place;
    if (!currentBook(file, place))
        return;
    const std::string document = documentOf(file);
    if (document.empty())
        return;
    if (!ensureNetwork()) {
        keepInQueue(document, place);
        message(tr("No network connection. The place will be sent later."));
        return;
    }
    pushPlace(document, place, true);
}

bool PbKosync::getNow()
{
    if (!enabled()) {
        message(tr("The reading position sync is off: Settings, Reading position sync."));
        return false;
    }
    if (!loggedIn()) {
        message(tr("Set the login of the sync server first: Settings, Reading position sync."));
        return false;
    }
    lString32 file;
    kosync::Progress here;
    if (!currentBook(file, here))
        return false;
    const std::string document = documentOf(file);
    if (document.empty())
        return false;
    if (!ensureNetwork()) {
        message(tr("No network connection. Turn on Wi-Fi and try again."));
        return false;
    }
    return pullPlace(document, here, true);
}

// ---- the screen

void PbKosync::open(CRGUIWindowManager * wm, bool fromSettings)
{
    _fromSettings = fromSettings;
    showMenu(wm);
}

void PbKosync::showMenu(CRGUIWindowManager * wm)
{
    CRPropRef props = main_win->getProps();
    kosync::Account a = account();
    lvRect rc = wm->getScreen()->getRect();
    OpdsMenu * menu = new OpdsMenu(wm, tr("Reading position sync"), rc, false);
    menu->setupRows(true);
    menu->setCloseCommand(PB_KOSYNC_CMD_CLOSE);
    menu->add(PB_KOSYNC_CMD_CLOSE, lString32(MARK_PREV) + (_fromSettings ? tr("Back to settings") : tr("Back to the book")));
    // The switch of the sync is the one of the settings menu only, and every row is here at once
    // (the owner: a second switch here, rows that came only after the login and the switch).
    menu->add(PB_KOSYNC_CMD_SERVER, tr("Server..."), Utf8ToUnicode(a.server.c_str()));
    menu->add(PB_KOSYNC_CMD_USER, tr("Login..."), a.user.empty() ? tr("Not set") : Utf8ToUnicode(a.user.c_str()));
    menu->add(PB_KOSYNC_CMD_PASSWORD, tr("Password..."), a.key.empty() ? tr("Not set") : tr("Set"));
    menu->add(PB_KOSYNC_CMD_FORWARD,
              tr("Newer place from another device") + U": "
                  + tr(strategyName(props->getIntDef(PROP_KOSYNC_FORWARD, kosync::DEFAULT_FORWARD))),
              tr("Read there after the last page turned here"));
    menu->add(PB_KOSYNC_CMD_BACKWARD,
              tr("Older place from another device") + U": "
                  + tr(strategyName(props->getIntDef(PROP_KOSYNC_BACKWARD, kosync::DEFAULT_BACKWARD))),
              tr("Read there before the last page turned here"));
    menu->add(PB_KOSYNC_CMD_CHECK, tr("Check the login"), tr("The server is asked whether it knows you"));
    menu->add(PB_KOSYNC_CMD_REGISTER, tr("Register on the server"), tr("A new user with this login and password"));
    lString32 file;
    kosync::Progress place;
    menu->add(PB_KOSYNC_CMD_PUSH, tr("Send the place of the book"),
              currentBook(file, place) ? percentText(place.percentage) : lString32::empty_str);
    menu->add(PB_KOSYNC_CMD_PULL, tr("Get the place of the book"), tr("The place sent by another device"));
    const int kept = queueCount();
    if (kept > 0) {
        lString32 text = tr("Places not sent yet: $1");
        text.replaceParam(1, lString32::itoa(kept));
        menu->add(PB_KOSYNC_CMD_QUEUE, text, tr("Send them now"));
    }
    menu->add(PB_KOSYNC_CMD_LOGOUT, tr("Forget login and password"));
    activate(wm, menu);
}

bool PbKosync::onCommand(CRGUIWindowManager * wm, int command, int params)
{
    (void)params;
    CRPropRef props = main_win->getProps();
    switch (command) {
    case PB_KOSYNC_CMD_MENU:
        showMenu(wm);
        return true;
    case PB_KOSYNC_CMD_CLOSE:
        // opened from the settings menu: back to that menu, at its item
        if (_fromSettings) {
            _fromSettings = false;
            wm->postCommand(MCMD_SETTINGS, PB_CMD_KOSYNC);
        }
        return true;
    case PB_KOSYNC_CMD_SERVER:
        editText(_("Address of the sync server"), props->getStringDef(PROP_KOSYNC_SERVER, kosync::DEFAULT_SERVER),
                 serverKeyboardHandler);
        return true;
    case PB_KOSYNC_CMD_USER:
        editText(_("Login on the sync server"), props->getStringDef(PROP_KOSYNC_USER, ""), userKeyboardHandler);
        return true;
    case PB_KOSYNC_CMD_PASSWORD:
        // typed anew: only its MD5 is kept
        editText(_("Password on the sync server"), lString32::empty_str, passwordKeyboardHandler);
        return true;
    case PB_KOSYNC_CMD_SERVER_APPLY: {
        lString32 url = Utf8ToUnicode(lString8(keyBuffer));
        url.trim();
        const int scheme = url.pos("://");
        if (scheme < 0)
            url = lString32("https://") + url;
        else
            url = url.substr(0, scheme).lowercase() + url.substr(scheme);
        props->setString(PROP_KOSYNC_SERVER, url);
        main_win->saveSettings(lString32::empty_str);
        showMenu(wm);
        return true;
    }
    case PB_KOSYNC_CMD_USER_APPLY: {
        lString32 user = Utf8ToUnicode(lString8(keyBuffer));
        user.trim();
        props->setString(PROP_KOSYNC_USER, user);
        main_win->saveSettings(lString32::empty_str);
        showMenu(wm);
        return true;
    }
    case PB_KOSYNC_CMD_PASSWORD_APPLY:
        props->setString(PROP_KOSYNC_KEY, Utf8ToUnicode(kosync::md5Hex(keyBuffer).c_str()));
        memset(keyBuffer, 0, sizeof(keyBuffer));
        main_win->saveSettings(lString32::empty_str);
        showMenu(wm);
        return true;
    case PB_KOSYNC_CMD_CHECK:
    case PB_KOSYNC_CMD_REGISTER: {
        if (!loggedIn()) {
            message(tr("Set the login and the password first."));
            showMenu(wm);
            return true;
        }
        if (!ensureNetwork()) {
            // (the row closed the screen: it is shown again, or the reader is back in the book)
            message(tr("No network connection. Turn on Wi-Fi and try again."));
            showMenu(wm);
            return true;
        }
        ShowHourglassForce();
        const bool reg = command == PB_KOSYNC_CMD_REGISTER;
        kosync::Result r = reg ? kosync::registerUser(accountFor(false)) : kosync::authorize(accountFor(false));
        HideHourglass();
        CRLog::info("kosync: %s: HTTP %ld %s", reg ? "register" : "authorize", r.http, r.error.c_str());
        if (r.http == 0)
            message(web::isCertificateError(r.curl) ? tr("The certificate of the server is not accepted.")
                    : tr("The server does not answer. Check its address and the network."));
        else if (r.http >= 300 && r.http < 400)
            message(tr("The server sends the requests to another address. Check the address of the server."));
        else if (reg)
            message(r.http == 201 ? tr("Registered: the server knows you now.")
                    : r.http == 402 ? tr("The server has a user with this login already.")
                    : tr("The server did not register you (it may take no new users)."));
        else
            message(r.ok() ? tr("The login is right.") : tr("The server did not accept the login and the password."));
        showMenu(wm);
        return true;
    }
    case PB_KOSYNC_CMD_FORWARD:
    case PB_KOSYNC_CMD_BACKWARD: {
        const char * prop = command == PB_KOSYNC_CMD_FORWARD ? PROP_KOSYNC_FORWARD : PROP_KOSYNC_BACKWARD;
        const int def = command == PB_KOSYNC_CMD_FORWARD ? kosync::DEFAULT_FORWARD : kosync::DEFAULT_BACKWARD;
        props->setInt(prop, nextStrategy(props->getIntDef(prop, def)));
        main_win->saveSettings(lString32::empty_str);
        showMenu(wm);
        return true;
    }
    case PB_KOSYNC_CMD_QUEUE: {
        if (!ensureNetwork()) {
            // (the row closed the screen: it is shown again, or the reader is back in the book)
            message(tr("No network connection. Turn on Wi-Fi and try again."));
            showMenu(wm);
            return true;
        }
        const int kept = queueCount();
        ShowHourglassForce();
        const int sent = drainQueue();
        HideHourglass();
        lString32 text = tr("Places sent: $1 of $2.");
        text.replaceParam(1, lString32::itoa(sent));
        text.replaceParam(2, lString32::itoa(kept));
        message(text);
        showMenu(wm);
        return true;
    }
    // the rows of the screen: the screen again after them (the owner: "Send" went back to the
    // book), but after a place taken, which the book shows
    case PB_KOSYNC_CMD_PUSH:
        sendNow();
        showMenu(wm);
        return true;
    case PB_KOSYNC_CMD_PULL:
        if (!getNow())
            showMenu(wm);
        return true;
    case PB_KOSYNC_CMD_LOGOUT:
        props->setString(PROP_KOSYNC_USER, lString32::empty_str);
        props->setString(PROP_KOSYNC_KEY, lString32::empty_str);
        main_win->saveSettings(lString32::empty_str);
        showMenu(wm);
        return true;
    case PB_KOSYNC_CMD_GO_OFFERED: {
        // the book on the screen is still the one the place was asked for
        lString32 file;
        kosync::Progress now;
        if (_offeredValid && currentBook(file, now) && documentOf(file) == _offeredDocument
                && goToPlace(_offered))
            movedTo();
        _offeredValid = false;
        return true;
    }
    default:
        return true;
    }
}
