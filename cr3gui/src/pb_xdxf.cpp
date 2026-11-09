/*
 * XDXF dictionary reader, see pb_xdxf.h.
 * No std::string operator+ here: the binary would export its copy of it to libinkview.
 */
#include "pb_xdxf.h"
#include "pb_stardict.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <algorithm>

#define XDXF_HEADER_SIZE (64 * 1024)
#define XDXF_CHUNK_SIZE (256 * 1024)

namespace {

void appendUtf8(std::string & out, unsigned long code)
{
    if (code == 0 || code > 0x10FFFF)
        return;
    if (code < 0x80) {
        out += (char)code;
    } else if (code < 0x800) {
        out += (char)(0xC0 | (code >> 6));
        out += (char)(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += (char)(0xE0 | (code >> 12));
        out += (char)(0x80 | ((code >> 6) & 0x3F));
        out += (char)(0x80 | (code & 0x3F));
    } else {
        out += (char)(0xF0 | (code >> 18));
        out += (char)(0x80 | ((code >> 12) & 0x3F));
        out += (char)(0x80 | ((code >> 6) & 0x3F));
        out += (char)(0x80 | (code & 0x3F));
    }
}

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// "<name" at pos is the whole tag name: ">", "/" or a space follows
bool tagAt(const char * text, size_t size, size_t pos, const char * name)
{
    size_t len = strlen(name);
    if (pos + 1 + len >= size || text[pos] != '<' || strncmp(text + pos + 1, name, len) != 0)
        return false;
    char c = text[pos + 1 + len];
    return c == '>' || c == '/' || isSpace(c);
}

// position of the next "<name" tag at or after from, or npos
size_t findTag(const std::string & s, size_t from, const char * name)
{
    std::string open("<");
    open.append(name);
    size_t pos = from;
    while ((pos = s.find(open, pos)) != std::string::npos) {
        if (tagAt(s.data(), s.size(), pos, name))
            return pos;
        pos++;
    }
    return std::string::npos;
}

// text of the first <name>...</name> of the header, as plain text
std::string headerTag(const std::string & header, const char * name)
{
    size_t start = findTag(header, 0, name);
    if (start == std::string::npos)
        return std::string();
    size_t gt = header.find('>', start);
    if (gt == std::string::npos || header[gt - 1] == '/')
        return std::string();
    std::string close("</");
    close.append(name);
    size_t end = header.find(close, gt + 1);
    if (end == std::string::npos)
        return std::string();
    return xdxfPlainText(header.data() + gt + 1, end - gt - 1, false);
}

} // namespace

std::string xdxfPlainText(const char * text, size_t size, bool skipOpt)
{
    std::string out;
    bool space = false;
    size_t i = 0;
    while (i < size) {
        char c = text[i];
        if (c == '<') {
            if (skipOpt && tagAt(text, size, i, "opt")) {
                // optional part of a key: up to </opt>
                const char * end = NULL;
                for (size_t p = i; p + 6 <= size; p++)
                    if (strncmp(text + p, "</opt>", 6) == 0) {
                        end = text + p + 6;
                        break;
                    }
                i = end != NULL ? (size_t)(end - text) : size;
                continue;
            }
            while (i < size && text[i] != '>')
                i++;
            i++;
            continue;
        }
        if (isSpace(c)) {
            space = !out.empty();
            i++;
            continue;
        }
        if (space) {
            out += ' ';
            space = false;
        }
        if (c == '&') {
            size_t semi = i + 1;
            while (semi < size && semi - i < 12 && text[semi] != ';')
                semi++;
            if (semi < size && text[semi] == ';') {
                std::string name(text + i + 1, semi - i - 1);
                unsigned long code = 0;
                if (name == "amp") code = '&';
                else if (name == "lt") code = '<';
                else if (name == "gt") code = '>';
                else if (name == "quot") code = '"';
                else if (name == "apos") code = '\'';
                else if (name.size() > 1 && name[0] == '#')
                    code = (name[1] == 'x' || name[1] == 'X') ? strtoul(name.c_str() + 2, NULL, 16)
                                                              : strtoul(name.c_str() + 1, NULL, 10);
                if (code != 0) {
                    appendUtf8(out, code);
                    i = semi + 1;
                    continue;
                }
            }
        }
        out += c;
        i++;
    }
    return out;
}

XdxfDict::XdxfDict() : _loaded(false), _file(NULL)
{
}

XdxfDict::~XdxfDict()
{
    unload();
}

bool XdxfDict::readInfo(const char * path)
{
    _path = path;
    FILE * f = fopen(path, "rb");
    if (f == NULL) {
        _error = "cannot open the file";
        return false;
    }
    std::string header(XDXF_HEADER_SIZE, '\0');
    size_t n = fread(&header[0], 1, header.size(), f);
    fclose(f);
    header.resize(n);
    if (findTag(header, 0, "xdxf") == std::string::npos) {
        _error = "no <xdxf> element at the start of the file";
        return false;
    }
    size_t firstArticle = findTag(header, 0, "ar");
    if (firstArticle != std::string::npos)
        header.resize(firstArticle);
    _bookName = headerTag(header, "full_name");      // XDXF 1
    if (_bookName.empty())
        _bookName = headerTag(header, "full_title"); // XDXF 2
    if (_bookName.empty())
        _bookName = headerTag(header, "title");
    if (_bookName.empty()) {
        // the file name, or the folder name for the usual "dict.xdxf"
        std::string p(path);
        size_t slash = p.rfind('/');
        std::string name = p.substr(slash == std::string::npos ? 0 : slash + 1);
        size_t dot = name.rfind('.');
        if (dot != std::string::npos)
            name.resize(dot);
        if (strcasecmp(name.c_str(), "dict") == 0 && slash != std::string::npos && slash > 0) {
            size_t parent = p.rfind('/', slash - 1);
            name = p.substr(parent == std::string::npos ? 0 : parent + 1,
                            slash - (parent == std::string::npos ? 0 : parent + 1));
        }
        _bookName = name;
    }
    return true;
}

void XdxfDict::addKey(const std::string & key, long long offset, unsigned size)
{
    if (key.empty())
        return;
    Entry e;
    e.key = (unsigned)_keys.size();
    e.offset = offset;
    e.size = size;
    _keys.append(key);
    _keys += '\0';
    _entries.push_back(e);
}

void XdxfDict::addArticle(const char * text, size_t size, long long offset)
{
    // every <k> of the article is a key of it; a key with <opt> also gives the key without it
    size_t pos = 0;
    while (pos < size) {
        const char * open = NULL;
        for (size_t p = pos; p < size; p++)
            if (text[p] == '<' && tagAt(text, size, p, "k")) {
                open = text + p;
                break;
            }
        if (open == NULL)
            break;
        const char * gt = (const char *)memchr(open, '>', text + size - open);
        if (gt == NULL)
            break;
        const char * close = NULL;
        for (const char * p = gt + 1; p + 4 <= text + size; p++)
            if (strncmp(p, "</k>", 4) == 0) {
                close = p;
                break;
            }
        if (close == NULL)
            break;
        std::string full = xdxfPlainText(gt + 1, close - gt - 1, false);
        std::string shortKey = xdxfPlainText(gt + 1, close - gt - 1, true);
        addKey(full, offset, (unsigned)size);
        if (shortKey != full)
            addKey(shortKey, offset, (unsigned)size);
        pos = close + 4 - text;
    }
}

namespace {

struct EntryOrder
{
    const char * keys;
    template <class E> bool operator()(const E & a, const E & b) const
    {
        int c = sdCompare(keys + a.key, keys + b.key);
        if (c != 0)
            return c < 0;
        return a.offset < b.offset; // articles of the same word in the file order
    }
};

} // namespace

bool XdxfDict::load()
{
    if (_loaded)
        return true;
    _keys.clear();
    _entries.clear();
    _file = fopen(_path.c_str(), "rb");
    if (_file == NULL) {
        _error = "cannot open the file";
        return false;
    }
    std::string buf;
    long long bufStart = 0;  // file position of buf[0]
    size_t pos = 0;
    std::string chunk(XDXF_CHUNK_SIZE, '\0');
    bool eof = false;
    for (;;) {
        // the next whole article in the buffer
        size_t start = findTag(buf, pos, "ar");
        size_t gt = start == std::string::npos ? std::string::npos : buf.find('>', start);
        size_t close = gt == std::string::npos ? std::string::npos : buf.find("</ar>", gt + 1);
        if (close != std::string::npos) {
            addArticle(buf.data() + gt + 1, close - gt - 1, bufStart + (long long)(gt + 1));
            pos = close + 5;
            continue;
        }
        if (eof)
            break;
        // keep an article that is not complete yet, or the end that may hold a part of "<ar"
        size_t keep = start;
        if (keep == std::string::npos) {
            keep = buf.size() > 3 ? buf.size() - 3 : 0;
            if (keep < pos)
                keep = pos;
        }
        buf.erase(0, keep);
        bufStart += (long long)keep;
        pos = 0;
        size_t n = fread(&chunk[0], 1, chunk.size(), _file);
        if (n == 0)
            eof = true;
        else
            buf.append(chunk.data(), n);
    }
    EntryOrder order;
    order.keys = _keys.data();
    std::sort(_entries.begin(), _entries.end(), order);
    _loaded = true;
    return true;
}

void XdxfDict::unload()
{
    if (_file != NULL)
        fclose(_file);
    _file = NULL;
    _loaded = false;
    _keys.clear();
    _entries.clear();
}

const char * XdxfDict::wordAt(int index) const
{
    if (index < 0 || index >= (int)_entries.size())
        return "";
    return _keys.data() + _entries[index].key;
}

int XdxfDict::lowerBound(const char * word) const
{
    int lo = 0, hi = (int)_entries.size();
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (sdCompare(_keys.data() + _entries[mid].key, word) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

int XdxfDict::findExact(const char * word) const
{
    int i = lowerBound(word);
    if (i < (int)_entries.size() && strcmp(_keys.data() + _entries[i].key, word) == 0)
        return i;
    return -1;
}

bool XdxfDict::articleAt(int index, std::string & article)
{
    article.clear();
    if (!_loaded || _file == NULL || index < 0 || index >= (int)_entries.size())
        return false;
    const Entry & e = _entries[index];
    std::string raw(e.size, '\0');
    if (fseek(_file, (long)e.offset, SEEK_SET) != 0 || (e.size > 0 && fread(&raw[0], 1, e.size, _file) != e.size)) {
        _error = "cannot read the article";
        return false;
    }
    sdMarkupToArticle(raw.data(), raw.size(), 'x', article);
    return true;
}
