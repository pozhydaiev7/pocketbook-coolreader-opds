/*
 * The position sync of KOReader in CoolReader on PocketBook: the screen of its settings and what a
 * book does with it. The protocol and the decisions are in pb_kosync_api.h.
 *
 * Asked by readers: the place of a book read on a computer and on the reader. KOReader does it with
 * its sync server; a server of that protocol keeps the place of a book (its file, by the MD5 of its
 * parts) for a user, and KOReader on a computer, a phone or another reader, Readest, and this program
 * on another PocketBook send it and take it. The address of the server is a setting: the one of
 * KOReader, or one of the user (Calibre-Web-Automated, Kavita, korrosync...).
 *
 * The sync is turned on by a switch of the settings menu ("Reading position sync"), off by default; while
 * it is off nothing goes to the network. While it is on, the program does what the automatic sync
 * of KOReader does (plugins/kosync.koplugin/main.lua):
 *  - a book opened, and the program back on the screen: the places not sent yet are sent, then
 *    the place of the book is asked for. Wi-Fi is turned on for it when the firmware can do it
 *    without a dialog (NetConnectSilent); otherwise only while Wi-Fi is on;
 *  - a book closed (another one opened, the program closed or sent to the background): its place
 *    is sent while Wi-Fi is on, otherwise kept and sent at the next chance (the queue of KOReader);
 *  - Wi-Fi turned on (EVT_NET_CONNECTED, firmware 6): the places kept are sent, the place is asked;
 *  - a place of another device that is newer than the last page turned here is asked about, an
 *    older one is left (both are settings: ask, go there, ignore);
 *  - an automatic request waits 25 s after the last one of its kind; a request waits 2 s for the
 *    connection and 5 s for the answer, the login 5 and 10.
 * The actions "Send the place" and "Get the place" (the screen, a tap zone, the main menu) turn the
 * network on through the firmware and go to the place whatever it is.
 */
#ifndef PB_KOSYNC_H_INCLUDED
#define PB_KOSYNC_H_INCLUDED

#include <crgui.h>
#include <inkview.h>
#include <string>
#include <vector>
#include <time.h>
#include "pb_kosync_api.h"

#define PROP_KOSYNC_SERVER "cr3.pocketbook.kosync.server"
#define PROP_KOSYNC_USER "cr3.pocketbook.kosync.user"
// the MD5 of the password, as the protocol sends it; the password itself is not kept
#define PROP_KOSYNC_KEY "cr3.pocketbook.kosync.key"
// 1: the sync is on (the switch of the settings menu, off by default); 0: nothing goes to the network
#define PROP_KOSYNC_ENABLED "cr3.pocketbook.kosync.enabled"
// the id of this device for the server, made once
#define PROP_KOSYNC_DEVICE_ID "cr3.pocketbook.kosync.device.id"
// what a newer and an older place of another device does: kosync::Strategy (1 ask, 2 go, 3 ignore)
#define PROP_KOSYNC_FORWARD "cr3.pocketbook.kosync.forward"
#define PROP_KOSYNC_BACKWARD "cr3.pocketbook.kosync.backward"
// the places not sent yet: cr3.pocketbook.kosync.queue.1..KOSYNC_QUEUE_MAX, kosync::queueLine, the
// oldest first, one per book; an empty value is a free slot
#define PROP_KOSYNC_QUEUE "cr3.pocketbook.kosync.queue."
#define KOSYNC_QUEUE_MAX 10

// The event of Wi-Fi connected (EVT_NET_CONNECTED of the SDK of firmware 6; the older SDKs have no
// name for it and their firmware does not send it).
#ifdef EVT_NET_CONNECTED
#define PB_EVT_NET_CONNECTED EVT_NET_CONNECTED
#else
#define PB_EVT_NET_CONNECTED 256
#endif

// Command ids of the screen: after the debugging screen (65100..65109).
enum PbKosyncCommands {
    PB_KOSYNC_CMD_FIRST = 65200,
    PB_KOSYNC_CMD_MENU = PB_KOSYNC_CMD_FIRST, // the screen again
    PB_KOSYNC_CMD_CLOSE,
    PB_KOSYNC_CMD_SERVER,
    PB_KOSYNC_CMD_SERVER_APPLY,
    PB_KOSYNC_CMD_USER,
    PB_KOSYNC_CMD_USER_APPLY,
    PB_KOSYNC_CMD_PASSWORD,
    PB_KOSYNC_CMD_PASSWORD_APPLY,
    PB_KOSYNC_CMD_CHECK,      // the login is checked
    PB_KOSYNC_CMD_REGISTER,   // a new user on the server
    PB_KOSYNC_CMD_PUSH,       // the place of the open book is sent
    PB_KOSYNC_CMD_PULL,       // the place of the open book is taken
    PB_KOSYNC_CMD_LOGOUT,     // the login is forgotten
    PB_KOSYNC_CMD_GO_OFFERED, // the answer "Yes" to the question of a place of another device
    PB_KOSYNC_CMD_FORWARD,    // the next choice for a newer place
    PB_KOSYNC_CMD_BACKWARD,   // the next choice for an older place
    PB_KOSYNC_CMD_QUEUE,      // the places not sent yet are sent now
    PB_KOSYNC_CMD_LAST = 65219
};

