/**
 * Program update over the network (OTA). See ota_update.h.
 * The screens are the full-screen touch lists of the OPDS catalog (opds/opdsmenu.h).
 */

#include "ota_update.h"

#include <inkview.h>
#include <inkplatform.h>
#include <crengine.h>
#include <cri18n.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "mainwnd.h"
#include "opdsmenu.h"
#include "web.h"

extern V3DocViewWin * main_win;
// language of the interface, as the firmware names it ("ru", "uk", "en")
extern lString32 currentLang;
// cr3pocketbook.cpp: a question of the firmware that the program closes when it is drawn over
void pbQuestion(int icon, const char * title, const char * text, const char * button1, const char * button2,
                void (*handler)(int));

using namespace opdsui;

namespace {

// The certificate of the server is checked with the root certificates of the firmware
// (web::caFile, or the default of libcurl); when that check is refused, the request is made again
// without it, as the OPDS catalogs are read; the log says which way a request went.

const char * const WINDOW_TITLE = "CoolReader";

lString32 tr(const char * s)
{
    return lString32(_(s));
}

std::string utf8(const lString32 & s)
{
    return std::string(UnicodeToUtf8(s).c_str());
}

// text with $1 and $2 replaced
lString32 fill(const char * pattern, const lString32 & p1, const lString32 & p2 = lString32::empty_str)
{
    lString32 s(_(pattern));
    s.replaceParam(1, p1);
    s.replaceParam(2, p2);
    return s;
}

lString32 sizeText(long long bytes)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f ", (double)bytes / (1024.0 * 1024.0));
    return lString32(buf) + tr("MB");
}

lString32 timeText(long long t)
{
    time_t tt = (time_t)t;
    struct tm * tmv = localtime(&tt);
    if (!tmv)
        return lString32::empty_str;
    char buf[32];
    strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", tmv);
    return lString32(buf);
}

void message(int icon, const lString32 & text, int ms)
{
    Message(icon, WINDOW_TITLE, UnicodeToUtf8(text).c_str(), ms);
}

CRPropRef props()
{
    return main_win->getProps();
}

void saveProps()
{
    main_win->saveSettings(lString32::empty_str);
}

ota::Version installedVersion()
{
    return ota::parseVersion(CR_PB_VERSION);
}

bool testVersionsOn()
{
    return props()->getIntDef(PROP_POCKETBOOK_OTA_TEST, installedVersion().test() ? 1 : 0) != 0;
}

bool autoCheckOn()
{
    return props()->getIntDef(PROP_POCKETBOOK_OTA_AUTO, 1) != 0;
}

lString32 onOff(bool on)
{
    return tr(on ? "On" : "Off");
}

bool startsWith(const std::string & s, const char * head)
{
    return s.compare(0, strlen(head), head) == 0;
}

// "2026-12-01" of GitHub as "01.12.2026"
lString32 dateText(const std::string & date)
{
    if (date.size() != 10 || date[4] != '-' || date[7] != '-')
        return u16(date);
    return u16(date.substr(8, 2) + "." + date.substr(5, 2) + "." + date.substr(0, 4));
}

// "01.12.2026  ·  7.0 MB  ·  test  ·  installed"
lString32 releaseInfo(const ota::Release & r)
{
    lString32 s = dateText(r.date);
    if (r.size > 0)
        s << lString32(DOT) << sizeText(r.size);
    if (r.test)
        s << lString32(DOT) << tr("test");
    if (ota::compareVersions(r.parsed, installedVersion()) == 0)
        s << lString32(DOT) << tr("installed");
    return s;
}

void offerHandler(int button)
{
    PbOta::instance().offerAnswered(button);
}

void installHandler(int button)
{
    PbOta::instance().installAnswered(button);
}

void autoCheckTimer()
{
    PbOta::instance().autoCheck();
}

// A window of the firmware (Dialog, Message) takes the screen from the program; what the program
// draws before it gets the screen back is dropped (seen on InkPad 4, firmware 6.11: a list shown
// right after Message() never appeared). What follows an answer in such a window is done by
// these timers, after the window is gone.
#define OTA_AFTER_WINDOW_MS 500

void openCardTimer()
{
    PbOta::instance().openOfferedCard();
}

void downloadTimer()
{
    PbOta::instance().download();
}

// one line for a row of the screen
lString32 oneLine(const lString32 & text)
{
    lString32 s = text;
    s.replace(lString32("\r"), lString32(" "));
    s.replace(lString32("\n"), lString32(" "));
    return s;
}

