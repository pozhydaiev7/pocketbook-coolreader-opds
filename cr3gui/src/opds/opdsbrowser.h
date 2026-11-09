// OPDS catalog browser for the CoolReader PocketBook port.
// Shows catalog feeds as full-screen touch lists (two lines per book, own paging),
// downloads the chosen book with libcurl and opens it in the reader.
#ifndef OPDSBROWSER_H
#define OPDSBROWSER_H

#include <crgui.h>
#include <string>
#include <vector>
#include "opdsfeed.h"

// address of the single catalog of the earlier versions: read once, when the list is made
#define PROP_OPDS_URL          "cr3.pocketbook.opds.url"
#define PROP_OPDS_DOWNLOAD_DIR "cr3.pocketbook.opds.dir"
// Catalogs are kept by their number in the list, not by the address:
// cr3.pocketbook.opds.catalog.<N>.name / .url. N = 0 is Flibusta, always the first catalog: it
// cannot be renamed or deleted, its address can be changed (a mirror). N = 1, 2, ... are the
// catalogs added by the user; an empty address ends the list.
#define PROP_OPDS_CATALOG      "cr3.pocketbook.opds.catalog."
// number in the list (1 = Flibusta) of the catalog the touch menu icon opens;
// 0: the icon shows the list of catalogs
#define PROP_OPDS_DEFAULT      "cr3.pocketbook.opds.default"
#define OPDS_DEFAULT_NAME      "Flibusta"
#define OPDS_MAX_CATALOGS      20
#define OPDS_DEFAULT_URL       "https://flibusta.is/opds"
#define OPDS_DEFAULT_DIR       "/mnt/ext1/Books/Flibusta"

// Command ids posted by the browser menus. They must stay below PB_TOC_SAFE_CMD_RANGE (90000).
enum OpdsCommands {
    OPDS_CMD_FIRST = 70000,
    OPDS_CMD_CLOSE = OPDS_CMD_FIRST,
    OPDS_CMD_BACK,          // one catalog level up
    OPDS_CMD_HOME,
    OPDS_CMD_SEARCH,
    OPDS_CMD_SEARCH_APPLY,
    OPDS_CMD_NEXT,          // next screen of the list (loads the next catalog page when needed)
    OPDS_CMD_SET_URL,
    OPDS_CMD_SET_URL_APPLY,
    OPDS_CMD_INFO,
    OPDS_CMD_SHOW_LIST,
    OPDS_CMD_SHOW_BOOK,
    OPDS_CMD_PREV,          // previous screen of the list, or one level up on the first screen
    OPDS_CMD_SERVICE,       // service menu: back, search, start page, address, close
    OPDS_CMD_CATALOGS,      // list of the catalogs: a tap opens one
    OPDS_CMD_CATALOGS_EDIT, // list of the catalogs: a tap opens the catalog card
    OPDS_CMD_CATALOGS_SHOW, // the list again, in the mode it was shown last
    OPDS_CMD_CATALOG_ADD,   // asks for the address of a new catalog
    OPDS_CMD_CATALOG_ADD_URL_APPLY,
    OPDS_CMD_CATALOG_ADD_NAME_APPLY,
    OPDS_CMD_CATALOG_CARD,  // card of the catalog being edited
    OPDS_CMD_CATALOG_NAME,
    OPDS_CMD_CATALOG_NAME_APPLY,
    OPDS_CMD_CATALOG_URL,
    OPDS_CMD_CATALOG_URL_APPLY,
    OPDS_CMD_CATALOG_DEFAULT, // makes the catalog the default one, or no longer the default
    OPDS_CMD_CATALOG_DELETE,  // asks to confirm
    OPDS_CMD_CATALOG_DELETE_APPLY,
    OPDS_CMD_ENTRY   = 71000, // + entry index
    OPDS_CMD_FORMAT  = 76000, // + format index of the current book
    OPDS_CMD_RELATED = 77000, // + related link index of the current book
    OPDS_CMD_CATALOG_OPEN = 79000, // + catalog index
    OPDS_CMD_CATALOG_EDIT = 79100, // + catalog index: its card
    OPDS_CMD_LAST    = 79999
};

// One catalog of the list
struct OpdsCatalog {
    lString16 name;
    lString16 url;
};

class OpdsBrowser
{
public:
    static OpdsBrowser & instance();

    // The touch menu icon: opens the default catalog (resumes the last visited page of it),
    // or the list of catalogs when none is the default.
    void open(CRGUIWindowManager * wm);
    // The book menu: the list of catalogs.
    void openCatalogs(CRGUIWindowManager * wm);
    // Opens the on-screen keyboard to edit the address of the open catalog.
    void editUrl(CRGUIWindowManager * wm);
    // Handles OPDS_CMD_FIRST..OPDS_CMD_LAST. Returns true when the command was consumed.
    bool onCommand(CRGUIWindowManager * wm, int command, int params);

    static bool isOpdsCommand(int command) { return command >= OPDS_CMD_FIRST && command <= OPDS_CMD_LAST; }

    // Keyboard callbacks write here before posting the *_APPLY command.
    char keyBuffer[1024];

private:
    OpdsBrowser() : _bookIndex(-1), _rows(0), _catalog(-1), _editCatalog(-1), _catalogsEdit(false), _catalogRows(0)
    {
        keyBuffer[0] = 0;
    }

    std::vector<opds::Feed> _history; // back stack, last element is the visible page
    std::vector<int> _screens;        // current screen of each history level
    int _bookIndex;                   // entry index shown in the book menu, -1 for the list
    int _rows;                        // rows per screen of the list (back row + search row + entries + paging row)
    std::vector<OpdsCatalog> _catalogs;
    int _catalog;                     // index of the open catalog, -1 when none
    int _editCatalog;                 // index of the catalog of the card
    bool _catalogsEdit;               // the list of catalogs was shown to edit them
    lString16 _newUrl;                // address of the catalog being added
    int _catalogRows;                 // rows of the screen of the catalog list

    std::string rootUrl();
    // reads the catalogs from the settings: Flibusta, then the catalogs of the user; the first
    // time, a catalog address of the earlier versions (cr3.pocketbook.opds.url) that is not a
    // Flibusta address becomes the second catalog and the default one
    void loadCatalogs();
    void saveCatalogs();
    int defaultCatalog();             // index, -1 when none
    void setDefaultCatalog(int index);
    // fromList: when the catalog does not load, the list of catalogs is shown again
    void openCatalog(CRGUIWindowManager * wm, int index, bool fromList);
    // catalogs that fit on the screen of the list, Flibusta included
    int maxCatalogs();
    void showCatalogs(CRGUIWindowManager * wm, bool edit);
    void showCard(CRGUIWindowManager * wm);
    void showDeleteQuestion(CRGUIWindowManager * wm);
    void editText(const char * title, const lString16 & value, void (*handler)(char *));
    bool fetch(const std::string & url, opds::Feed & feed);
    bool load(const std::string & url, bool push);
    bool loadMore();
    void showList(CRGUIWindowManager * wm);
    void showService(CRGUIWindowManager * wm);
    void showBook(CRGUIWindowManager * wm, int index);
    void showInfo(CRGUIWindowManager * wm);
    void download(CRGUIWindowManager * wm, int formatIndex);
    const opds::Entry * currentBook();
    bool hasSearchRow() const;
    int firstEntryOfScreen(int screen) const;
    int entriesOnScreen(int screen) const;
};

#endif // OPDSBROWSER_H
