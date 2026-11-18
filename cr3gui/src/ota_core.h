// Core of the program update (OTA). No firmware and no engine calls here, so the PC bench runs it:
//  - versions: "0.0.6-17-67-opds-alpha18", "v0.0.7-opds-alpha1", "0.0.7-opds"
//  - the answer of the GitHub "list releases" request (JSON, read with cJSON of thirdparty/cjson)
//    and the choice of the releases
//  - the text of the release notes
//  - SHA-256 of the downloaded package
#ifndef OTA_CORE_H
#define OTA_CORE_H

#include <cJSON.h>

#include <algorithm>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ota {

// ------------------------------------------------------------ versions

enum Stage { STAGE_ALPHA, STAGE_BETA, STAGE_RC, STAGE_RELEASE };

// "0.0.6-17-67-opds-alpha18": the numbers at the left, then the stage with its number.
// A version without a stage word is a release.
struct Version {
    std::vector<int> numbers;
    int stage;
    int stageNumber;
    Version() : stage(STAGE_RELEASE), stageNumber(0) {}
    bool valid() const { return !numbers.empty(); }
    // alpha, beta or release candidate
    bool test() const { return stage != STAGE_RELEASE; }
};

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
inline bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

inline Version parseVersion(const std::string & text)
{
    Version v;
    size_t i = 0, n = text.size();
    while (i < n && (text[i] == ' ' || text[i] == '\t'))
        i++;
    if (i < n && (text[i] == 'v' || text[i] == 'V'))
        i++;
    while (i < n && isDigit(text[i])) {
        long value = 0;
        while (i < n && isDigit(text[i])) {
            if (value < 100000000)
                value = value * 10 + (text[i] - '0');
            i++;
        }
        v.numbers.push_back((int)value);
        if (i + 1 < n && (text[i] == '.' || text[i] == '-') && isDigit(text[i + 1]))
            i++;
        else
            break;
    }
    std::string rest;
    for (; i < n; i++)
        rest += (char)(text[i] >= 'A' && text[i] <= 'Z' ? text[i] - 'A' + 'a' : text[i]);
    static const char * const names[] = { "alpha", "beta", "rc" };
    for (int k = 0; k < 3; k++) {
        size_t len = strlen(names[k]);
        size_t pos = 0;
        while ((pos = rest.find(names[k], pos)) != std::string::npos) {
            // a whole word: "rc1", not a part of another word
            bool wordStart = pos == 0 || !isLetter(rest[pos - 1]);
            bool wordEnd = pos + len >= rest.size() || !isLetter(rest[pos + len]);
            if (wordStart && wordEnd)
                break;
            pos += len;
        }
        if (pos == std::string::npos)
            continue;
        v.stage = k;
        size_t j = pos + len;
        while (j < rest.size() && (rest[j] == '.' || rest[j] == '-' || rest[j] == '_'))
            j++;
        long number = 0;
        while (j < rest.size() && isDigit(rest[j])) {
            if (number < 100000000)
                number = number * 10 + (rest[j] - '0');
            j++;
        }
        v.stageNumber = (int)number;
        break;
    }
    return v;
}

