/*
 * Dictionaries of the CoolReader dictionary window on PocketBook: the firmware (inkview) .dic
 * dictionaries and StarDict dictionaries behind one interface, in one list.
 */
#ifndef PB_DICT_H_INCLUDED
#define PB_DICT_H_INCLUDED

#include <crengine.h>

class StarDict;

enum PbDictType {
    PB_DICT_TYPE_INKVIEW,  // PocketBook .dic, read by the firmware library
    PB_DICT_TYPE_STARDICT  // StarDict, read by CoolReader
};

/// id of the "Selected dictionaries" choice in the settings: the checked dictionaries together
#define PB_DICT_ID_SELECTED "all:"
/// how many dictionaries can be checked for the search in several dictionaries
#define PB_DICT_SELECTED_MAX 5
/// StarDict dictionaries looked for at most, so that a folder with thousands does not stall the list
#define PB_DICT_STARDICT_MAX 200

/// One dictionary of any type
class PbDictSource
{
public:
    virtual ~PbDictSource() {}
    virtual PbDictType type() const = 0;
    /// stored in the settings: "dic:<name>" or "sd:<.ifo path in the dictionaries folder>"
    const lString16 & id() const { return _id; }
    /// dictionary name
    const lString16 & title() const { return _title; }
    /// name with the type mark, for the lists of dictionaries
    lString16 label() const;
    /// makes lookups possible; an inkview dictionary closes the other inkview dictionary
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    /// 0: nothing; 1: the nearest word (word list); 2: the word itself
    virtual int lookup(const lString16 & w, lString8 & word, lString8 & article) = 0;
    /// the word itself or its form, any letter case; for the search in several dictionaries
    virtual bool lookupExact(const lString16 & w, lString8 & word, lString8 & article) = 0;
    /// moves the word list position to the word, as returned by lookup/next/prev
    virtual bool seek(const lString8 & word) = 0;
    virtual bool next(lString8 & word, lString8 & article) = 0;
    virtual bool prev(lString8 & word, lString8 & article) = 0;
protected:
    lString16 _id;
    lString16 _title;
};

/// All dictionaries found on the device. Loaded StarDict indexes stay in memory until exit.
class PbDictManager
{
public:
    static PbDictManager & instance();
    /// looks for dictionaries once per run (dictionaries are copied over USB, CoolReader restarts)
    void scan();
    int count() { scan(); return _sources.length(); }
    PbDictSource * get(int index) { scan(); return _sources[index]; }
    /// by id, or by a plain .dic name saved by older versions; NULL when not found
    PbDictSource * find(const lString16 & id);
    /// closes the open inkview dictionary
    void closeInkview();
    /// the id of a value saved by older versions (plain .dic name)
    static lString16 normalizeId(const lString16 & value);
private:
    PbDictManager() : _scanned(false) {}
    void scanStarDictFolder(const lString8 & folder, int depth, LVPtrVector<PbDictSource, false> & found);
    bool _scanned;
    LVPtrVector<PbDictSource> _sources;
};

#endif // PB_DICT_H_INCLUDED
