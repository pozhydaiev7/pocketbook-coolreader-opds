// The position sync of KOReader: the protocol. See pb_kosync_api.h.
#include "pb_kosync_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "web.h"

namespace kosync {

const char * const DEFAULT_SERVER = "https://sync.koreader.rocks";

// ------------------------------------------------------------------------------------ MD5
// RFC 1321, for the key of a user and the id of a document; nothing else of the program needs it.
namespace {

typedef unsigned int u32;

struct Md5 {
    u32 a, b, c, d;
    unsigned long long length;
    unsigned char block[64];
    size_t used;
    Md5() : a(0x67452301), b(0xefcdab89), c(0x98badcfe), d(0x10325476), length(0), used(0) { }

    static u32 rol(u32 x, int n) { return (x << n) | (x >> (32 - n)); }

    void transform(const unsigned char * p)
    {
        static const u32 K[64] = {
            0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
            0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
            0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
            0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
            0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
            0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
            0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
            0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
        };
        static const int R[64] = {
            7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
            5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
            4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
            6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
        };
        u32 m[16];
        for (int i = 0; i < 16; i++)
            m[i] = (u32)p[i * 4] | ((u32)p[i * 4 + 1] << 8) | ((u32)p[i * 4 + 2] << 16) | ((u32)p[i * 4 + 3] << 24);
        u32 A = a, B = b, C = c, D = d;
        for (int i = 0; i < 64; i++) {
            u32 f;
            int g;
            if (i < 16) {
                f = (B & C) | (~B & D);
                g = i;
            } else if (i < 32) {
                f = (D & B) | (~D & C);
                g = (5 * i + 1) & 15;
            } else if (i < 48) {
                f = B ^ C ^ D;
                g = (3 * i + 5) & 15;
            } else {
                f = C ^ (B | ~D);
                g = (7 * i) & 15;
            }
            u32 t = D;
            D = C;
            C = B;
            B = B + rol(A + f + K[i] + m[g], R[i]);
            A = t;
        }
        a += A;
        b += B;
        c += C;
        d += D;
    }

    void update(const unsigned char * p, size_t n)
    {
        length += n;
        while (n > 0) {
            size_t take = 64 - used < n ? 64 - used : n;
            memcpy(block + used, p, take);
            used += take;
            p += take;
            n -= take;
            if (used == 64) {
                transform(block);
                used = 0;
            }
        }
    }