// A row of plain text, up to two lines
OpdsItem * textRow(OpdsMenu * menu, const lString32 & text)
{
    OpdsItem * item = new OpdsItem(menu, PB_OTA_CMD_LAST, lString32::empty_str, lString32::empty_str, OpdsItem::TEXT);
    int width = menu->rowTextWidth();
    std::vector<lString32> lines = wrapText(menu->bodyFont, oneLine(text), width, width);
    if (lines.size() > 0) {
        item->first.text = lines[0];
        item->first.font = menu->bodyFont;
    }
    if (lines.size() > 1) {
        item->second.text = lines.size() > 2 ? fitText(menu->bodyFont, lines[1] + U" " + lines[2], width) : lines[1];
        item->second.font = menu->bodyFont;
    }
    return item;
}

// The package is downloaded and checked: the program stops, the launcher installs the package
// and starts the program again.
void restartHandler(int button)
{
    (void)button;
    FILE * marker = fopen(OTA_RESTART_MARK, "wb");
    if (marker) {
        fwrite("x", 1, 1, marker);
        fclose(marker);
    }
    #ifdef POCKETBOOK_PRO_FW5
    ShowPureHourglassForce();
    #elif !defined(POCKETBOOK_PRO_PRO2)
    ShowHourglassForce();
    #endif
    PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
    exitApp();
}

struct DownloadProgress {
    int percent;
    lString8 text;
};

bool downloadProgress(double now, double total, void * data)
{
    DownloadProgress * p = (DownloadProgress *)data;
    if (total <= 0)
        return true;
    int percent = (int)(now * 100.0 / total);
    if (percent > 100)
        percent = 100;
    // every screen update takes time: one per 5 percent
    if (percent >= p->percent + 5 || (percent == 100 && p->percent != 100)) {
        p->percent = percent;
        UpdateProgressbar(p->text.c_str(), percent);
    }
    return true;
}

// The network for a request: true when it is connected. connect: Wi-Fi is turned on when it is
// off. A reader turns Wi-Fi off by itself after a while without traffic (reported from a
// PocketBook 632: the list of versions came, and the download a minute later could not resolve
// the host), so every request the user asked for starts with this, not only the first one.
bool networkUp(bool connect)
{
    if (QueryNetwork() & NET_CONNECTED)
        return true;
    if (!connect)
        return false;
    int rc = pbNetConnect();
    CRLog::info("OTA: NetConnect(NULL) = %d, QueryNetwork() = 0x%x", rc, QueryNetwork());
    return rc == NET_OK;
}

// The downloaded file is the package GitHub lists: the size, the SHA-256 and the contents.
// The contents: the installer, the program and the list of files. The other names of the package
// are not looked at: what an update puts on the reader is decided by the installer of the package
// (install.sh), which opens nothing of the settings but the file associations.
bool packageIsGood(const char * path, const ota::Release & release, lString32 & problem)
{
    std::string hex;
    long long size = 0;
    if (!ota::sha256File(path, hex, size) || size <= 0) {
        problem = tr("Couldn't download the update package!");
        return false;
    }
    if (release.size > 0 && size != release.size) {
        problem = tr("The downloaded file has a wrong size.");
        return false;
    }
    if (!release.sha256.empty() && hex != release.sha256) {
        problem = tr("The downloaded file is damaged: the checksum is wrong.");
        return false;
    }
    bool installer = false, program = false, files = false;
    {
        LVStreamRef stream = LVOpenFileStream(path, LVOM_READ);
        LVContainerRef archive;
        if (!stream.isNull())
            archive = LVOpenArchieve(stream);
        if (!archive.isNull()) {
            for (int i = 0; i < archive->GetObjectCount(); i++) {
                const LVContainerItemInfo * item = archive->GetObjectInfo(i);
                if (!item || !item->GetName())
                    continue;
                std::string name = utf8(lString32(item->GetName()));
                for (size_t k = 0; k < name.size(); k++)
                    if (name[k] == '\\')
                        name[k] = '/';
                if (name == "install.sh")
                    installer = true;
                else if (name == "system/share/cr3/bin/cr3-pb.app")
                    program = true;
                else if (name == "system/share/cr3/package.files")
                    files = true;
            }
        }
    }
    CRLog::info("OTA: package %s: %lld bytes, sha256 %s, installer %d, program %d, list of files %d",
                path, size, hex.c_str(), installer, program, files);
    if (!installer || !program || !files) {
        problem = tr("The downloaded file is not an update package.");
        return false;
    }
    return true;
}

} // namespace

// ------------------------------------------------------------ PbOta

PbOta::PbOta()
    : _cardBack(PB_OTA_CMD_MENU), _cardScreen(0), _cardInfoOnly(false), _listScreen(0), _mode(MODE_MENU), _fromSettings(false),
      _fetched(0), _verified(-1)
{
}

PbOta & PbOta::instance()
{
    static PbOta ota;
    return ota;
}

bool PbOta::supported()
{
    return isNetworkSupported();
}

