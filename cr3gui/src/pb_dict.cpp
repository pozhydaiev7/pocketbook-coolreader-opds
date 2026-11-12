/*
 * Dictionaries of the CoolReader dictionary window on PocketBook, see pb_dict.h.
 */
#include "pb_dict.h"
#include "pb_stardict.h"
#include "pb_xdxf.h"

#include <inkview.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>

#define PB_DICT_ID_INKVIEW "dic:"
#define PB_DICT_ID_STARDICT "sd:"
#define PB_DICT_ID_XDXF "xd:"

lString32 PbDictSource::label() const
{
    // only the dictionaries that the firmware reader cannot use are marked
    if (type() == PB_DICT_TYPE_INKVIEW)
        return _title;
    lString32 mark(type() == PB_DICT_TYPE_STARDICT ? "StarDict" : "XDXF");
    return _title + Utf8ToUnicode(lString8(" \xC2\xB7 ")) + mark;
}

// lower case and capitalized forms of the word, without repeats
static void pbDictWordForms(const lString32 & w, lString32Collection & forms)
{
    forms.clear();
    lString32 lower = w;
    lower.lowercase();
    lString32 capital = lower;
    if (!capital.empty()) {
        lString32 first = capital.substr(0, 1);
        first.uppercase();
        capital = first + capital.substr(1);
    }
    forms.add(w);
    if (lower != w)
        forms.add(lower);
    if (capital != w && capital != lower)
        forms.add(capital);
}

// ---------------------------------------------------------------------------------------------
// inkview .dic: the firmware keeps one dictionary open at a time

class PbInkviewDict;
static PbInkviewDict * pbOpenInkviewDict = NULL;

class PbInkviewDict : public PbDictSource
{
private:
    lString8 _name;
    static void assign(lString8 & dst, const char * src)
    {
        dst = src != NULL ? lString8(src) : lString8::empty_str;
    }
public:
    PbInkviewDict(const char * name) : _name(name)
    {
        _title = Utf8ToUnicode(_name);
        _id = lString32(PB_DICT_ID_INKVIEW) + _title;
    }
    virtual PbDictType type() const { return PB_DICT_TYPE_INKVIEW; }
    virtual bool isOpen() const { return pbOpenInkviewDict == this; }
    virtual bool open()
    {
        if (isOpen())
            return true;
        if (pbOpenInkviewDict != NULL)
            pbOpenInkviewDict->close();
        int rc = OpenDictionary((char *)_name.c_str());
        CRLog::trace("OpenDictionary(%s) returned %d", _name.c_str(), rc);
        if (rc != 1)
            return false;
        pbOpenInkviewDict = this;
        return true;
    }
    virtual void close()
    {
        if (!isOpen())
            return;
        CloseDictionary();
        pbOpenInkviewDict = NULL;
    }
    virtual int lookup(const lString32 & w, lString8 & word, lString8 & article)
    {
        if (!open())
            return 0;
        char *wd = NULL, *tr = NULL;
        lString8 what = UnicodeToUtf8(w);
        int rc = LookupWord((char *)what.c_str(), &wd, &tr);
        if (rc == 0) {
            lString32 lower = w;
            lower.lowercase();
            what = UnicodeToUtf8(lower);
            rc = LookupWord((char *)what.c_str(), &wd, &tr);
        }
        if (rc != 0) {
            assign(word, wd);
            assign(article, tr);
        }
        return rc;
    }
    virtual bool lookupExact(const lString32 & w, lString8 & word, lString8 & article)
    {
        if (lookup(w, word, article) == 0 || word.empty())
            return false;
        lString32 got = Utf8ToUnicode(word);
        lString32 wanted = w;
        got.lowercase();
        wanted.lowercase();
        return got == wanted;
    }
    virtual bool seek(const lString8 & word)
    {
        char *wd = NULL, *tr = NULL;
        return open() && LookupWord((char *)word.c_str(), &wd, &tr) != 0;
    }
    virtual bool next(lString8 & word, lString8 & article)
    {
        char *wd = NULL, *tr = NULL;
        if (!isOpen() || LookupNext(&wd, &tr) == 0 || wd == NULL)
            return false;
        assign(word, wd);
        assign(article, tr);
        return true;
    }
    virtual bool prev(lString8 & word, lString8 & article)
    {
        char *wd = NULL, *tr = NULL;
        if (!isOpen() || LookupPrevious(&wd, &tr) == 0 || wd == NULL)
            return false;
        assign(word, wd);
        assign(article, tr);
        return true;
    }
};