    std::string hex()
    {
        const unsigned long long bits = length * 8;
        unsigned char pad = 0x80;
        update(&pad, 1);
        unsigned char zero = 0;
        while (used != 56)
            update(&zero, 1);
        unsigned char len[8];
        for (int i = 0; i < 8; i++)
            len[i] = (unsigned char)(bits >> (8 * i));
        update(len, 8);
        const u32 words[4] = { a, b, c, d };
        char out[33];
        for (int w = 0; w < 4; w++)
            for (int i = 0; i < 4; i++)
                snprintf(out + w * 8 + i * 2, 3, "%02x", (words[w] >> (8 * i)) & 0xff);
        return std::string(out, 32);
    }
};

// The address of a request: the server without its last slash, then the path.
std::string urlOf(const Account & account, const std::string & path)
{
    std::string base = account.server.empty() ? std::string(DEFAULT_SERVER) : account.server;
    while (!base.empty() && base[base.size() - 1] == '/')
        base.erase(base.size() - 1);
    return base + path;
}

// A request of the protocol; short limits, as the plugin of KOReader has (a book is not held
// for long when it is closed without network).
Result send(const Account & account, const char * method, const std::string & path, const std::string & body,
            bool auth, std::string * answer)
{
    std::string url = urlOf(account, path);
    std::string text;
    Result r;
    bool verify = true;
    // at most: a redirect to the same host, and the request again without the check of the certificate
    for (int attempt = 0; attempt < 4; attempt++) {
        // told to stop: no request is started (no answer: the place stays in the queue)
        if (account.stopped()) {
            r.http = 0;
            r.error = "stopped";
            break;
        }
        WebRequest rq;
        rq.connectTimeout = account.connectTimeout;
        rq.timeout = account.timeout;
        rq.accept = "application/vnd.koreader.v1+json";
        rq.method = method;
        rq.body = body;
        // the user agent of the program, as for the update of the program (some servers refuse none)
        rq.userAgent = "CoolReader-PocketBook-OPDS";
        // the redirects are decided here (below): libcurl would send the key to any host
        rq.followRedirects = false;
        const bool https = url.size() > 8 && strncasecmp(url.c_str(), "https://", 8) == 0;
        if (https && verify) {
            rq.verifyPeer = true;
            rq.caInfo = account.caInfo;
        }
        if (auth) {
            rq.headers.push_back("x-auth-user: " + account.user);
            rq.headers.push_back("x-auth-key: " + account.key);
        }
        web::request(url, rq, &text, NULL);
        r.http = rq.httpCode;
        r.error = rq.error;
        r.curl = rq.curlCode;
        // A certificate the firmware does not know (old root certificates, a server of the user
        // with a certificate of its own): the request again without the check, as the update of
        // the program and the catalogs make it. A reader is for reading: the sync must work with
        // the servers there are.
        if (https && verify && (web::isCertificateError(rq.curlCode) || rq.curlCode == CURLE_SSL_CONNECT_ERROR)) {
            verify = false;
            continue;
        }
        // A redirect to the same host (http to https, a server behind a proxy) is followed; one to
        // another host is not: the login goes to the server that was set only. Nor one from https
        // down to http: no server needs it, and it would send the login in clear.
        const bool down = https && strncasecmp(rq.redirectUrl.c_str(), "https://", 8) != 0;
        if (r.http >= 300 && r.http < 400 && !rq.redirectUrl.empty() && !down && sameHost(url, rq.redirectUrl)) {
            url = rq.redirectUrl;
            continue;
        }
        break;
    }
    if (answer)
        *answer = text;
    return r;
}

// The value of a field of a flat JSON object: a string (unescaped) or the text of a number.
// Enough for the answers of the protocol, which are flat objects.
bool field(const std::string & json, const char * name, std::string & value, bool & isString)
{
    const std::string key = std::string("\"") + name + "\"";
    size_t at = 0;
    for (;;) {
        at = json.find(key, at);
        if (at == std::string::npos)
            return false;
        size_t p = at + key.size();
        while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\r' || json[p] == '\n'))
            p++;
        if (p < json.size() && json[p] == ':') {
            at = p + 1;
            break;
        }
        at = p;
    }
    while (at < json.size() && (json[at] == ' ' || json[at] == '\t' || json[at] == '\r' || json[at] == '\n'))
        at++;
    value.clear();
    if (at < json.size() && json[at] == '"') {
        isString = true;
        for (size_t i = at + 1; i < json.size(); i++) {
            char ch = json[i];
            if (ch == '"')
                return true;
            if (ch == '\\' && i + 1 < json.size()) {
                char e = json[++i];
                switch (e) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case 'r': value += '\r'; break;
                case 'b': value += '\b'; break;
                case 'f': value += '\f'; break;
                case 'u':
                    if (i + 4 < json.size()) {
                        unsigned code = (unsigned)strtoul(json.substr(i + 1, 4).c_str(), NULL, 16);
                        i += 4;
                        // UTF-8 of the code (the place and the names are ASCII in practice)
                        if (code < 0x80) {
                            value += (char)code;
                        } else if (code < 0x800) {
                            value += (char)(0xC0 | (code >> 6));
                            value += (char)(0x80 | (code & 0x3F));
                        } else {
                            value += (char)(0xE0 | (code >> 12));
                            value += (char)(0x80 | ((code >> 6) & 0x3F));
                            value += (char)(0x80 | (code & 0x3F));
                        }
                    }
                    break;
                default: value += e; break;
                }
                continue;
            }
            value += ch;
        }
        return false;
    }
    isString = false;
    while (at < json.size() && json[at] != ',' && json[at] != '}' && json[at] != ' ' && json[at] != '\r'
           && json[at] != '\n')
        value += json[at++];
    return !value.empty() && value != "null";
}

} // namespace

bool asyncNameLookup()
{
    curl_version_info_data * info = curl_version_info(CURLVERSION_NOW);
    return info != NULL && (info->features & CURL_VERSION_ASYNCHDNS) != 0;
}

std::string md5Hex(const std::string & data)
{
    Md5 m;
    m.update((const unsigned char *)data.data(), data.size());
    return m.hex();
}