void PbOta::setDefaults(CRPropRef p)
{
    p->setIntDef(PROP_POCKETBOOK_OTA_AUTO, 1);
    // a test build gets test versions, a release gets releases
    p->setIntDef(PROP_POCKETBOOK_OTA_TEST, installedVersion().test() ? 1 : 0);
}

void PbOta::started()
{
    // an update that failed: the installer or the launcher left the version here
    if (access(OTA_FAILED_MARKER, F_OK) != -1) {
        char version[128] = "";
        FILE * f = fopen(OTA_FAILED_MARKER, "rb");
        if (f) {
            size_t n = fread(version, 1, sizeof(version) - 1, f);
            version[n] = 0;
            fclose(f);
        }
        iv_unlink(OTA_FAILED_MARKER);
        lString32 failed(version);
        failed.trim();
        CRLog::error("OTA: the update to \"%s\" failed, the installed version is %s", version, CR_PB_VERSION);
        lString32 text;
        if (failed.empty()) {
            text = fill("The update was not installed: the package is damaged. Version $1 is working.",
                        lString32(CR_PB_VERSION));
        } else {
            text = fill("The update to version $1 was not installed: it did not start or the package is damaged. "
                        "Version $2 is working.", failed, lString32(CR_PB_VERSION));
            // the automatic check does not offer this version again
            props()->setString(PROP_POCKETBOOK_OTA_SKIP, failed);
            saveProps();
        }
        message(ICON_WARNING, text, 8000);
    }
    if (!supported() || !autoCheckOn())
        return;
    int last = props()->getIntDef(PROP_POCKETBOOK_OTA_LAST_CHECK, 0);
    if (ota::checkDue((long long)time(NULL), last, OTA_CHECK_PERIOD))
        SetWeakTimer("OtaAutoCheck", autoCheckTimer, 5000);
}

// Gets the list of the releases. connect: Wi-Fi is turned on when it is off (a check asked by
// the user); otherwise a disconnected network ends the check without a message.
bool PbOta::fetch(bool connect, lString32 & error)
{
    error.clear();
    std::string url = utf8(props()->getStringDef(PROP_POCKETBOOK_OTA_URL, ""));
    if (url.empty())
        url = OTA_RELEASES_URL;
    if (!startsWith(url, "file:") && !networkUp(connect)) {
        if (connect)
            error = tr("No network connection. Turn on Wi-Fi and try again.");
        return false;
    }

    WebRequest rq;
    // the automatic check runs while the user reads: short limits
    rq.connectTimeout = connect ? 15 : 5;
    rq.timeout = connect ? 40 : 15;
    rq.userAgent = "CoolReader-PocketBook/" CR_PB_VERSION;
    rq.accept = "application/vnd.github+json";
    rq.verifyPeer = true;
    rq.caInfo = web::caFile();
    std::string body;
    bool ok = web::request(url, rq, &body, NULL);
    _verified = ok ? 1 : _verified;
    // an old OpenSSL of a firmware may report a refused certificate as a failed handshake
    if (!ok && (web::isCertificateError(rq.curlCode) || rq.curlCode == CURLE_SSL_CONNECT_ERROR)) {
        CRLog::warn("OTA: the certificate of the server was not accepted (%s), the request goes without the check",
                    rq.error.c_str());
        rq.verifyPeer = false;
        ok = web::request(url, rq, &body, NULL);
        if (ok)
            _verified = 0;
    }
    if (!ok) {
        error = tr("Cannot get the list of versions. Check Wi-Fi.");
        error << "\n(" << lString32(rq.error.c_str()) << ")";
        return false;
    }
    std::vector<ota::Release> list;
    std::string reason;
    bool parsed = ota::parseReleases(body, CR_PB_SDK, list, reason);
    CRLog::info("OTA: %s: HTTP %ld, %d bytes, %d releases with a %s package%s%s", url.c_str(), rq.httpCode,
                (int)body.size(), (int)list.size(), CR_PB_SDK, parsed ? "" : ": ", reason.c_str());
    if (rq.httpCode == 403 || rq.httpCode == 429) {
        error = tr("GitHub limits the number of requests. Try again in an hour.");
        return false;
    }
    if (!parsed || (rq.httpCode != 0 && rq.httpCode != 200)) {
        char code[32];
        snprintf(code, sizeof(code), "HTTP %ld", rq.httpCode);
        error = fill("GitHub answered with an error: $1", lString32(parsed ? code : reason.c_str()));
        return false;
    }
    _all = list;
    _fetched = (long long)time(NULL);
    return true;
}

std::vector<ota::Release> PbOta::offered()
{
    return ota::offeredReleases(_all, ota::parseVersion(OTA_FIRST_VERSION), testVersionsOn());
}