// ---------------------------------------------------------------------------------------------
// StarDict (D = StarDict) and XDXF (D = XdxfDict): read by CoolReader, any number open at once

template <class D> class PbIndexedDictSource : public PbDictSource
{
private:
    D _dict;
    PbDictType _type;
    int _pos;
    bool _failed;

    // the word at index, with the articles of the same word that follow it
    void fill(int index, lString8 & word, lString8 & article)
    {
        _pos = index;
        const char * w = _dict.wordAt(index);
        word = lString8(w);
        std::string text;
        _dict.articleAt(index, text);
        for (int i = index + 1; i < _dict.wordCount() && strcmp(_dict.wordAt(i), w) == 0; i++) {
            std::string more;
            if (_dict.articleAt(i, more) && !more.empty())
                text += "\n" + more;
        }
        article = lString8(text.c_str());
    }
    // index of the word itself or of the entry its form points to, or -1
    int findWord(const lString32 & w)
    {
        lString32Collection forms;
        pbDictWordForms(w, forms);
        for (int i = 0; i < forms.length(); i++) {
            int index = _dict.findExact(UnicodeToUtf8(forms[i]).c_str());
            if (index >= 0)
                return index;
        }
        for (int i = 0; i < forms.length(); i++) {
            int index = _dict.findSynonym(UnicodeToUtf8(forms[i]).c_str());
            if (index >= 0)
                return index;
        }
        return -1;
    }
public:
    PbIndexedDictSource(PbDictType type) : _type(type), _pos(-1), _failed(false) {}
    bool init(const lString8 & path)
    {
        if (!_dict.readInfo(path.c_str())) {
            CRLog::error("dictionary %s: %s", path.c_str(), _dict.lastError().c_str());
            return false;
        }
        _title = Utf8ToUnicode(lString8(_dict.bookName().c_str()));
        _id = lString32(_type == PB_DICT_TYPE_XDXF ? PB_DICT_ID_XDXF : PB_DICT_ID_STARDICT) + Utf8ToUnicode(path);
        return true;
    }
    virtual PbDictType type() const { return _type; }
    virtual bool isOpen() const { return _dict.isLoaded(); }
    virtual bool open()
    {
        if (_dict.isLoaded())
            return true;
        if (_failed)
            return false;
        struct timeval start, end;
        gettimeofday(&start, NULL);
        if (!_dict.load()) {
            _failed = true;
            CRLog::error("dictionary %s: %s", _dict.ifoPath().c_str(), _dict.lastError().c_str());
            return false;
        }
        gettimeofday(&end, NULL);
        CRLog::info("dictionary %s: %d words loaded in %d ms", _dict.ifoPath().c_str(), _dict.wordCount(),
                    (int)((end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000));
        return true;
    }
    // the index stays loaded: the next lookup does not read it again
    virtual void close() {}
    virtual int lookup(const lString32 & w, lString8 & word, lString8 & article)
    {
        if (!open() || _dict.wordCount() == 0)
            return 0;
        int index = findWord(w);
        if (index >= 0) {
            fill(index, word, article);
            return 2;
        }
        // the nearest word for the word list
        lString32 lower = w;
        lower.lowercase();
        index = _dict.lowerBound(UnicodeToUtf8(lower).c_str());
        if (index >= _dict.wordCount())
            index = _dict.wordCount() - 1;
        fill(index, word, article);
        return 1;
    }
    virtual bool lookupExact(const lString32 & w, lString8 & word, lString8 & article)
    {
        if (!open())
            return false;
        int index = findWord(w);
        if (index < 0)
            return false;
        fill(index, word, article);
        return true;
    }
    virtual bool seek(const lString8 & word)
    {
        if (!open())
            return false;
        int index = _dict.findExact(word.c_str());
        if (index < 0)
            return false;
        _pos = index;
        return true;
    }
    virtual bool next(lString8 & word, lString8 & article)
    {
        if (!isOpen() || _pos < 0 || _pos + 1 >= _dict.wordCount())
            return false;
        // the articles of the same word were shown together
        int index = _pos + 1;
        const char * w = _dict.wordAt(_pos);
        while (index < _dict.wordCount() && strcmp(_dict.wordAt(index), w) == 0)
            index++;
        if (index >= _dict.wordCount())
            return false;
        fill(index, word, article);
        return true;
    }
    virtual bool prev(lString8 & word, lString8 & article)
    {
        if (!isOpen() || _pos <= 0)
            return false;
        int index = _pos - 1;
        // to the first article of that word
        while (index > 0 && strcmp(_dict.wordAt(index - 1), _dict.wordAt(index)) == 0)
            index--;
        fill(index, word, article);
        return true;
    }
};

// ---------------------------------------------------------------------------------------------
// PbDictManager

PbDictManager & PbDictManager::instance()
{
    static PbDictManager manager;
    return manager;
}

static bool pbEndsWith(const char * name, const char * ext)
{
    size_t len = strlen(name);
    size_t extLen = strlen(ext);
    return len > extLen && strcasecmp(name + len - extLen, ext) == 0;
}

void PbDictManager::scanStarDictFolder(const lString8 & folder, int depth, LVPtrVector<PbDictSource, false> & found)
{
    DIR * dir = opendir(folder.c_str());
    if (dir == NULL)
        return;
    struct dirent * entry;
    while ((entry = readdir(dir)) != NULL) {
        if (found.length() >= PB_DICT_STARDICT_MAX) {
            CRLog::error("more than %d StarDict dictionaries, the rest are skipped", PB_DICT_STARDICT_MAX);
            break;
        }
        if (entry->d_name[0] == '.')
            continue;
        lString8 path = folder + "/" + entry->d_name;
        struct stat st;
        if (stat(path.c_str(), &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            // StarDict archives are usually unpacked into a folder of their own
            if (depth > 0)
                scanStarDictFolder(path, depth - 1, found);
        } else if (pbEndsWith(entry->d_name, ".ifo")) {
            PbIndexedDictSource<StarDict> * source = new PbIndexedDictSource<StarDict>(PB_DICT_TYPE_STARDICT);
            if (source->init(path)) {
                found.add(source);
                CRLog::info("StarDict found: %s", path.c_str());
            } else {
                delete source;
            }
        } else if (pbEndsWith(entry->d_name, ".xdxf")) {
            PbIndexedDictSource<XdxfDict> * source = new PbIndexedDictSource<XdxfDict>(PB_DICT_TYPE_XDXF);
            if (source->init(path)) {
                found.add(source);
                CRLog::info("XDXF found: %s", path.c_str());
            } else {
                delete source;
            }
        }
    }
    closedir(dir);
}

void PbDictManager::scan()
{
    if (_scanned)
        return;
    _scanned = true;
    char ** names = EnumDictionaries();
    for (int i = 0; names != NULL && names[i] != NULL; i++)
        _sources.add(new PbInkviewDict(names[i]));
    int dicCount = _sources.length();
    LVPtrVector<PbDictSource, false> found;
    scanStarDictFolder(lString8(USERDICTPATH1), 1, found);
#ifdef USERDICTPATH2
    scanStarDictFolder(lString8(USERDICTPATH2), 1, found);
#endif
    // StarDict and XDXF dictionaries after the .dic ones, by name
    for (int i = 0; i < found.length(); i++)
        for (int j = i + 1; j < found.length(); j++)
            if (found[j]->title().compare(found[i]->title()) < 0) {
                PbDictSource * tmp = found[i];
                found.set(i, found[j]);
                found.set(j, tmp);
            }
    for (int i = 0; i < found.length(); i++)
        _sources.add(found[i]);
    CRLog::info("dictionaries: %d .dic, %d StarDict and XDXF", dicCount, found.length());
}

lString32 PbDictManager::normalizeId(const lString32 & value)
{
    if (value.empty() || value.startsWith(lString32(PB_DICT_ID_INKVIEW)) ||
        value.startsWith(lString32(PB_DICT_ID_STARDICT)) || value.startsWith(lString32(PB_DICT_ID_XDXF)) ||
        value == lString32(PB_DICT_ID_SELECTED))
        return value;
    return lString32(PB_DICT_ID_INKVIEW) + value;
}

PbDictSource * PbDictManager::find(const lString32 & id)
{
    scan();
    lString32 wanted = normalizeId(id);
    for (int i = 0; i < _sources.length(); i++)
        if (_sources[i]->id() == wanted)
            return _sources[i];
    return NULL;
}

void PbDictManager::closeInkview()
{
    if (pbOpenInkviewDict != NULL)
        pbOpenInkviewDict->close();
}