// -1: a is older than b, 0: the same, 1: a is newer
inline int compareVersions(const Version & a, const Version & b)
{
    size_t n = a.numbers.size() > b.numbers.size() ? a.numbers.size() : b.numbers.size();
    for (size_t i = 0; i < n; i++) {
        int x = i < a.numbers.size() ? a.numbers[i] : 0;
        int y = i < b.numbers.size() ? b.numbers[i] : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    if (a.stage != b.stage)
        return a.stage < b.stage ? -1 : 1;
    if (a.stageNumber != b.stageNumber)
        return a.stageNumber < b.stageNumber ? -1 : 1;
    return 0;
}

inline int compareVersions(const std::string & a, const std::string & b)
{
    return compareVersions(parseVersion(a), parseVersion(b));
}

// ------------------------------------------------------------ releases

// A release of the GitHub repository that has a package for this build.
struct Release {
    std::string version; // tag without the leading "v"
    std::string date;    // YYYY-MM-DD
    std::string notes;   // description of the release, as written on GitHub
    bool test;           // pre-release on GitHub, or an alpha / beta / rc version
    std::string file;    // name of the package
    std::string url;     // address of the package
    std::string sha256;  // lower-case hex; empty when GitHub gives none
    long long size;      // bytes
    Version parsed;
    Release() : test(false), size(0) {}
};

inline bool newerRelease(const Release & a, const Release & b)
{
    return compareVersions(a.parsed, b.parsed) > 0;
}

inline bool endsWith(const std::string & s, const std::string & tail)
{
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// size hex digits
inline bool isHex(const std::string & s, size_t size)
{
    if (s.size() != size)
        return false;
    for (size_t i = 0; i < s.size(); i++)
        if (!isDigit(s[i]) && !(s[i] >= 'a' && s[i] <= 'f') && !(s[i] >= 'A' && s[i] <= 'F'))
            return false;
    return true;
}

inline std::string lowerHex(const std::string & s)
{
    std::string r = s;
    for (size_t i = 0; i < r.size(); i++)
        if (r[i] >= 'A' && r[i] <= 'F')
            r[i] = (char)(r[i] - 'A' + 'a');
    return r;
}

// text of a string member of a JSON object; empty when there is no such member
inline std::string jsonText(const cJSON * object, const char * key)
{
    const cJSON * item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring ? std::string(item->valuestring) : std::string();
}

inline bool jsonTrue(const cJSON * object, const char * key)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(object, key)) != 0;
}

// Reads the answer of "GET /repos/<owner>/<repo>/releases". Keeps the published releases that
// have the package "*-<sdk>.zip", the newest version first. Returns false and the reason when
// the answer is not a list of releases (GitHub answers with {"message": ...} on an error).
inline bool parseReleases(const std::string & text, const std::string & sdk, std::vector<Release> & out,
                          std::string & error)
{
    out.clear();
    // the whole text must be one JSON value
    cJSON * root = cJSON_ParseWithOpts(text.c_str(), NULL, 1);
    if (!root) {
        error = "not JSON";
        return false;
    }
    if (!cJSON_IsArray(root)) {
        error = jsonText(root, "message");
        if (error.empty())
            error = "not a list of releases";
        cJSON_Delete(root);
        return false;
    }
    std::string tail = "-" + sdk + ".zip";
    const cJSON * r;
    cJSON_ArrayForEach(r, root) {
        if (!cJSON_IsObject(r) || jsonTrue(r, "draft"))
            continue;
        Release release;
        release.version = jsonText(r, "tag_name");
        if (!release.version.empty() && (release.version[0] == 'v' || release.version[0] == 'V'))
            release.version.erase(0, 1);
        release.parsed = parseVersion(release.version);
        if (!release.parsed.valid())
            continue;
        const cJSON * assets = cJSON_GetObjectItemCaseSensitive(r, "assets");
        if (!cJSON_IsArray(assets))
            continue;
        const cJSON * a;
        cJSON_ArrayForEach(a, assets) {
            std::string name = jsonText(a, "name");
            std::string state = jsonText(a, "state");
            if (!endsWith(name, tail) || (!state.empty() && state != "uploaded"))
                continue;
            release.file = name;
            release.url = jsonText(a, "browser_download_url");
            const cJSON * size = cJSON_GetObjectItemCaseSensitive(a, "size");
            release.size = cJSON_IsNumber(size) ? (long long)size->valuedouble : 0;
            std::string digest = jsonText(a, "digest");
            if (digest.compare(0, 7, "sha256:") == 0 && isHex(digest.substr(7), 64))
                release.sha256 = lowerHex(digest.substr(7));
            break;
        }
        if (release.url.empty())
            continue;
        release.test = jsonTrue(r, "prerelease") || release.parsed.test();
        release.date = jsonText(r, "published_at");
        if (release.date.empty())
            release.date = jsonText(r, "created_at");
        if (release.date.size() > 10)
            release.date.erase(10);
        release.notes = jsonText(r, "body");
        out.push_back(release);
    }
    cJSON_Delete(root);
    std::stable_sort(out.begin(), out.end(), newerRelease);
    return true;
}

