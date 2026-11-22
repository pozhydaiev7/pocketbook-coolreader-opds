/*
 * The position sync of KOReader ("progress sync"): its protocol, without the screens of the
 * program, so the PC builds it alone (web.cpp, libcurl) and tests it against a server.
 *
 * The protocol is that of the plugin kosync of KOReader (plugins/kosync.koplugin/api.json) and of
 * its server (koreader-sync-server, app/controllers/1/syncs_controller.lua); other servers repeat it
 * (Calibre-Web-Automated, Kavita, korrosync, kosync-dotnet...):
 *   POST /users/create      {"username", "password"}            201 made, 402 the name is taken
 *   GET  /users/auth        headers x-auth-user, x-auth-key     200 or 401
 *   PUT  /syncs/progress    {"document", "progress", "percentage", "device", "device_id"}  200
 *   GET  /syncs/progress/<document>                              200 {...} ({} when none)
 * The key of a user is the MD5 of the password, in hex; every request asks for
 * "application/vnd.koreader.v1+json". A document is the MD5 of parts of its file (partialMd5), the
 * same on every device that has the same file. The place is a XPointer of the engine for a book (KOReader
 * reads books with the same engine, crengine, and its XPointers are of the same kind: normalized,
 * DOM version 20200824) or the page from 1 for a PDF or a DjVu read as pages; the percentage is from
 * 0 to 1, cut to four digits.
 */
#ifndef PB_KOSYNC_API_H_INCLUDED
#define PB_KOSYNC_API_H_INCLUDED

#include <string>
#include <vector>