void PbOta::open(CRGUIWindowManager * wm, bool fromSettings)
{
    _fromSettings = fromSettings;
    _error.clear();
    showMenu(wm);
}

void PbOta::showMenu(CRGUIWindowManager * wm)
{
    _mode = MODE_MENU;
    lvRect rc = wm->getScreen()->getRect();
    OpdsMenu * menu = new OpdsMenu(wm, tr("Program update"), rc, false);
    menu->setupRows(true);
    menu->setCloseCommand(PB_OTA_CMD_CLOSE);
    menu->add(PB_OTA_CMD_CLOSE, lString32(MARK_PREV) + (_fromSettings ? tr("Back to settings") : tr("Back to the book")));
    // a tap shows what is in the installed version
    menu->add(PB_OTA_CMD_INSTALLED, tr("Installed version"),
              lString32(CR_PB_VERSION) + lString32(DOT) + lString32(CR_PB_BUILD_DATE));

    lString32 checked;
    int last = props()->getIntDef(PROP_POCKETBOOK_OTA_LAST_CHECK, 0);
    if (last > 0) {
        checked = fill("Last check: $1", timeText(last));
        if (!_result.empty())
            checked << U", " << _result;
    } else {
        checked = tr("Not checked yet");
    }
    menu->add(PB_OTA_CMD_CHECK, tr("Check for updates"), checked);
    // why the last request failed: on the screen, not in a message window (see OTA_AFTER_WINDOW_MS)
    if (!_error.empty())
        menu->addItem(textRow(menu, _error));
    menu->add(PB_OTA_CMD_VERSIONS, tr("Choose a version"), tr("Any version from GitHub, the installed one too"));
    menu->add(PB_OTA_CMD_TOGGLE_AUTO, tr("Check automatically") + U": " + onOff(autoCheckOn()),
              tr("At every start of the program, when Wi-Fi is on"));
    menu->add(PB_OTA_CMD_TOGGLE_TEST, tr("Test versions") + U": " + onOff(testVersionsOn()),
              tr("Alpha and beta versions"));
    activate(wm, menu);
}

void PbOta::showList(CRGUIWindowManager * wm)
{
    _mode = MODE_LIST;
    lvRect rc = wm->getScreen()->getRect();
    OpdsMenu * menu = new OpdsMenu(wm, tr("Versions"), rc, false);
    int rows = menu->setupRows(true);
    int total = (int)_list.size();
    // a long list is turned with the arrows of the title
    int perScreen = rows;
    if (perScreen < 1)
        perScreen = 1;
    int screens = total > 0 ? (total + perScreen - 1) / perScreen : 1;
    if (_listScreen >= screens)
        _listScreen = screens - 1;
    if (_listScreen < 0)
        _listScreen = 0;
    menu->setPageCommands(PB_OTA_CMD_PREV, PB_OTA_CMD_NEXT);
    menu->setScreens(_listScreen, screens);
    menu->setCloseCommand(PB_OTA_CMD_MENU);

    menu->add(PB_OTA_CMD_MENU, lString32(MARK_PREV) + tr("Program update"));
    ota::Version installed = installedVersion();
    int first = _listScreen * perScreen;
    for (int i = first; i < total && i < first + perScreen; i++) {
        OpdsItem * item = new OpdsItem(menu, PB_OTA_CMD_VERSION + i, u16(_list[i].version), releaseInfo(_list[i]));
        item->mark = ota::compareVersions(_list[i].parsed, installed) == 0;
        menu->addItem(item);
    }
    if (total == 0)
        menu->addItem(textRow(menu, tr("There are no versions with the update over the network on GitHub yet.")));
    activate(wm, menu);
}

void PbOta::openCard(CRGUIWindowManager * wm, const ota::Release & release, int back, bool infoOnly)
{
    _card = release;
    _cardBack = back;
    _cardScreen = 0;
    _cardInfoOnly = infoOnly;
    showCard(wm);
}

// The row "Installed version": the card of that version with its release notes, the text the
// reader saw before the version was downloaded, without the button that installs. The notes are
// those of the release on GitHub, so the list of the versions is asked for as "Choose a version"
// asks for it. A version that is not published there (a build put on the reader by USB) has no
// notes: the screen says so.
void PbOta::showInstalled(CRGUIWindowManager * wm)
{
    // the list of this run is good for ten minutes
    if (_fetched == 0 || (long long)time(NULL) - _fetched > 600) {
        ShowHourglassForce();
        if (!fetch(true, _error)) {
            CRLog::warn("OTA: notes of the installed version: the list of versions failed: %s",
                        UnicodeToUtf8(_error).c_str());
            showMenu(wm);
            return;
        }
    }
    ota::Version installed = installedVersion();
    for (size_t i = 0; i < _all.size(); i++) {
        if (ota::compareVersions(_all[i].parsed, installed) == 0) {
            _error.clear();
            openCard(wm, _all[i], PB_OTA_CMD_MENU, true);
            return;
        }
    }
    CRLog::info("OTA: notes of the installed version: %s is not among the %d versions of GitHub", CR_PB_VERSION,
                (int)_all.size());
    _error = fill("Version $1 has no description on GitHub.", lString32(CR_PB_VERSION));
    showMenu(wm);
}

