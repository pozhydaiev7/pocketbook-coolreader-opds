/*
 * StarDict dictionary reader for CoolReader on PocketBook.
 *
 * Reads .ifo, .idx or .idx.gz, optional .syn, and .dict or .dict.dz (dictzip).
 * Plain C++ with zlib only, no crengine types, so it can be checked on a desktop.
 */
#ifndef PB_STARDICT_H_INCLUDED
#define PB_STARDICT_H_INCLUDED

#include <stdio.h>
#include <string>
#include <vector>

/// Random access to a .dict file, plain or dictzip (.dict.dz)
class SdDictData
{
public:
    SdDictData();
    ~SdDictData();
    bool open(const char * path);
    void close();
    /// reads size bytes from the unpacked position start
    bool read(unsigned long long start, unsigned size, std::string & out);
private:
    FILE * _file;
    bool _compressed;
    unsigned _chunkLength;
    std::vector<unsigned> _chunkSizes;
    std::vector<long long> _chunkOffsets;
    long long _cachedChunk;
    std::string _cache;
    bool loadChunk(unsigned n);
};

/// One StarDict dictionary. info() reads only the .ifo; the index is loaded on the first lookup.
class StarDict
{
public:
    StarDict();
    ~StarDict();
    /// reads the .ifo file; false when it is not a StarDict dictionary
    bool readInfo(const char * ifoPath);
    const std::string & bookName() const { return _bookName; }
    const std::string & ifoPath() const { return _ifoPath; }
    /// loads .idx/.idx.gz, .syn and opens the data file; keeps them until unload()
    bool load();
    void unload();
    bool isLoaded() const { return _loaded; }
    int wordCount() const { return (int)_entries.size(); }
    /// index of the first entry equal to the word (StarDict order), or -1
    int findExact(const char * word) const;
    /// index of the first entry not less than the word, may be wordCount()
    int lowerBound(const char * word) const;
    /// entry index the synonym (word form) points to, or -1
    int findSynonym(const char * word);
    const char * wordAt(int index) const;
    /// article of the entry as article markup: UTF-8 text, '\n' between paragraphs,
    /// inline <strong>, <emphasis>, <sup>, <sub>, <span style="color:..."> only
    bool articleAt(int index, std::string & article);
    /// last error text for the log
    const std::string & lastError() const { return _error; }
private:
    std::string _ifoPath;
    std::string _basePath; // ifo path without ".ifo"
    std::string _bookName;
    std::string _sameTypeSequence;
    unsigned _wordCount;
    unsigned _synWordCount;
    int _offsetBits;
    bool _loaded;
    bool _synLoaded;
    std::string _error;

    std::string _idx;                 // whole index file
    std::vector<unsigned> _entries;   // start of every index entry in _idx
    std::string _syn;                 // whole synonym file
    std::vector<unsigned> _synEntries;
    SdDictData _data;

    bool loadSyn();
    void entryLocation(int index, unsigned long long & offset, unsigned & size) const;
    void convertPart(char type, const char * data, size_t size, std::string & out);
};

/// StarDict order: ASCII case-insensitive, then byte order
int sdCompare(const char * s1, const char * s2);

/// HTML, Pango or XDXF markup to article markup (see StarDict::articleAt)
void sdMarkupToArticle(const char * src, size_t size, char type, std::string & out);

#endif // PB_STARDICT_H_INCLUDED