std::string partialMd5(const char * path)
{
    FILE * f = fopen(path, "rb");
    if (f == NULL)
        return std::string();
    Md5 m;
    unsigned char buf[1024];
    // i = -1 gives the offset 0: LuaJIT shifts by the count modulo 32, and 1024 << 30 is 0 in 32 bits
    for (int i = -1; i <= 10; i++) {
        // (the largest offset, 1 GiB, fits a long of 32 bits)
        const long offset = i < 0 ? 0 : 1024L << (2 * i);
        if (fseek(f, offset, SEEK_SET) != 0)
            break;
        size_t n = fread(buf, 1, sizeof(buf), f);
        if (n == 0)
            break;
        m.update(buf, n);
    }
    fclose(f);
    return m.hex();
}

double roundPercent(double percent)
{
    // A value cut before is cut again where it is compared and where it is sent, and must stay
    // the same: 0.7515 is kept as 0.75149999..., times 10000 it is 7514.99..., and the plain
    // floor gave 0.7514 (the page 121 of 161 went to the server so). The millionth of a step
    // makes up for that.
    return floor(percent * 10000.0 + 1e-6) / 10000.0;
}

std::string jsonString(const std::string & s)
{
    std::string out = "\"";
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == '"' || ch == '\\') {
            out += '\\';
            out += (char)ch;
        } else if (ch < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", ch);
            out += buf;
        } else {
            out += (char)ch;
        }
    }
    return out + "\"";
}

bool sameHost(const std::string & a, const std::string & b)
{
    const std::string ha = web::hostOf(a);
    return !ha.empty() && ha == web::hostOf(b);
}

bool debounced(long now, long last)
{
    return last != 0 && now >= last && now - last <= DEBOUNCE_SECONDS;
}

Decision decide(const Progress & here, const Progress & there, const std::string & model,
                const std::string & deviceId, long lastTurn, int forward, int backward, bool interactive,
                bool * newer)
{
    if (newer)
        *newer = false;
    if (!there.found)
        return DECIDE_NONE;
    if (there.device == model && there.deviceId == deviceId)
        return DECIDE_FROM_HERE;
    const double percentage = roundPercent(there.percentage);
    if (percentage == roundPercent(here.percentage) || there.progress == here.progress)
        return DECIDE_SAME;
    if (interactive)
        return DECIDE_GO;
    const bool isNewer = there.timestamp != 0 ? there.timestamp > lastTurn : percentage > here.percentage;
    if (newer)
        *newer = isNewer;
    const int strategy = isNewer ? forward : backward;
    return strategy == STRATEGY_SILENT ? DECIDE_GO : strategy == STRATEGY_PROMPT ? DECIDE_ASK : DECIDE_IGNORE;
}

std::string queueLine(const std::string & document, const Progress & place, long at)
{
    char fields[64];
    snprintf(fields, sizeof(fields), "|%.4f|%ld|", roundPercent(place.percentage), at);
    return document + fields + place.progress;
}

bool parseQueueLine(const std::string & line, std::string & document, Progress & place, long & at)
{
    const size_t a = line.find('|');
    const size_t b = a == std::string::npos ? a : line.find('|', a + 1);
    const size_t c = b == std::string::npos ? b : line.find('|', b + 1);
    if (c == std::string::npos || a == 0 || c == b + 1)
        return false;
    document = line.substr(0, a);
    place.percentage = atof(line.substr(a + 1, b - a - 1).c_str());
    at = atol(line.substr(b + 1, c - b - 1).c_str());
    place.progress = line.substr(c + 1);
    return true;
}

bool queueDrop(std::vector<std::string> & lines, const std::string & document)
{
    for (size_t i = 0; i < lines.size(); i++) {
        std::string d;
        Progress p;
        long t = 0;
        if (parseQueueLine(lines[i], d, p, t) && d == document) {
            lines.erase(lines.begin() + i);
            return true;
        }
    }
    return false;
}

void queueKeep(std::vector<std::string> & lines, const std::string & document, const Progress & place, long at,
               size_t max)
{
    std::vector<std::string> kept;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string d;
        Progress p;
        long t = 0;
        if (parseQueueLine(lines[i], d, p, t) && d != document)
            kept.push_back(lines[i]);
    }
    kept.push_back(queueLine(document, place, at));
    if (kept.size() > max)
        kept.erase(kept.begin(), kept.begin() + (kept.size() - max));
    lines.swap(kept);
}

