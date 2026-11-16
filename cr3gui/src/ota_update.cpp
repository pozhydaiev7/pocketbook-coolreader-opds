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

using namespace opdsui;

namespace {

// Root certificates of the firmware. With one of them (or with the default of libcurl) the
// certificate of the server is checked; when that check is refused, the request is made again
// without it, as the OPDS catalogs are read; the log says which way a request went.
const char * const CA_FILES[] = {
    "/ebrmain/share/ssl/certs/ca-certificates.crt",
    "/etc/ssl/certs/ca-certificates.crt",
    "/etc/ssl/cert.pem",
    NULL
};

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

// "2026-12-01  ·  7.0 MB  ·  test  ·  installed"
lString32 releaseInfo(const ota::Release & r)
{
    lString32 s = u16(r.date);
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

// The downloaded file is the package GitHub lists: the size, the SHA-256 and the contents.
// The contents: the installer, the program and the list of files; nothing for system/config and
// system/state (settings, history, statistics), which an update never touches.
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
    bool installer = false, program = false, files = false, forbidden = false;
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
                else if (startsWith(name, "system/config") || startsWith(name, "system/state"))
                    forbidden = true;
            }
        }
    }
    CRLog::info("OTA: package %s: %lld bytes, sha256 %s, installer %d, program %d, list of files %d, settings inside %d",
                path, size, hex.c_str(), installer, program, files, forbidden);
    if (!installer || !program || !files || forbidden) {
        problem = tr("The downloaded file is not an update package.");
        return false;
    }
    return true;
}

} // namespace

// ------------------------------------------------------------ PbOta

PbOta::PbOta()
    : _cardBack(PB_OTA_CMD_MENU), _cardScreen(0), _listScreen(0), _mode(MODE_MENU), _fromSettings(false),
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
    if (!startsWith(url, "file:")) {
        int state = QueryNetwork();
        if (!(state & NET_CONNECTED)) {
            if (!connect)
                return false;
            int rc = NetConnect(NULL);
            CRLog::info("OTA: NetConnect(NULL) = %d, QueryNetwork() = 0x%x", rc, QueryNetwork());
            if (rc != NET_OK) {
                error = tr("No network connection. Turn on Wi-Fi and try again.");
                return false;
            }
        }
    }

    WebRequest rq;
    // the automatic check runs while the user reads: short limits
    rq.connectTimeout = connect ? 15 : 5;
    rq.timeout = connect ? 40 : 15;
    rq.userAgent = "CoolReader-PocketBook/" CR_PB_VERSION;
    rq.accept = "application/vnd.github+json";
    rq.verifyPeer = true;
    for (int i = 0; CA_FILES[i]; i++) {
        if (access(CA_FILES[i], R_OK) == 0) {
            rq.caInfo = CA_FILES[i];
            break;
        }
    }
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
    menu->add(PB_OTA_CMD_MENU, tr("Installed version"),
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
              tr("At the start, once a day, when Wi-Fi is on"));
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
    // the first row goes back; a long list is turned with the arrows of the title
    int perScreen = rows - 1;
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

void PbOta::openCard(CRGUIWindowManager * wm, const ota::Release & release, int back)
{
    _card = release;
    _cardBack = back;
    _cardScreen = 0;
    showCard(wm);
}

// The card of a version: the row that installs it and its release notes, two lines in a row
void PbOta::showCard(CRGUIWindowManager * wm)
{
    _mode = MODE_CARD;
    lvRect rc = wm->getScreen()->getRect();
    OpdsMenu * menu = new OpdsMenu(wm, fill("Version $1", u16(_card.version)), rc, false);
    // a row holds two lines of the notes: separator lines of the skin would cut the text
    menu->setPlainRows();
    int rows = menu->setupRows(true);
    int width = menu->rowTextWidth();

    std::vector<lString32> lines;
    std::vector<std::string> notes = ota::noteLines(_card.notes);
    if (notes.empty()) {
        lines.push_back(tr("The version has no description."));
    } else {
        for (size_t i = 0; i < notes.size(); i++) {
            if (notes[i].empty()) {
                lines.push_back(lString32::empty_str);
                continue;
            }
            std::vector<lString32> wrapped = wrapText(menu->bodyFont, u16(notes[i]), width, width);
            lines.insert(lines.end(), wrapped.begin(), wrapped.end());
        }
    }
    int total = ((int)lines.size() + 1) / 2;
    // the first two rows go back and install
    int perScreen = rows - 2;
    if (perScreen < 1)
        perScreen = 1;
    int screens = total > 0 ? (total + perScreen - 1) / perScreen : 1;
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
    menu->add(PB_OTA_CMD_INSTALL,
              tr(order > 0 ? "Download and install" : (order == 0 ? "Install again" : "Go back to this version")),
              releaseInfo(_card));

    int first = _cardScreen * perScreen;
    for (int i = first; i < total && i < first + perScreen; i++) {
        OpdsItem * item = new OpdsItem(menu, PB_OTA_CMD_LAST, lString32::empty_str, lString32::empty_str,
                                       OpdsItem::TEXT);
        item->first.text = lines[i * 2];
        item->first.font = menu->bodyFont;
        if (i * 2 + 1 < (int)lines.size()) {
            item->second.text = lines[i * 2 + 1];
            item->second.font = menu->bodyFont;
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
    // one attempt in the period, successful or not: a dead network must not hold every start
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
    std::string notes = ota::shortNotes(_card.notes, 5, 400);
    if (!notes.empty())
        text << "\n\n" << u16(notes);
    _question = UnicodeToUtf8(text);
    Dialog(ICON_INFORMATION, WINDOW_TITLE, _question.c_str(), _("Details"), _("Not now"), offerHandler);
}

void PbOta::offerAnswered(int button)
{
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
    Dialog(ICON_QUESTION, WINDOW_TITLE, _question.c_str(), _("Yes"), _("No"), installHandler);
}

// "Details" of the window of the automatic check: the card of the version over the book
void PbOta::openOfferedCard()
{
    _fromSettings = false;
    openCard(main_win->getWindowManager(), _card, PB_OTA_CMD_CLOSE);
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
    for (int i = 0; rq.verifyPeer && CA_FILES[i]; i++) {
        if (access(CA_FILES[i], R_OK) == 0) {
            rq.caInfo = CA_FILES[i];
            break;
        }
    }
    CRLog::info("OTA: download %s", _card.url.c_str());
    bool ok = web::request(_card.url, rq, NULL, OTA_PACKAGE_PART);
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
        else if (_cardBack == PB_OTA_CMD_MENU)
            showMenu(wm);
        // otherwise the card was opened over the book by the automatic check
        return true;
    case PB_OTA_CMD_INSTALL:
        // the card stays on the screen under the question
        showCard(wm);
        askInstall();
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
