/*
 * XDXF dictionary reader for CoolReader on PocketBook.
 *
 * Reads a plain (not compressed) .xdxf file of the XDXF 1 (visual) or XDXF 2 (logical) format:
 * the articles <ar>...</ar> with one or more keys <k>...</k> each. The index (keys and article
 * positions) is built by one pass over the file on the first lookup and stays in memory; the
 * articles are read from the file when shown.
 * Plain C++ without crengine types, so it can be checked on a desktop. The method names follow
 * StarDict (pb_stardict.h): pb_dict.cpp serves both through the same code.
 */
#ifndef PB_XDXF_H_INCLUDED
#define PB_XDXF_H_INCLUDED

#include <stdio.h>
#include <string>
#include <vector>

class XdxfDict
{
public:
    XdxfDict();
    ~XdxfDict();
    /// reads the dictionary name from the file header; false when it is not an XDXF file
    bool readInfo(const char * path);
    const std::string & bookName() const { return _bookName; }
    /// the .xdxf file (named as in StarDict, where it is the .ifo file)
    const std::string & ifoPath() const { return _path; }
    /// reads the keys of the whole file into the index and keeps the file open
    bool load();
    void unload();
    bool isLoaded() const { return _loaded; }
    int wordCount() const { return (int)_entries.size(); }
    /// index of the first key equal to the word, or -1
    int findExact(const char * word) const;
    /// index of the first key not less than the word (StarDict order), may be wordCount()
    int lowerBound(const char * word) const;
    /// XDXF has no word form list: always -1
    int findSynonym(const char * word) { (void)word; return -1; }
    const char * wordAt(int index) const;
    /// article as article markup (see StarDict::articleAt)
    bool articleAt(int index, std::string & article);
    const std::string & lastError() const { return _error; }
private:
    struct Entry {
        unsigned key;              // start of the key in _keys
        long long offset;          // article text: after <ar ...>, up to </ar>
        unsigned size;
    };
    std::string _path;
    std::string _bookName;
    std::string _error;
    bool _loaded;
    FILE * _file;
    std::string _keys;             // keys, each ends with a zero
    std::vector<Entry> _entries;   // sorted by key in the StarDict order

    void addArticle(const char * text, size_t size, long long offset);
    void addKey(const std::string & key, long long offset, unsigned size);
};

/// XML text to plain UTF-8: entities decoded, tags dropped, spaces collapsed, trimmed.
/// skipOpt: the text of <opt>...</opt> (optional part of an XDXF key) is dropped too.
std::string xdxfPlainText(const char * text, size_t size, bool skipOpt);

#endif // PB_XDXF_H_INCLUDED