class PbKosync
{
public:
    static PbKosync & instance();
    static bool isKosyncCommand(int command)
    {
        return command >= PB_KOSYNC_CMD_FIRST && command <= PB_KOSYNC_CMD_LAST;
    }
    /// The values a reader that never set them has (the server of KOReader, the sync off, the
    /// choices of KOReader for the places of other devices).
    static void setDefaults(CRPropRef props);
    /// The switch of the settings menu is on.
    static bool enabled();

    /// Opens the screen; fromSettings: "Back" opens the settings menu again.
    void open(CRGUIWindowManager * wm, bool fromSettings);
    /// Handles PB_KOSYNC_CMD_FIRST..PB_KOSYNC_CMD_LAST; true when consumed.
    bool onCommand(CRGUIWindowManager * wm, int command, int params);

    /// A book was opened: a moment later (the book is on the screen first) the places kept are
    /// sent and its place is asked for (KOReader: onReaderReady).
    void bookOpened();
    /// The book is to be closed, or the program goes to the background: its place is sent while
    /// Wi-Fi is on, otherwise kept (KOReader: onCloseDocument).
    void bookClosing();
    /// The program is on the screen again: as a book opened (KOReader: onResume).
    void foregrounded();
    /// Wi-Fi was turned on: the places kept are sent, the place is asked (KOReader: onNetworkConnected).
    void networkConnected();
    /// The actions: the place sent, or taken (the network is turned on for them); getNow is true
    /// when the book went to the place it got.
    void sendNow();
    bool getNow();
    /// The timers: the automatic request, with Wi-Fi turned on for it or not.
    void autoPull(bool connect);
    /// A page went to the screen (the book window, the pages of a PDF or a DjVu): a page turned
    /// when it is another one than the page before (KOReader: onPageUpdate). The time of the last
    /// page turned tells a place of another device sent after it (newer) from one sent before.
    void pageShown(int page);
    /// While held, a book opened or closed sends and asks nothing: the same file goes from one view
    /// to the other (a PDF read as a book and as its pages).
    void hold(bool on) { _held = on; }
#ifdef CR3_PAGEDOC
    /// The pages of a PDF or a DjVu shown (the picture mode, pb_pageview.h), and closed: as a book
    /// opened and closed. The place of such a file is its page, in either view: KOReader reads
    /// a PDF as pages and sends the page from 1; a book made of a PDF sends the page of the PDF
    /// its screen begins on and goes to the page it gets. page: the one the file is opened at.
    void pagesOpened(int page);
    void pagesClosing();
#endif

    /// Keyboard callbacks write here before posting the *_APPLY command.
    char keyBuffer[512];

private:
    PbKosync()
        : _fromSettings(false), _held(false), _pagesOverBook(false), _lookupLogged(false), _pushAt(0), _pullAt(0),
          _lastTurn(0), _shownPage(-1), _openedAt(0), _offeredValid(false)
    {
        keyBuffer[0] = 0;
    }
    bool _fromSettings;
    bool _held;
    // the pages were shown over the book made of the same file after the book was opened: the
    // place of the book window is older than the one the pages sent when they went
    bool _pagesOverBook;
    bool _lookupLogged;
    // the times (time()) of the last automatic push and pull of the open book: KOReader waits 25 s
    // between two (its plugin lives as long as a book, so a book opened starts them anew)
    long _pushAt, _pullAt;
    // the last page turned here (time()), 0 when none since the book was opened: a place of
    // another device sent after it is newer
    long _lastTurn;
    // the page on the screen (pageShown), -1 when none since the book or the view was opened
    int _shownPage;
    // the time the book was opened: the time of its place while no page has been turned
    long _openedAt;
    // the book went to a place of the server: a page turned now, as in KOReader
    void movedTo() { _lastTurn = (long)time(NULL); }
    // what was sent last
    std::string _sentDocument, _sentProgress;
    // the place offered by the question and the book it is of
    kosync::Progress _offered;
    std::string _offeredDocument;
    bool _offeredValid;

    /// The account with the limits of KOReader: progress true for a place, false for the login.
    kosync::Account accountFor(bool progress);
    /// The place of the open book sent (the queue when it is not); false when it was not sent.
    bool pushPlace(const std::string & document, kosync::Progress place, bool interactive);
    /// The place on the server for the open book, and what is done with it; true when the book
    /// went to it.
    bool pullPlace(const std::string & document, const kosync::Progress & here, bool interactive);
    /// The queue: a place kept (the one of the same book replaced) with the time it was read here,
    /// the places sent; the number of places that left the queue, sent or found newer on the server.
    void keepInQueue(const std::string & document, const kosync::Progress & place);
    int drainQueue();
    int queueCount();

    void showMenu(CRGUIWindowManager * wm);
    kosync::Account account();
    bool loggedIn();
    std::string deviceModel();
    std::string deviceId();
    void editText(const char * title, const lString32 & value, void (*handler)(char *));
    void message(const lString32 & text);
};

#endif // PB_KOSYNC_H_INCLUDED