// A line of the release notes as the card draws it
struct CardLine {
    lString32 text;
    LVFontRef font;
    int indent;
    bool rule;   // a heading: a thin line above it
    bool newRow; // starts a row of the list
    CardLine() : indent(0), rule(false), newRow(false) {}
};

// The card of a version. The first screen: the button that installs the version, a line about
// the version, then its release notes. The next screens: the notes. (No row back: the arrow at
// the caption goes back, see OpdsMenu::add.) The notes are in the language of the interface (ota::notesForLanguage); a heading is
// bold with a line above it, a list item has a bullet and its next lines start under its text.
// A row of the list holds two lines.
void PbOta::showCard(CRGUIWindowManager * wm)
{
    _mode = MODE_CARD;
    lvRect rc = wm->getScreen()->getRect();
    OpdsMenu * menu = new OpdsMenu(wm, fill("Version $1", u16(_card.version)), rc, false);
    // a row holds two lines of the notes: separator lines of the skin would cut the text
    menu->setPlainRows();
    menu->setSwipeTurnsScreens(true);
    int rows = menu->setupRows(true);
    int width = menu->rowTextWidth();
    LVFontRef body = menu->bodyFont;
    LVFontRef head = menu->titleFont;
    lString32 bullet(U"\x2022 ");
    int bulletWidth = textWidth(body, bullet);

    std::vector<CardLine> lines;
    std::vector<ota::Note> notes = ota::parseNotes(ota::notesForLanguage(_card.notes, utf8(currentLang)));
    if (notes.empty()) {
        CardLine line;
        line.text = tr("The version has no description.");
        line.font = body;
        lines.push_back(line);
    }
    for (size_t i = 0; i < notes.size(); i++) {
        const ota::Note & note = notes[i];
        LVFontRef font = note.kind == ota::NOTE_HEADING ? head : body;
        int base = 0;
        std::vector<lString32> wrapped;
        if (note.kind == ota::NOTE_BULLET) {
            base = note.level * bulletWidth;
            wrapped = wrapText(font, bullet + u16(note.text), width - base, width - base - bulletWidth);
        } else {
            wrapped = wrapText(font, u16(note.text), width, width);
        }
        for (size_t k = 0; k < wrapped.size(); k++) {
            CardLine line;
            line.text = wrapped[k];
            line.font = font;
            line.indent = base + (note.kind == ota::NOTE_BULLET && k > 0 ? bulletWidth : 0);
            if (k == 0) {
                // a heading starts a row; so does a paragraph that follows a paragraph
                line.rule = note.kind == ota::NOTE_HEADING;
                line.newRow = note.kind == ota::NOTE_HEADING
                              || (note.kind == ota::NOTE_TEXT && i > 0 && notes[i - 1].kind == ota::NOTE_TEXT);
            }
            lines.push_back(line);
        }
    }
    // two lines in a row
    std::vector<std::pair<int, int> > packed;
    for (int i = 0; i < (int)lines.size(); i++) {
        if (!packed.empty() && packed.back().second < 0 && !lines[i].newRow)
            packed.back().second = i;
        else
            packed.push_back(std::make_pair(i, -1));
    }
    int total = (int)packed.size();
    // the first screen has two rows of its own: the button that installs and the line about the
    // version; the card of the installed version has the line only
    int firstRows = rows - (_cardInfoOnly ? 1 : 2);
    if (firstRows < 1)
        firstRows = 1;
    int otherRows = rows;
    if (otherRows < 1)
        otherRows = 1;
    int screens = total <= firstRows ? 1 : 1 + (total - firstRows + otherRows - 1) / otherRows;
    if (_cardScreen >= screens)
        _cardScreen = screens - 1;
    if (_cardScreen < 0)
        _cardScreen = 0;
    menu->setPageCommands(PB_OTA_CMD_PREV, PB_OTA_CMD_NEXT);
    menu->setScreens(_cardScreen, screens);
    menu->setCloseCommand(PB_OTA_CMD_BACK);

    lString32 back;
    if (_cardBack == PB_OTA_CMD_VERSIONS)
        back = tr("Versions");
    else if (_cardBack == PB_OTA_CMD_MENU)
        back = tr("Program update");
    else
        back = tr("Back to the book");
    menu->add(PB_OTA_CMD_BACK, lString32(MARK_PREV) + back);

    int order = ota::compareVersions(_card.parsed, installedVersion());
    if (_cardScreen == 0) {
        if (!_cardInfoOnly) {
            OpdsItem * install = new OpdsItem(menu, PB_OTA_CMD_INSTALL,
                tr(order > 0 ? "Download and install" : (order == 0 ? "Install again" : "Go back to this version")),
                lString32::empty_str);
            install->button = true;
            menu->addItem(install);
        }

        OpdsItem * about = new OpdsItem(menu, PB_OTA_CMD_LAST, lString32::empty_str, lString32::empty_str,
                                        OpdsItem::TEXT);
        about->first.label = releaseInfo(_card);
        if (order != 0)
            about->first.label << lString32(DOT)
                               << tr(order > 0 ? "newer than the installed one" : "older than the installed one");
        about->first.font = menu->subFont;
        about->first.center = true;
        menu->addItem(about);
    }

    int first = _cardScreen == 0 ? 0 : firstRows + (_cardScreen - 1) * otherRows;
    int count = _cardScreen == 0 ? firstRows : otherRows;
    for (int i = first; i < total && i < first + count; i++) {
        OpdsItem * item = new OpdsItem(menu, PB_OTA_CMD_LAST, lString32::empty_str, lString32::empty_str,
                                       OpdsItem::TEXT);
        for (int k = 0; k < 2; k++) {
            int index = k == 0 ? packed[i].first : packed[i].second;
            if (index < 0)
                continue;
            const CardLine & l = lines[index];
            InfoLine & info = k == 0 ? item->first : item->second;
            info.text = l.text;
            info.font = l.font;
            info.indent = l.indent;
            info.rule = l.rule;
        }
        menu->addItem(item);
    }
    activate(wm, menu);
}