// No answer, or a login the server refuses: the sending stops and the place stays in the queue.
static bool keepQueue(const Result & r)
{
    return r.http < 400 || r.http >= 500 || r.http == 401 || r.http == 403;
}

int queueDrain(const Account & account, const std::string & model, const std::string & deviceId,
               std::vector<std::string> & lines, int * superseded)
{
    int sent = 0, newer = 0;
    size_t done = 0;
    for (; done < lines.size(); done++) {
        // told to stop: this place and the ones after it stay in the queue
        if (account.stopped())
            break;
        std::string document;
        Progress place;
        long at = 0;
        if (!parseQueueLine(lines[done], document, place, at))
            continue;
        Progress there;
        Result r = pull(account, document, there);
        if (account.stopped() || (!r.ok() && keepQueue(r)))
            break;
        // another 4xx of the question (a server that tells "no place yet" so): the push decides
        const bool fromHere = there.device == model && there.deviceId == deviceId;
        if (r.ok() && there.found && !fromHere && there.timestamp > at) {
            newer++;
            continue;
        }
        place.device = model;
        place.deviceId = deviceId;
        r = push(account, document, place);
        if (r.ok())
            sent++;
        else if (keepQueue(r))
            break;
    }
    lines.erase(lines.begin(), lines.begin() + done);
    if (superseded)
        *superseded = newer;
    return sent;
}

CloseAction closeAction(bool online, bool debouncedNow, bool sameAsSent)
{
    if (!online)
        return CLOSE_KEEP;
    if (debouncedNow)
        return sameAsSent ? CLOSE_SKIP : CLOSE_KEEP;
    return CLOSE_PUSH;
}

void pagePlace(int page, int pages, Progress & place)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", page + 1);
    place.progress = buf;
    place.percentage = pages > 0 ? roundPercent((double)(page + 1) / pages) : 0;
}

int pageOfPlace(const Progress & place, int pages)
{
    if (pages < 1)
        return -1;
    const std::string & s = place.progress;
    bool number = !s.empty() && s.size() < 9;
    for (size_t i = 0; number && i < s.size(); i++)
        number = s[i] >= '0' && s[i] <= '9';
    int page;
    if (number)
        page = atoi(s.c_str()) - 1;
    else if (place.percentage > 0)
        page = (int)(place.percentage * pages + 0.5) - 1;
    else
        return -1;
    if (page < 0)
        page = 0;
    if (page >= pages)
        page = pages - 1;
    return page;
}

Result registerUser(const Account & account)
{
    const std::string body = "{\"username\":" + jsonString(account.user) + ",\"password\":" + jsonString(account.key) + "}";
    return send(account, "POST", "/users/create", body, false, NULL);
}

Result authorize(const Account & account)
{
    return send(account, NULL, "/users/auth", std::string(), true, NULL);
}

Result push(const Account & account, const std::string & document, const Progress & progress)
{
    char percent[32];
    snprintf(percent, sizeof(percent), "%.4f", roundPercent(progress.percentage));
    const std::string body = "{\"document\":" + jsonString(document) + ",\"progress\":" + jsonString(progress.progress)
        + ",\"percentage\":" + percent + ",\"device\":" + jsonString(progress.device)
        + ",\"device_id\":" + jsonString(progress.deviceId) + "}";
    return send(account, "PUT", "/syncs/progress", body, true, NULL);
}

Result pull(const Account & account, const std::string & document, Progress & out)
{
    out = Progress();
    std::string answer;
    Result r = send(account, NULL, "/syncs/progress/" + document, std::string(), true, &answer);
    if (!r.ok())
        return r;
    std::string value;
    bool isString = false;
    if (field(answer, "percentage", value, isString)) {
        out.percentage = atof(value.c_str());
        out.found = true;
    }
    if (field(answer, "progress", value, isString))
        out.progress = value;
    if (field(answer, "device", value, isString))
        out.device = value;
    if (field(answer, "device_id", value, isString))
        out.deviceId = value;
    if (field(answer, "timestamp", value, isString))
        out.timestamp = atol(value.c_str());
    // a server that keeps a place without its percentage still gives a place
    if (!out.progress.empty())
        out.found = true;
    return r;
}

}