// Releases offered to the user: not older than the first version that has this update (an older
// one could not be updated over the network again), test versions only when asked for.
inline std::vector<Release> offeredReleases(const std::vector<Release> & all, const Version & oldest, bool withTest)
{
    std::vector<Release> list;
    for (size_t i = 0; i < all.size(); i++) {
        if (all[i].test && !withTest)
            continue;
        if (compareVersions(all[i].parsed, oldest) < 0)
            continue;
        list.push_back(all[i]);
    }
    return list;
}

// The time of the automatic check has come. A clock set back does not block the check.
inline bool checkDue(long long now, long long last, long long period)
{
    return last <= 0 || now < last || now - last >= period;
}

// ------------------------------------------------------------ release notes

// Lines of the release notes without the Markdown marks: headings, bold, code, list bullets.
inline std::vector<std::string> noteLines(const std::string & notes)
{
    std::vector<std::string> lines;
    size_t pos = 0;
    bool lastEmpty = true; // no empty lines at the start, one between blocks
    while (pos <= notes.size()) {
        size_t end = notes.find('\n', pos);
        if (end == std::string::npos)
            end = notes.size();
        std::string line = notes.substr(pos, end - pos);
        pos = end + 1;
        std::string clean;
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            i++;
        size_t indent = i;
        while (i < line.size() && line[i] == '#')
            i++;
        if (i > indent)
            while (i < line.size() && line[i] == ' ')
                i++;
        if (i + 1 < line.size() && (line[i] == '-' || line[i] == '*') && line[i + 1] == ' ') {
            clean = indent >= 2 ? "    \xE2\x80\xA2 " : "\xE2\x80\xA2 "; // bullet
            i += 2;
        }
        for (; i < line.size(); i++) {
            char c = line[i];
            if (c == '\r' || c == '`')
                continue;
            if (c == '*' && i + 1 < line.size() && line[i + 1] == '*') {
                i++;
                continue;
            }
            clean += c;
        }
        while (!clean.empty() && (clean[clean.size() - 1] == ' ' || clean[clean.size() - 1] == '\t'))
            clean.erase(clean.size() - 1);
        if (clean.empty()) {
            if (!lastEmpty)
                lines.push_back(clean);
            lastEmpty = true;
        } else {
            lines.push_back(clean);
            lastEmpty = false;
        }
    }
    while (!lines.empty() && lines.back().empty())
        lines.pop_back();
    return lines;
}

// The start of the release notes for a message window: at most maxLines lines and maxBytes bytes
// of UTF-8, with an ellipsis when something is left out.
inline std::string shortNotes(const std::string & notes, int maxLines, size_t maxBytes)
{
    std::vector<std::string> lines = noteLines(notes);
    std::string text;
    bool cut = false;
    int count = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        if (lines[i].empty())
            continue;
        if (count >= maxLines) {
            cut = true;
            break;
        }
        if (!text.empty())
            text += "\n";
        text += lines[i];
        count++;
    }
    if (text.size() > maxBytes) {
        size_t at = maxBytes;
        while (at > 0 && ((unsigned char)text[at] & 0xC0) == 0x80) // not inside a character
            at--;
        text.erase(at);
        cut = true;
    }
    if (cut)
        text += "\xE2\x80\xA6";
    return text;
}