namespace kosync {

/// The server KOReader uses by default.
extern const char * const DEFAULT_SERVER;

/// MD5 of the bytes, 32 hex digits in lower case.
std::string md5Hex(const std::string & data);

/// The document id of KOReader (util.partialMD5): the MD5 of 1024 bytes read at the offsets 0 and
/// 1024 * 4^i (i = 0..10) while the file has them, the last piece as long as the file goes on.
/// Empty when the file cannot be read.
std::string partialMd5(const char * path);

struct Account {
    std::string server;     // "https://sync.koreader.rocks", without the last slash or with it
    std::string user;
    std::string key;        // md5Hex of the password
    // seconds a request waits for the connection and for the whole answer: a server that is down
    // must not hold the reader. KOReader gives 2 and 5 to a place sent or asked for
    // (PROGRESS_TIMEOUTS), 5 and 10 to the login (AUTH_TIMEOUTS), the default here.
    long connectTimeout;
    long timeout;
    // The certificate of an https server is checked against these root certificates (NULL: the
    // default of libcurl); a certificate they do not know is accepted after all (the request is
    // made again without the check, as the update of the program does). A redirect is followed
    // to the same host only: the key goes to this server.
    const char * caInfo;
    Account() : connectTimeout(5), timeout(10), caInfo(NULL) { }
};

/// True when the libcurl looks up names in a thread of its own (CURL_VERSION_ASYNCHDNS): only then
/// the limits of a request hold for the lookup of the name of the server too (the requests are made
/// without signals).
bool asyncNameLookup();

struct Progress {
    bool found;             // the server has a place for the document
    std::string progress;   // XPointer, or the page from 1
    double percentage;      // 0..1
    std::string device;     // the name of the device the place came from
    std::string deviceId;
    long timestamp;         // seconds, set by the server; 0 when it gives none
    Progress() : found(false), percentage(0), timestamp(0) { }
};

/// The result of a request: the HTTP answer (0 when no answer came: no network, no server) and
/// the text of the error of the connection.
struct Result {
    long http;
    std::string error;
    int curl;               // the libcurl code (CURLE_OK 0): a refused certificate is told apart
    Result() : http(0), curl(0) { }
    bool ok() const { return http >= 200 && http < 300; }
};

/// A new user on the server. 201: made; 402: the name is taken; 403 or other: refused.
Result registerUser(const Account & account);
/// The login is checked. 200: right; 401: wrong.
Result authorize(const Account & account);
/// The place of the document is sent.
Result push(const Account & account, const std::string & document, const Progress & progress);
/// The place of the document is asked; out.found tells whether the server has one.
Result pull(const Account & account, const std::string & document, Progress & out);

/// A percentage as KOReader sends it: cut to four digits after the point.
double roundPercent(double percent);

/// True when both addresses name the same host (any scheme and port): a redirect between them is
/// followed with the login.
bool sameHost(const std::string & a, const std::string & b);

/// A string of JSON: the quotes, the backslash and the control characters escaped.
std::string jsonString(const std::string & s);

// What the plugin of KOReader decides (plugins/kosync.koplugin/main.lua), apart from the screens,
// so the PC checks it.

/// Seconds an automatic request waits after the last one of its kind (API_CALL_DEBOUNCE_DELAY).
const long DEBOUNCE_SECONDS = 25;
/// An automatic request made at `now` is skipped: the last one of its kind was made at `last`, less
/// than DEBOUNCE_SECONDS ago (a clock set back does not hold the requests).
bool debounced(long now, long last);

/// What a place of another device does (SYNC_STRATEGY of KOReader, the same numbers).
enum Strategy {
    STRATEGY_PROMPT = 1,    // a question
    STRATEGY_SILENT = 2,    // the book goes there with a message
    STRATEGY_DISABLE = 3    // nothing
};
/// KOReader by default: a newer place is asked about, an older one is left.
const int DEFAULT_FORWARD = STRATEGY_PROMPT;
const int DEFAULT_BACKWARD = STRATEGY_DISABLE;

enum Decision {
    DECIDE_NONE,        // the server has no place of the document
    DECIDE_FROM_HERE,   // the place on the server is the one this device sent
    DECIDE_SAME,        // the book is at that place already
    DECIDE_IGNORE,      // the strategy is DISABLE
    DECIDE_ASK,         // the question "go there?"
    DECIDE_GO           // the book goes there (asked by the reader, or SILENT)
};

/// The answer of KOReader to the place on the server (`there`) for the book at `here`, read on
/// this device (`model`, `deviceId`) with the last page turned at `lastTurn` (seconds, 0: none
/// since the book was opened). The place of the server is newer when the server gives the time
/// it got it and that time is after the last page turned here; a server without the time
/// compares the percentages. *newer tells which it is. An action of the reader goes there.
Decision decide(const Progress & here, const Progress & there, const std::string & model,
                const std::string & deviceId, long lastTurn, int forward, int backward, bool interactive,
                bool * newer);

/// A place that could not be sent, kept to be sent later (the queue of KOReader, KOSyncQueue),
/// with the time it was read here (`at`, seconds): a line "<document>|<percentage>|<at>|<progress>"
/// for the settings file. False for a line that is not one.
std::string queueLine(const std::string & document, const Progress & place, long at);
bool parseQueueLine(const std::string & line, std::string & document, Progress & place, long & at);

/// The queue with the place of a book kept: the line of the same book leaves its slot, the new one
/// goes last, the oldest lines leave a queue longer than max. Lines that are no lines are dropped.
void queueKeep(std::vector<std::string> & lines, const std::string & document, const Progress & place, long at,
               size_t max);
/// The queue without the line of the document (its place was sent); true when there was one.
bool queueDrop(std::vector<std::string> & lines, const std::string & document);
/// The places of the queue sent, the oldest first, as this device (model, deviceId). The place on
/// the server is asked first: one of another device that the server got after the place of the
/// queue was read here (that device read on meanwhile) is newer, and the place of the queue is
/// dropped, not sent over it (KOReader sends the queue blindly; a place read offline would win
/// over a newer one). A place of this device on the server is older than the queue (a place sent
/// takes its book out of the queue) and is not compared. A place sent, newer on the server, or
/// refused for good (4xx but 401 and 403) leaves the queue; no answer (no network, no server,
/// 5xx), a refused login (401, 403) or a redirect to another host stops the sending and keeps the
/// place with the ones after it. The number of places sent; *superseded (when
/// given) the number left for a newer place on the server.
int queueDrain(const Account & account, const std::string & model, const std::string & deviceId,
               std::vector<std::string> & lines, int * superseded = NULL);

/// What a book closed does with its place (KOReader: onCloseDocument, and the debounce of
/// updateProgress): sent while the network is on; kept for later without the network, or when a
/// place was sent less than DEBOUNCE_SECONDS ago and this one is another; nothing when it is the
/// place sent then.
enum CloseAction { CLOSE_PUSH, CLOSE_KEEP, CLOSE_SKIP };
CloseAction closeAction(bool online, bool debouncedNow, bool sameAsSent);

/// The place of a file read as pages (a PDF, a DjVu) as KOReader sends it: the page from 1, the
/// percentage of that page out of the pages.
void pagePlace(int page, int pages, Progress & place);
/// The page (from 0) of a place for a file of `pages` pages: the page KOReader sent (a number),
/// or the page of the percentage (a place of a book that is no number); within the file; -1
/// when the place has neither.
int pageOfPlace(const Progress & place, int pages);

}

#endif // PB_KOSYNC_API_H_INCLUDED