void PbOta::showCurrent(CRGUIWindowManager * wm)
{
    if (_mode == MODE_LIST)
        showList(wm);
    else if (_mode == MODE_CARD)
        showCard(wm);
    else
        showMenu(wm);
}

// A check asked by the user: a newer version opens its card, otherwise a message
void PbOta::check(CRGUIWindowManager * wm)
{
    // No HideHourglass(): like the rest of CR3, the next screen update removes the hourglass.
    ShowHourglassForce();
    _result.clear();
    if (!fetch(true, _error)) {
        CRLog::warn("OTA: check failed: %s", UnicodeToUtf8(_error).c_str());
        showMenu(wm);
        return;
    }
    props()->setInt(PROP_POCKETBOOK_OTA_LAST_CHECK, (int)time(NULL));
    saveProps();
    std::vector<ota::Release> list = offered();
    CRLog::info("OTA: check: %d versions offered, the newest %s, installed %s", (int)list.size(),
                list.empty() ? "-" : list[0].version.c_str(), CR_PB_VERSION);
    if (!list.empty() && ota::compareVersions(list[0].parsed, installedVersion()) > 0) {
        _result = fill("version $1 is available", u16(list[0].version));
        openCard(wm, list[0], PB_OTA_CMD_MENU);
        return;
    }
    // the result is the second line of the row "Check for updates"
    _result = tr("no newer version");
    showMenu(wm);
}

void PbOta::autoCheck()
{
    if (!main_win || !autoCheckOn())
        return;
    lString32 error;
    if (!fetch(false, error)) {
        if (error.empty()) {
            // Wi-Fi is off: the check waits for a start with the network
            CRLog::info("OTA: automatic check: the network is not connected");
            return;
        }
        CRLog::warn("OTA: automatic check failed: %s", UnicodeToUtf8(error).c_str());
    }
    // the time of the check is shown on the screen "Program update"
    props()->setInt(PROP_POCKETBOOK_OTA_LAST_CHECK, (int)time(NULL));
    saveProps();
    if (!error.empty())
        return;
    std::vector<ota::Release> list = offered();
    if (list.empty() || ota::compareVersions(list[0].parsed, installedVersion()) <= 0) {
        _result = tr("no newer version");
        CRLog::info("OTA: automatic check: no newer version among %d", (int)list.size());
        return;
    }
    _result = fill("version $1 is available", u16(list[0].version));
    if (u16(list[0].version) == props()->getStringDef(PROP_POCKETBOOK_OTA_SKIP, "")) {
        CRLog::info("OTA: automatic check: version %s was declined before", list[0].version.c_str());
        return;
    }
    _card = list[0];
    lString32 text = fill("Version $1 is available.", u16(_card.version));
    std::string notes = ota::shortNotes(ota::notesForLanguage(_card.notes, utf8(currentLang)), 5, 400);
    if (!notes.empty())
        text << "\n\n" << u16(notes);
    _question = UnicodeToUtf8(text);
    // (asked on the program's own, at its start: closed by the program when it is drawn over;
    // a question not answered is asked again at the next automatic check)
    pbQuestion(ICON_INFORMATION, WINDOW_TITLE, _question.c_str(), _("Details"), _("Not now"), offerHandler);
}