// A line without the Markdown marks of bold and code, from the position given; no trailing spaces
inline std::string cleanNoteText(const std::string & line, size_t from)
{
    std::string clean;
    for (size_t i = from; i < line.size(); i++) {
        char c = line[i];
        if (c == '\r' || c == '`')
            continue;
        if (c == '*' && i + 1 < line.size() && line[i + 1] == '*') {
            i++;
            continue;
        }
        clean += c;
    }
    while (!clean.empty() && (clean[clean.size() - 1] == ' ' || clean[clean.size() - 1] == '\t'))
        clean.erase(clean.size() - 1);
    return clean;
}

// The language a heading of the release notes names ("## English"); empty for any other heading.
// The notes of a release start in English and have the sections "## Русский" and "## Українська";
// the notes of the test versions start in Russian and have the section "## English".
inline std::string noteSectionLanguage(const std::string & heading)
{
    static const char * const names[][2] = {
        { "English", "en" }, { "english", "en" },
        { "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9", "ru" },                                 // Russian
        { "\xD0\xA3\xD0\xBA\xD1\x80\xD0\xB0\xD1\x97\xD0\xBD\xD1\x81\xD1\x8C\xD0\xBA\xD0\xB0", "uk" },         // Ukrainian
        { NULL, NULL }
    };
    for (int i = 0; names[i][0]; i++)
        if (heading == names[i][0])
            return names[i][1];
    return std::string();
}

// The part of the release notes for the language of the interface: the section under the heading
// of that language (the Russian section for Belarusian); without one, the text before the first
// language heading for Russian, Ukrainian and Belarusian, the English section for the other
// languages, and then the text before the first language heading. The whole text when the notes
// have no such part.
inline std::string notesForLanguage(const std::string & notes, const std::string & lang)
{
    std::vector<std::string> langs;  // language of every part; "" is the text before the headings
    std::vector<std::string> parts;
    langs.push_back(std::string());
    parts.push_back(std::string());
    size_t pos = 0;
    while (pos < notes.size()) {
        size_t end = notes.find('\n', pos);
        if (end == std::string::npos)
            end = notes.size();
        std::string line = notes.substr(pos, end - pos);
        pos = end + 1;
        size_t i = 0;
        while (i < line.size() && line[i] == '#')
            i++;
        if (i > 0) {
            while (i < line.size() && line[i] == ' ')
                i++;
            std::string name = noteSectionLanguage(cleanNoteText(line, i));
            if (!name.empty()) {
                langs.push_back(name);
                parts.push_back(std::string());
                continue;
            }
        }
        parts.back() += line;
        parts.back() += '\n';
    }
    std::string wanted[4];
    wanted[0] = lang;
    wanted[1] = (lang == "be") ? "ru" : lang;
    wanted[2] = (lang == "ru" || lang == "uk" || lang == "be") ? "" : "en";
    wanted[3] = "";
    for (int w = 0; w < 4; w++) {
        for (size_t i = 0; i < parts.size(); i++) {
            // the text before the headings is never asked for by a language code
            if (langs[i] != wanted[w] || (i == 0 && w == 0 && !lang.empty()))
                continue;
            if (parts[i].find_first_not_of(" \t\r\n") != std::string::npos)
                return parts[i];
        }
    }
    return notes;
}

enum NoteKind { NOTE_TEXT, NOTE_HEADING, NOTE_BULLET };

// A paragraph, a heading or a list item of the release notes
struct Note {
    int kind;
    int level;        // of a list item: 0, or 1 for an item inside an item
    std::string text;
    Note() : kind(NOTE_TEXT), level(0) {}
};

