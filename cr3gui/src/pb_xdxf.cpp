/*
 * XDXF dictionary reader, see pb_xdxf.h.
 * No std::string operator+ here: the binary would export its copy of it to libinkview.
 */
#include "pb_xdxf.h"
#include "pb_stardict.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <algorithm>

#define XDXF_HEADER_SIZE (64 * 1024)
#define XDXF_CHUNK_SIZE (256 * 1024)
// The index of a dictionary is kept in a file when the dictionary is not smaller than this:
// a small one is read faster than its index would be
#define XDXF_INDEX_CACHE_MIN_SIZE (256 * 1024)
// first line of a kept index: "<magic> <dictionary size> <dictionary time> <entries> <bytes of the keys>",
// second line: the path of the dictionary; then the keys and the entries (XdxfDiskEntry each)
#define XDXF_INDEX_MAGIC "cr3xdxf-1"

namespace {

std::string xdxfIndexCacheDir;

// an entry of the index as it is kept in the file
struct XdxfDiskEntry {
    unsigned key;
    unsigned size;
    long long offset;
};

// the file of the kept index of a dictionary: named by a hash of its path
std::string xdxfIndexCacheFile(const std::string & path)
{
    if (xdxfIndexCacheDir.empty())
        return std::string();
    unsigned hash = 2166136261u; // FNV-1a
    for (size_t i = 0; i < path.size(); i++) {
        hash ^= (unsigned char)path[i];
        hash *= 16777619u;
    }
    char name[40];
    snprintf(name, sizeof(name), "/xdxf-%08x.idx", hash);
    std::string file(xdxfIndexCacheDir);
    file.append(name);
    return file;
}

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

XdxfDict::XdxfDict() : _loaded(false), _fromCache(false), _file(NULL)
{
}

void XdxfDict::setIndexCacheDir(const char * dir)
{
    xdxfIndexCacheDir = dir != NULL ? dir : "";
}

// Reads the kept index when it was made for this dictionary file as it is now. Anything that
// does not fit (another size or time of the dictionary, a file cut short, a key position outside
// the keys) leaves the index empty: the dictionary is then read the usual way.
bool XdxfDict::readIndexCache(const std::string & file, long long dictSize, long long dictTime)
{
    FILE * f = fopen(file.c_str(), "rb");
    if (f == NULL)
        return false;
    bool ok = false;
    char line[2048];
    char magic[32];
    long long size = 0, time = 0;
    unsigned long entries = 0, keyBytes = 0;
    if (fgets(line, sizeof(line), f) != NULL
            && sscanf(line, "%31s %lld %lld %lu %lu", magic, &size, &time, &entries, &keyBytes) == 5
            && strcmp(magic, XDXF_INDEX_MAGIC) == 0 && size == dictSize && time == dictTime
            && entries > 0 && keyBytes > 0 && fgets(line, sizeof(line), f) != NULL) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = 0;
        if (_path == line) {
            _keys.resize(keyBytes);
            std::vector<XdxfDiskEntry> disk(entries);
            if (fread(&_keys[0], 1, keyBytes, f) == keyBytes
                    && fread(&disk[0], sizeof(XdxfDiskEntry), entries, f) == entries
                    && fgetc(f) == EOF && _keys[keyBytes - 1] == '\0') {
                ok = true;
                _entries.resize(entries);
                for (unsigned long i = 0; i < entries; i++) {
                    if (disk[i].key >= keyBytes || disk[i].offset < 0
                            || disk[i].offset + (long long)disk[i].size > dictSize) {
                        ok = false;
                        break;
                    }
                    _entries[i].key = disk[i].key;
                    _entries[i].size = disk[i].size;
                    _entries[i].offset = disk[i].offset;
                }
            }
        }
    }
    fclose(f);
    if (!ok) {
        _keys.clear();
        _entries.clear();
    }
    return ok;
}

// Keeps the index: written to a temporary file that takes the place of the old one when it is
// whole. A failure is not an error of the dictionary: the next run reads the file again.
void XdxfDict::writeIndexCache(const std::string & file, long long dictSize, long long dictTime) const
{
    if (_entries.empty() || _keys.empty() || _path.find('\n') != std::string::npos)
        return;
    std::string tmp(file);
    tmp.append(".tmp");
    FILE * f = fopen(tmp.c_str(), "wb");
    if (f == NULL)
        return;
    std::vector<XdxfDiskEntry> disk(_entries.size());
    for (size_t i = 0; i < _entries.size(); i++) {
        disk[i].key = _entries[i].key;
        disk[i].size = _entries[i].size;
        disk[i].offset = _entries[i].offset;
    }
    bool ok = fprintf(f, "%s %lld %lld %lu %lu\n%s\n", XDXF_INDEX_MAGIC, dictSize, dictTime,
                      (unsigned long)_entries.size(), (unsigned long)_keys.size(), _path.c_str()) > 0
            && fwrite(_keys.data(), 1, _keys.size(), f) == _keys.size()
            && fwrite(&disk[0], sizeof(XdxfDiskEntry), disk.size(), f) == disk.size();
    if (fclose(f) != 0)
        ok = false;
    if (!ok || rename(tmp.c_str(), file.c_str()) != 0)
        remove(tmp.c_str());
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
    _fromCache = false;
    _file = fopen(_path.c_str(), "rb");
    if (_file == NULL) {
        _error = "cannot open the file";
        return false;
    }
    // the index kept by an earlier run, for a dictionary large enough to be worth it
    struct stat st;
    std::string cacheFile;
    if (stat(_path.c_str(), &st) == 0 && (long long)st.st_size >= XDXF_INDEX_CACHE_MIN_SIZE)
        cacheFile = xdxfIndexCacheFile(_path);
    if (!cacheFile.empty() && readIndexCache(cacheFile, (long long)st.st_size, (long long)st.st_mtime)) {
        _fromCache = true;
        _loaded = true;
        return true;
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
    if (!cacheFile.empty())
        writeIndexCache(cacheFile, (long long)st.st_size, (long long)st.st_mtime);
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