void PbOta::offerAnswered(int button)
{
    CRLog::info("OTA: version %s is offered: answer %d", _card.version.c_str(), button);
    if (button == 1) {
        SetWeakTimer("OtaOpenCard", openCardTimer, OTA_AFTER_WINDOW_MS);
        return;
    }
    // not now: the automatic check does not offer this version again; the screen
    // "Program update" still shows it
    props()->setString(PROP_POCKETBOOK_OTA_SKIP, u16(_card.version));
    saveProps();
}

void PbOta::askInstall()
{
    lString32 text = fill("Download and install version $1 ($2)?", u16(_card.version), sizeText(_card.size));
    if (ota::compareVersions(_card.parsed, installedVersion()) < 0)
        text << "\n" << fill("It is older than the installed version $1.", lString32(CR_PB_VERSION));
    text << "\n" << tr("CoolReader will restart itself.");
    _question = UnicodeToUtf8(text);
    CRLog::info("OTA: asks to download and install %s", _card.version.c_str());
    Dialog(ICON_QUESTION, WINDOW_TITLE, _question.c_str(), _("Yes"), _("No"), installHandler);
    CRLog::info("OTA: the question is on the screen");
}

// "Details" of the window of the automatic check. The user goes to the program update as if
// the screen were opened from the settings and the version chosen there: the command is posted
// and handled as a tap in a list is, the card leaves to the screen "Program update", and that
// screen leaves to the book. Seen on InkPad 4 with 0.0.7-opds-alpha3: the card opened straight
// from the timer over the book left to the book when its install button was tapped, while the
// same button of a card opened from the list worked.
void PbOta::openOfferedCard()
{
    CRGUIWindowManager * wm = main_win->getWindowManager();
    CRLog::info("OTA: details of the offered version %s", _card.version.c_str());
    wm->postCommand(PB_OTA_CMD_OFFERED, 0);
    wm->processPostedEvents();
}

void PbOta::installAnswered(int button)
{
    CRLog::info("OTA: download and install %s: answer %d", _card.version.c_str(), button);
    if (button == 1)
        SetWeakTimer("OtaDownload", downloadTimer, OTA_AFTER_WINDOW_MS);
}

void PbOta::download()
{
    if (GetBatteryPower() < 20 && IsCharging() <= 0) {
        message(ICON_WARNING, tr("The battery is low. Charge the device to 20% and try again."), 5000);
        return;
    }
    // the package, its unpacked copy and a reserve
    long long needed = _card.size * 5 + 1024 * 1024;
    struct statvfs fs;
    if (_card.size > 0 && statvfs(OTA_DOWNLOAD_DIR, &fs) == 0) {
        long long available = (long long)fs.f_bavail * (long long)fs.f_frsize;
        CRLog::info("OTA: free space %lld bytes, needed %lld", available, needed);
        if (available < needed) {
            message(ICON_WARNING, fill("Not enough free space for the update: $1 is needed.", sizeText(needed)), 5000);
            return;
        }
    }
    // a package left by a failed attempt must not be installed at the restart
    iv_unlink(OTA_PACKAGE_PART);
    iv_unlink(OTA_PACKAGE_FILE);

    // Wi-Fi may be off again since the list of versions was got
    if (!startsWith(_card.url, "file:") && !networkUp(true)) {
        message(ICON_WARNING, tr("No network connection. Turn on Wi-Fi and try again."), 5000);
        return;
    }

    DownloadProgress progress;
    progress.percent = 0;
    progress.text = UnicodeToUtf8(tr("Downloading..."));
    OpenProgressbar(ICON_INFORMATION, _("Program update"), progress.text.c_str(), 0, NULL);

    WebRequest rq;
    rq.connectTimeout = 20;
    rq.timeout = 0; // no limit for the whole file; a stalled transfer fails
    rq.userAgent = "CoolReader-PocketBook/" CR_PB_VERSION;
    rq.progress = downloadProgress;
    rq.progressData = &progress;
    // the same way the list was got
    rq.verifyPeer = _verified == 1;
    if (rq.verifyPeer)
        rq.caInfo = web::caFile();
    CRLog::info("OTA: download %s", _card.url.c_str());
    bool ok = web::request(_card.url, rq, NULL, OTA_PACKAGE_PART);
    if (!ok && (rq.curlCode == CURLE_COULDNT_RESOLVE_HOST || rq.curlCode == CURLE_COULDNT_CONNECT)) {
        // the firmware told of a connection that was gone: connected anew, the download is
        // tried once more
        int rc = pbNetConnect();
        CRLog::info("OTA: the download failed (%s); NetConnect(NULL) = %d, QueryNetwork() = 0x%x", rq.error.c_str(),
                    rc, QueryNetwork());
        if (rc == NET_OK) {
            iv_unlink(OTA_PACKAGE_PART);
            ok = web::request(_card.url, rq, NULL, OTA_PACKAGE_PART);
        }
    }
    CloseProgressbar();

    lString32 problem;
    if (!ok || (rq.httpCode != 0 && rq.httpCode != 200)) {
        problem = tr("Couldn't download the update package!");
        if (!rq.error.empty())
            problem << "\n(" << lString32(rq.error.c_str()) << ")";
        else if (rq.httpCode != 0)
            problem << "\n(HTTP " << fmt::decimal((int)rq.httpCode) << ")";
    } else {
        packageIsGood(OTA_PACKAGE_PART, _card, problem);
    }
    if (problem.empty() && rename(OTA_PACKAGE_PART, OTA_PACKAGE_FILE) != 0)
        problem = tr("Couldn't download the update package!");
    if (!problem.empty()) {
        iv_unlink(OTA_PACKAGE_PART);
        message(ICON_ERROR, problem, 6000);
        return;
    }
    Dialog(ICON_INFORMATION, WINDOW_TITLE, _("Download successful!\nCoolReader will restart itself."),
           _("OK"), NULL, restartHandler);
}