// The release notes as paragraphs, headings and list items. Lines of one paragraph and the
// lines that continue a list item are joined.
inline std::vector<Note> parseNotes(const std::string & notes)
{
    std::vector<Note> list;
    bool open = false; // the last note takes the next plain line
    size_t pos = 0;
    while (pos < notes.size()) {
        size_t end = notes.find('\n', pos);
        if (end == std::string::npos)
            end = notes.size();
        std::string line = notes.substr(pos, end - pos);
        pos = end + 1;
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
            i++;
        size_t indent = i;
        Note note;
        if (i < line.size() && line[i] == '#') {
            while (i < line.size() && line[i] == '#')
                i++;
            while (i < line.size() && line[i] == ' ')
                i++;
            note.kind = NOTE_HEADING;
        } else if (i + 1 < line.size() && (line[i] == '-' || line[i] == '*') && line[i + 1] == ' ') {
            i += 2;
            note.kind = NOTE_BULLET;
            note.level = indent >= 2 ? 1 : 0;
        }
        note.text = cleanNoteText(line, i);
        if (note.text.empty()) {
            open = false;
            continue;
        }
        if (note.kind == NOTE_TEXT && open && !list.empty() && list.back().kind != NOTE_HEADING) {
            list.back().text += " " + note.text;
            continue;
        }
        list.push_back(note);
        open = note.kind != NOTE_HEADING;
    }
    return list;
}

// ------------------------------------------------------------ SHA-256

class Sha256
{
    unsigned int _state[8];
    unsigned char _block[64];
    size_t _used;
    unsigned long long _bytes;

    static unsigned int rotr(unsigned int x, int n) { return (x >> n) | (x << (32 - n)); }

    void transform(const unsigned char * p)
    {
        static const unsigned int k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };
        unsigned int w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((unsigned int)p[i * 4] << 24) | ((unsigned int)p[i * 4 + 1] << 16)
                 | ((unsigned int)p[i * 4 + 2] << 8) | (unsigned int)p[i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            unsigned int s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            unsigned int s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        unsigned int a = _state[0], b = _state[1], c = _state[2], d = _state[3];
        unsigned int e = _state[4], f = _state[5], g = _state[6], h = _state[7];
        for (int i = 0; i < 64; i++) {
            unsigned int s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            unsigned int ch = (e & f) ^ (~e & g);
            unsigned int t1 = h + s1 + ch + k[i] + w[i];
            unsigned int s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            unsigned int maj = (a & b) ^ (a & c) ^ (b & c);
            unsigned int t2 = s0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        _state[0] += a; _state[1] += b; _state[2] += c; _state[3] += d;
        _state[4] += e; _state[5] += f; _state[6] += g; _state[7] += h;
    }
public:
    Sha256() : _used(0), _bytes(0)
    {
        static const unsigned int init[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
        };
        memcpy(_state, init, sizeof(_state));
    }
    void update(const void * data, size_t size)
    {
        const unsigned char * p = (const unsigned char *)data;
        _bytes += size;
        while (size > 0) {
            size_t n = 64 - _used;
            if (n > size)
                n = size;
            memcpy(_block + _used, p, n);
            _used += n;
            p += n;
            size -= n;
            if (_used == 64) {
                transform(_block);
                _used = 0;
            }
        }
    }
    // lower-case hex of the digest; the object is finished after it
    std::string hex()
    {
        unsigned long long bits = _bytes * 8;
        unsigned char pad = 0x80;
        update(&pad, 1);
        unsigned char zero = 0;
        while (_used != 56)
            update(&zero, 1);
        unsigned char length[8];
        for (int i = 0; i < 8; i++)
            length[i] = (unsigned char)(bits >> (56 - i * 8));
        update(length, 8);
        char text[65];
        for (int i = 0; i < 8; i++)
            snprintf(text + i * 8, 9, "%08x", _state[i]);
        return std::string(text, 64);
    }
};

// SHA-256 of a file as lower-case hex and its size; false when the file cannot be read
inline bool sha256File(const char * path, std::string & hex, long long & size)
{
    FILE * f = fopen(path, "rb");
    if (!f)
        return false;
    Sha256 sum;
    size = 0;
    unsigned char buf[16384];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        sum.update(buf, n);
        size += (long long)n;
    }
    bool ok = !ferror(f);
    fclose(f);
    if (ok)
        hex = sum.hex();
    return ok;
}

} // namespace ota

#endif // OTA_CORE_H
