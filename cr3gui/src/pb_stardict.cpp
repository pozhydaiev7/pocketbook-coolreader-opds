/*
 * StarDict dictionary reader for CoolReader on PocketBook, see pb_stardict.h.
 * Format: StarDict 2.4.2 / 3.0.0 (.ifo, .idx, .idx.gz, .syn, .dict, .dict.dz).
 */
#include "pb_stardict.h"

#include <string.h>
#include <stdlib.h>
#include <zlib.h>

// ---------------------------------------------------------------------------------------------
// helpers

static unsigned readBE32(const unsigned char * p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | (unsigned)p[3];
}

static int asciiLower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

int sdCompare(const char * s1, const char * s2)
{
    const unsigned char * a = (const unsigned char *)s1;
    const unsigned char * b = (const unsigned char *)s2;
    // g_ascii_strcasecmp
    for (;; a++, b++) {
        int c1 = asciiLower(*a);
        int c2 = asciiLower(*b);
        if (c1 != c2)
            return c1 - c2;
        if (!c1)
            break;
    }
    // then strcmp
    return strcmp(s1, s2);
}

static bool fileExists(const std::string & path)
{
    FILE * f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

// Reads a whole file; gzread also reads files that are not compressed
static bool readWholeFile(const std::string & path, std::string & out)
{
    out.clear();
    gzFile f = gzopen(path.c_str(), "rb");
    if (!f)
        return false;
    char buf[65536];
    for (;;) {
        int n = gzread(f, buf, sizeof(buf));
        if (n < 0) {
            gzclose(f);
            out.clear();
            return false;
        }
        if (n == 0)
            break;
        out.append(buf, n);
    }
    gzclose(f);
    return true;
}

static void appendEscaped(std::string & out, const char * s, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '\r': break;
        default: out += c; break;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// SdDictData: .dict or dictzip .dict.dz

SdDictData::SdDictData()
    : _file(NULL), _compressed(false), _chunkLength(0), _cachedChunk(-1)
{
}

SdDictData::~SdDictData()
{
    close();
}

void SdDictData::close()
{
    if (_file)
        fclose(_file);
    _file = NULL;
    _compressed = false;
    _chunkLength = 0;
    _chunkSizes.clear();
    _chunkOffsets.clear();
    _cachedChunk = -1;
    _cache.clear();
}

bool SdDictData::open(const char * path)
{
    close();
    _file = fopen(path, "rb");
    if (!_file)
        return false;
    unsigned char h[10];
    if (fread(h, 1, 10, _file) != 10 || h[0] != 0x1f || h[1] != 0x8b) {
        // not gzip: plain .dict
        return true;
    }
    if (h[2] != 8)
        return false;
    unsigned char flags = h[3];
    long pos = 10;
    bool haveChunks = false;
    if (flags & 4) { // FEXTRA
        unsigned char x[2];
        if (fread(x, 1, 2, _file) != 2)
            return false;
        unsigned xlen = x[0] | (x[1] << 8);
        std::string extra(xlen, '\0');
        if (xlen && fread(&extra[0], 1, xlen, _file) != xlen)
            return false;
        pos += 2 + xlen;
        // subfields: SI1 SI2 LEN(2) data; dictzip uses 'R' 'A'
        size_t p = 0;
        while (p + 4 <= extra.size()) {
            const unsigned char * e = (const unsigned char *)extra.data() + p;
            unsigned len = e[2] | (e[3] << 8);
            if (p + 4 + len > extra.size())
                break;
            if (e[0] == 'R' && e[1] == 'A' && len >= 6) {
                const unsigned char * d = e + 4;
                _chunkLength = d[2] | (d[3] << 8);
                unsigned count = d[4] | (d[5] << 8);
                if (6 + count * 2 <= len) {
                    for (unsigned i = 0; i < count; i++)
                        _chunkSizes.push_back(d[6 + i * 2] | (d[7 + i * 2] << 8));
                    haveChunks = _chunkLength > 0 && count > 0;
                }
            }
            p += 4 + len;
        }
    }
    if (flags & 8) { // FNAME
        int c;
        while ((c = fgetc(_file)) > 0)
            pos++;
        pos++;
    }
    if (flags & 16) { // FCOMMENT
        int c;
        while ((c = fgetc(_file)) > 0)
            pos++;
        pos++;
    }
    if (flags & 2) // FHCRC
        pos += 2;
    if (!haveChunks) {
        // plain gzip without random access: unpack it whole once
        fclose(_file);
        _file = NULL;
        std::string all;
        if (!readWholeFile(path, all))
            return false;
        _compressed = true;
        _chunkLength = 0;
        _cache.swap(all);
        _cachedChunk = 0;
        return true;
    }
    long long offset = pos;
    for (size_t i = 0; i < _chunkSizes.size(); i++) {
        _chunkOffsets.push_back(offset);
        offset += _chunkSizes[i];
    }
    _compressed = true;
    return true;
}

bool SdDictData::loadChunk(unsigned n)
{
    if ((long long)n == _cachedChunk)
        return true;
    if (n >= _chunkSizes.size() || !_file)
        return false;
    std::string packed(_chunkSizes[n], '\0');
    if (fseek(_file, (long)_chunkOffsets[n], SEEK_SET) != 0 ||
        fread(&packed[0], 1, packed.size(), _file) != packed.size())
        return false;
    _cache.assign(_chunkLength, '\0');
    z_stream z;
    memset(&z, 0, sizeof(z));
    if (inflateInit2(&z, -15) != Z_OK)
        return false;
    z.next_in = (Bytef *)&packed[0];
    z.avail_in = packed.size();
    z.next_out = (Bytef *)&_cache[0];
    z.avail_out = _chunkLength;
    int rc = inflate(&z, Z_SYNC_FLUSH);
    unsigned got = _chunkLength - z.avail_out;
    inflateEnd(&z);
    if (rc != Z_OK && rc != Z_STREAM_END) {
        _cachedChunk = -1;
        _cache.clear();
        return false;
    }
    _cache.resize(got);
    _cachedChunk = n;
    return true;
}

bool SdDictData::read(unsigned long long start, unsigned size, std::string & out)
{
    out.clear();
    if (!_compressed) {
        if (!_file || fseek(_file, (long)start, SEEK_SET) != 0)
            return false;
        out.resize(size);
        if (size && fread(&out[0], 1, size, _file) != size) {
            out.clear();
            return false;
        }
        return true;
    }
    if (_chunkLength == 0) {
        // unpacked whole
        if (start + size > _cache.size())
            return false;
        out.assign(_cache, (size_t)start, size);
        return true;
    }
    while (size > 0) {
        unsigned n = (unsigned)(start / _chunkLength);
        if (!loadChunk(n))
            return false;
        unsigned inChunk = (unsigned)(start - (unsigned long long)n * _chunkLength);
        if (inChunk >= _cache.size())
            return false;
        unsigned take = _cache.size() - inChunk;
        if (take > size)
            take = size;
        out.append(_cache, inChunk, take);
        start += take;
        size -= take;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// StarDict

StarDict::StarDict()
    : _wordCount(0), _synWordCount(0), _offsetBits(32), _loaded(false), _synLoaded(false)
{
}

StarDict::~StarDict()
{
    unload();
}

bool StarDict::readInfo(const char * ifoPath)
{
    std::string text;
    _ifoPath = ifoPath;
    if (!readWholeFile(_ifoPath, text)) {
        _error = "cannot read .ifo";
        return false;
    }
    const char * magic = "StarDict's dict ifo file";
    // skip a UTF-8 byte order mark
    size_t start = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
    if (text.compare(start, strlen(magic), magic) != 0) {
        _error = "not a StarDict .ifo";
        return false;
    }
    size_t pos = start;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (key == "bookname")
            _bookName = value;
        else if (key == "wordcount")
            _wordCount = (unsigned)strtoul(value.c_str(), NULL, 10);
        else if (key == "synwordcount")
            _synWordCount = (unsigned)strtoul(value.c_str(), NULL, 10);
        else if (key == "idxoffsetbits")
            _offsetBits = atoi(value.c_str()) == 64 ? 64 : 32;
        else if (key == "sametypesequence")
            _sameTypeSequence = value;
    }
    _basePath = _ifoPath.substr(0, _ifoPath.size() - 4);
    if (_bookName.empty()) {
        size_t slash = _basePath.find_last_of('/');
        _bookName = slash == std::string::npos ? _basePath : _basePath.substr(slash + 1);
    }
    return true;
}

bool StarDict::load()
{
    if (_loaded)
        return true;
    std::string idxPath = _basePath + ".idx";
    if (!fileExists(idxPath))
        idxPath += ".gz";
    if (!readWholeFile(idxPath, _idx)) {
        _error = "cannot read " + idxPath;
        return false;
    }
    unsigned tail = (_offsetBits == 64 ? 8 : 4) + 4;
    _entries.clear();
    _entries.reserve(_wordCount ? _wordCount : _idx.size() / 16);
    size_t pos = 0;
    while (pos < _idx.size()) {
        const char * nul = (const char *)memchr(_idx.data() + pos, 0, _idx.size() - pos);
        if (!nul)
            break;
        size_t next = (nul - _idx.data()) + 1 + tail;
        if (next > _idx.size())
            break;
        _entries.push_back((unsigned)pos);
        pos = next;
    }
    std::string dictPath = _basePath + ".dict";
    if (!fileExists(dictPath))
        dictPath += ".dz";
    if (!_data.open(dictPath.c_str())) {
        _error = "cannot open " + dictPath;
        _idx.clear();
        _entries.clear();
        return false;
    }
    _loaded = true;
    return true;
}

void StarDict::unload()
{
    _loaded = false;
    _synLoaded = false;
    std::string().swap(_idx);
    std::vector<unsigned>().swap(_entries);
    std::string().swap(_syn);
    std::vector<unsigned>().swap(_synEntries);
    _data.close();
}

bool StarDict::loadSyn()
{
    if (_synLoaded)
        return !_synEntries.empty();
    _synLoaded = true;
    std::string synPath = _basePath + ".syn";
    if (!fileExists(synPath))
        return false;
    if (!readWholeFile(synPath, _syn))
        return false;
    _synEntries.reserve(_synWordCount ? _synWordCount : _syn.size() / 16);
    size_t pos = 0;
    while (pos < _syn.size()) {
        const char * nul = (const char *)memchr(_syn.data() + pos, 0, _syn.size() - pos);
        if (!nul)
            break;
        size_t next = (nul - _syn.data()) + 1 + 4;
        if (next > _syn.size())
            break;
        _synEntries.push_back((unsigned)pos);
        pos = next;
    }
    return !_synEntries.empty();
}

const char * StarDict::wordAt(int index) const
{
    if (index < 0 || index >= (int)_entries.size())
        return NULL;
    return _idx.data() + _entries[index];
}

int StarDict::lowerBound(const char * word) const
{
    int lo = 0, hi = (int)_entries.size();
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (sdCompare(_idx.data() + _entries[mid], word) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

int StarDict::findExact(const char * word) const
{
    int i = lowerBound(word);
    if (i < (int)_entries.size() && strcmp(_idx.data() + _entries[i], word) == 0)
        return i;
    return -1;
}

int StarDict::findSynonym(const char * word)
{
    if (!loadSyn())
        return -1;
    int lo = 0, hi = (int)_synEntries.size();
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (sdCompare(_syn.data() + _synEntries[mid], word) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= (int)_synEntries.size())
        return -1;
    const char * s = _syn.data() + _synEntries[lo];
    if (strcmp(s, word) != 0)
        return -1;
    unsigned index = readBE32((const unsigned char *)s + strlen(s) + 1);
    return index < _entries.size() ? (int)index : -1;
}

void StarDict::entryLocation(int index, unsigned long long & offset, unsigned & size) const
{
    const char * w = _idx.data() + _entries[index];
    const unsigned char * p = (const unsigned char *)w + strlen(w) + 1;
    if (_offsetBits == 64) {
        offset = ((unsigned long long)readBE32(p) << 32) | readBE32(p + 4);
        p += 8;
    } else {
        offset = readBE32(p);
        p += 4;
    }
    size = readBE32(p);
}

void StarDict::convertPart(char type, const char * data, size_t size, std::string & out)
{
    if (!out.empty() && out[out.size() - 1] != '\n')
        out += '\n';
    switch (type) {
    case 'm': // plain text
    case 'l': // text in the locale charset, UTF-8 in practice
    case 'y': // Chinese YinBiao or Japanese KANA
    case 'w': // MediaWiki markup, shown as text
        appendEscaped(out, data, size);
        break;
    case 't': // phonetic transcription
        out += '[';
        appendEscaped(out, data, size);
        out += ']';
        break;
    case 'h': // HTML
    case 'g': // Pango markup
    case 'x': // XDXF
        sdMarkupToArticle(data, size, type, out);
        break;
    default:  // resources, pictures, sounds
        break;
    }
}

bool StarDict::articleAt(int index, std::string & article)
{
    article.clear();
    if (!_loaded || index < 0 || index >= (int)_entries.size())
        return false;
    unsigned long long offset;
    unsigned size;
    entryLocation(index, offset, size);
    std::string raw;
    if (!_data.read(offset, size, raw)) {
        _error = "cannot read the article";
        return false;
    }
    const char * p = raw.data();
    const char * end = p + raw.size();
    if (!_sameTypeSequence.empty()) {
        for (size_t i = 0; i < _sameTypeSequence.size() && p <= end; i++) {
            char type = _sameTypeSequence[i];
            bool last = i + 1 == _sameTypeSequence.size();
            bool upper = type >= 'A' && type <= 'Z';
            size_t len;
            if (last) {
                len = end - p;
            } else if (upper) {
                if (end - p < 4)
                    break;
                len = readBE32((const unsigned char *)p);
                p += 4;
            } else {
                const char * nul = (const char *)memchr(p, 0, end - p);
                len = nul ? (size_t)(nul - p) : (size_t)(end - p);
            }
            if (len > (size_t)(end - p))
                len = end - p;
            if (!upper)
                convertPart(type, p, len, article);
            p += len;
            if (!last && !upper)
                p++; // the terminating zero
        }
    } else {
        while (p < end) {
            char type = *p++;
            bool upper = type >= 'A' && type <= 'Z';
            size_t len;
            if (upper) {
                if (end - p < 4)
                    break;
                len = readBE32((const unsigned char *)p);
                p += 4;
                if (len > (size_t)(end - p))
                    len = end - p;
            } else {
                const char * nul = (const char *)memchr(p, 0, end - p);
                len = nul ? (size_t)(nul - p) : (size_t)(end - p);
                convertPart(type, p, len, article);
            }
            p += len;
            if (!upper)
                p++;
        }
    }
    // no empty paragraphs at the ends
    while (!article.empty() && article[article.size() - 1] == '\n')
        article.erase(article.size() - 1);
    size_t first = article.find_first_not_of('\n');
    if (first != std::string::npos && first > 0)
        article.erase(0, first);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Markup conversion: HTML ('h'), Pango ('g'), XDXF ('x') to article markup

namespace {

struct OpenTag
{
    std::string source; // tag name in the source
    std::string open;   // what was written for it
    std::string close;
};

class MarkupWriter
{
public:
    MarkupWriter(std::string & out) : _out(out), _lineStart(true), _bullet(false) {}

    void text(const char * s, size_t len)
    {
        if (!len)
            return;
        if (_bullet) {
            // list item mark, written before the item's first text only
            _bullet = false;
            text("\xE2\x80\xA2 ", 4);
        }
        _out.append(s, len);
        _lineStart = false;
    }
    void bullet()
    {
        _bullet = true;
    }
    void space()
    {
        if (!_lineStart && !_out.empty() && _out[_out.size() - 1] != ' ')
            _out += ' ';
    }
    void paragraph()
    {
        if (_lineStart)
            return;
        closeAll();
        _out += '\n';
        reopenAll();
        _lineStart = true;
    }
    void push(const std::string & source, const std::string & open, const std::string & close)
    {
        OpenTag t;
        t.source = source;
        t.open = open;
        t.close = close;
        _stack.push_back(t);
        _out += open;
    }
    void pop(const std::string & source)
    {
        int i = (int)_stack.size() - 1;
        while (i >= 0 && _stack[i].source != source)
            i--;
        if (i < 0)
            return; // closing tag without an opening one
        // close the tags opened inside it, then it, then reopen the inner ones
        for (int j = (int)_stack.size() - 1; j >= i; j--)
            _out += _stack[j].close;
        std::vector<OpenTag> inner(_stack.begin() + i + 1, _stack.end());
        _stack.resize(i);
        for (size_t j = 0; j < inner.size(); j++) {
            _out += inner[j].open;
            _stack.push_back(inner[j]);
        }
    }
    void finish()
    {
        closeAll();
        _stack.clear();
    }
private:
    std::string & _out;
    std::vector<OpenTag> _stack;
    bool _lineStart;
    bool _bullet;

    void closeAll()
    {
        for (int j = (int)_stack.size() - 1; j >= 0; j--)
            _out += _stack[j].close;
    }
    void reopenAll()
    {
        for (size_t j = 0; j < _stack.size(); j++)
            _out += _stack[j].open;
    }
};

std::string lowerAscii(const std::string & s)
{
    std::string r(s);
    for (size_t i = 0; i < r.size(); i++)
        r[i] = (char)asciiLower((unsigned char)r[i]);
    return r;
}

// value of attribute name in the attribute text of a tag, "" when missing
std::string attribute(const std::string & attrs, const char * name)
{
    std::string lower = lowerAscii(attrs);
    size_t nlen = strlen(name);
    size_t pos = 0;
    while ((pos = lower.find(name, pos)) != std::string::npos) {
        bool startOk = pos == 0 || lower[pos - 1] == ' ' || lower[pos - 1] == '\t' || lower[pos - 1] == '\n';
        size_t p = pos + nlen;
        while (p < lower.size() && lower[p] == ' ')
            p++;
        if (!startOk || p >= lower.size() || lower[p] != '=') {
            pos += nlen;
            continue;
        }
        p++;
        while (p < lower.size() && lower[p] == ' ')
            p++;
        if (p >= attrs.size())
            return "";
        char q = attrs[p];
        if (q == '"' || q == '\'') {
            size_t e = attrs.find(q, p + 1);
            return attrs.substr(p + 1, e == std::string::npos ? std::string::npos : e - p - 1);
        }
        size_t e = p;
        while (e < attrs.size() && attrs[e] != ' ' && attrs[e] != '>' && attrs[e] != '/')
            e++;
        return attrs.substr(p, e - p);
    }
    return "";
}

// "#12ab34" or "red"; anything else is dropped
std::string cleanColor(const std::string & value)
{
    std::string r;
    for (size_t i = 0; i < value.size(); i++) {
        char c = value[i];
        if (c == ' ' && r.empty())
            continue;
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '#')
            r += c;
        else
            break;
    }
    return r;
}

std::string colorFromStyle(const std::string & style)
{
    std::string lower = lowerAscii(style);
    size_t pos = 0;
    while ((pos = lower.find("color", pos)) != std::string::npos) {
        // not background-color
        if (pos > 0 && lower[pos - 1] == '-') {
            pos += 5;
            continue;
        }
        size_t p = pos + 5;
        while (p < lower.size() && lower[p] == ' ')
            p++;
        if (p < lower.size() && lower[p] == ':')
            return cleanColor(style.substr(p + 1));
        pos = p;
    }
    return "";
}

bool isOneOf(const std::string & name, const char * const * names)
{
    for (; *names; names++)
        if (name == *names)
            return true;
    return false;
}

} // namespace

void sdMarkupToArticle(const char * src, size_t size, char type, std::string & out)
{
    static const char * const blockTags[] = {
        "p", "div", "br", "li", "tr", "table", "ul", "ol", "dl", "dt", "dd", "blockquote", "hr",
        "h1", "h2", "h3", "h4", "h5", "h6", "section", "article", "header", "footer", "pre",
        "def", "ar", "co", NULL };
    static const char * const strongTags[] = { "b", "strong", "big", NULL };
    static const char * const emphasisTags[] = { "i", "em", "cite", "var", "dfn", "ex", "abr", "kref", "iref", NULL };
    static const char * const skipContentTags[] = { "script", "style", "head", "title", "rref", NULL };

    MarkupWriter w(out);
    bool newlineIsBreak = type != 'h';
    size_t i = 0;
    while (i < size) {
        char c = src[i];
        if (c == '<') {
            size_t end = i + 1;
            while (end < size && src[end] != '>')
                end++;
            if (end >= size) {
                w.text("&lt;", 4);
                i++;
                continue;
            }
            std::string tag(src + i + 1, end - i - 1);
            i = end + 1;
            if (tag.empty() || tag[0] == '!' || tag[0] == '?')
                continue; // comment, doctype, processing instruction
            bool closing = tag[0] == '/';
            size_t nameStart = closing ? 1 : 0;
            size_t nameEnd = nameStart;
            while (nameEnd < tag.size() && tag[nameEnd] != ' ' && tag[nameEnd] != '/' &&
                   tag[nameEnd] != '\t' && tag[nameEnd] != '\n')
                nameEnd++;
            std::string name = lowerAscii(tag.substr(nameStart, nameEnd - nameStart));
            std::string attrs = tag.substr(nameEnd);
            bool selfClosing = !tag.empty() && tag[tag.size() - 1] == '/';

            if (!closing && isOneOf(name, skipContentTags)) {
                // drop everything up to the closing tag
                std::string closeTag = "</" + name;
                size_t p = i;
                while (p < size) {
                    if (src[p] == '<' && p + closeTag.size() <= size &&
                        lowerAscii(std::string(src + p, closeTag.size())) == closeTag)
                        break;
                    p++;
                }
                while (p < size && src[p] != '>')
                    p++;
                i = p < size ? p + 1 : size;
                continue;
            }
            if (name == "tr" && type == 'x') {
                // XDXF transcription; in HTML it is a table row, a block tag below
                w.text(closing ? "]" : "[", 1);
                continue;
            }
            if (isOneOf(name, blockTags)) {
                w.paragraph();
                if (name == "li" && !closing)
                    w.bullet();
                continue;
            }
            if (name == "img" || selfClosing)
                continue;
            if (closing) {
                w.pop(name);
                continue;
            }
            if (isOneOf(name, strongTags) || (name == "k" && type == 'x')) {
                w.push(name, "<strong>", "</strong>");
            } else if (isOneOf(name, emphasisTags)) {
                w.push(name, "<emphasis>", "</emphasis>");
            } else if (name == "sup" || name == "sub") {
                w.push(name, "<" + name + ">", "</" + name + ">");
            } else if (name == "font" || name == "span" || name == "c") {
                std::string color;
                if (name == "font")
                    color = cleanColor(attribute(attrs, "color"));
                else if (name == "c")
                    color = cleanColor(attribute(attrs, "c"));
                else {
                    color = colorFromStyle(attribute(attrs, "style"));
                    if (color.empty())
                        color = cleanColor(attribute(attrs, "foreground"));
                    if (color.empty())
                        color = cleanColor(attribute(attrs, "color"));
                }
                if (color.empty())
                    w.push(name, "", "");
                else
                    w.push(name, "<span style=\"color:" + color + "\">", "</span>");
            }
            // other tags (a, u, small, ...) are dropped, their text stays
            continue;
        }
        if (c == '&') {
            size_t p = i + 1;
            while (p < size && p - i < 12 &&
                   ((src[p] >= 'a' && src[p] <= 'z') || (src[p] >= 'A' && src[p] <= 'Z') ||
                    (src[p] >= '0' && src[p] <= '9') || src[p] == '#'))
                p++;
            if (p < size && src[p] == ';' && p > i + 1) {
                w.text(src + i, p - i + 1);
                i = p + 1;
            } else {
                w.text("&amp;", 5);
                i++;
            }
            continue;
        }
        if (c == '\n' || c == '\r') {
            if (newlineIsBreak) {
                if (c == '\n')
                    w.paragraph();
            } else {
                w.space();
            }
            i++;
            continue;
        }
        if (c == '>') {
            w.text("&gt;", 4);
            i++;
            continue;
        }
        // run of plain text
        size_t p = i;
        while (p < size && src[p] != '<' && src[p] != '&' && src[p] != '\n' && src[p] != '\r' && src[p] != '>')
            p++;
        w.text(src + i, p - i);
        i = p;
    }
    w.finish();
}