bool PbOta::onCommand(CRGUIWindowManager * wm, int command, int params)
{
    (void)params;
    CRLog::info("OTA: command %d", command);
    switch (command) {
    case PB_OTA_CMD_MENU:
        showMenu(wm);
        return true;
    case PB_OTA_CMD_CLOSE:
        // opened from the settings menu: back to that menu, at its item
        if (_fromSettings) {
            _fromSettings = false;
            wm->postCommand(MCMD_SETTINGS, PB_CMD_OTA_UPDATE);
        }
        return true;
    case PB_OTA_CMD_CHECK:
        check(wm);
        return true;
    case PB_OTA_CMD_INSTALLED:
        showInstalled(wm);
        return true;
    case PB_OTA_CMD_VERSIONS: {
        // the list of this run is good for ten minutes
        if (_fetched == 0 || (long long)time(NULL) - _fetched > 600) {
            ShowHourglassForce();
            if (!fetch(true, _error)) {
                CRLog::warn("OTA: list of versions failed: %s", UnicodeToUtf8(_error).c_str());
                showMenu(wm);
                return true;
            }
        }
        // an empty list says so itself
        _list = offered();
        _listScreen = 0;
        showList(wm);
        return true;
    }
    case PB_OTA_CMD_TOGGLE_AUTO:
        props()->setInt(PROP_POCKETBOOK_OTA_AUTO, autoCheckOn() ? 0 : 1);
        saveProps();
        showMenu(wm);
        return true;
    case PB_OTA_CMD_TOGGLE_TEST:
        props()->setInt(PROP_POCKETBOOK_OTA_TEST, testVersionsOn() ? 0 : 1);
        saveProps();
        _result.clear(); // the result of the check was for the other set of versions
        showMenu(wm);
        return true;
    case PB_OTA_CMD_PREV:
    case PB_OTA_CMD_NEXT: {
        int delta = command == PB_OTA_CMD_NEXT ? 1 : -1;
        if (_mode == MODE_LIST)
            _listScreen += delta;
        else if (_mode == MODE_CARD)
            _cardScreen += delta;
        showCurrent(wm);
        return true;
    }
    case PB_OTA_CMD_BACK:
        if (_cardBack == PB_OTA_CMD_VERSIONS)
            showList(wm);
        else
            showMenu(wm);
        return true;
    case PB_OTA_CMD_INSTALL:
        // the card stays on the screen under the question
        showCard(wm);
        askInstall();
        return true;
    case PB_OTA_CMD_OFFERED:
        _fromSettings = false;
        _error.clear();
        _result = fill("version $1 is available", u16(_card.version));
        openCard(wm, _card, PB_OTA_CMD_MENU);
        return true;
    case PB_OTA_CMD_LAST:
        // a tap on a text row: the list closed, show the same screen again
        showCurrent(wm);
        return true;
    default:
        break;
    }
    if (command >= PB_OTA_CMD_VERSION && command < PB_OTA_CMD_LAST) {
        int index = command - PB_OTA_CMD_VERSION;
        if (index < (int)_list.size())
            openCard(wm, _list[index], PB_OTA_CMD_VERSIONS);
        else
            showCurrent(wm);
        return true;
    }
    return false;
}
