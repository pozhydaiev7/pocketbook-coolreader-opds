// OPDS catalog browser for the CoolReader PocketBook port.
// Shows catalog feeds as full-screen touch lists (two lines per book, own paging),
// downloads the chosen book with libcurl and opens it in the reader.
#ifndef OPDSBROWSER_H
#define OPDSBROWSER_H

#include <crgui.h>
#include <string>
#include <vector>
#include "opdsfeed.h"

#define PROP_OPDS_URL          "cr3.pocketbook.opds.url"
#define PROP_OPDS_DOWNLOAD_DIR "cr3.pocketbook.opds.dir"
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
    OPDS_CMD_ENTRY   = 71000, // + entry index
    OPDS_CMD_FORMAT  = 76000, // + format index of the current book
    OPDS_CMD_RELATED = 77000, // + related link index of the current book
    OPDS_CMD_LAST    = 79999
};

class OpdsBrowser
{
public:
    static OpdsBrowser & instance();

    // Opens the browser: resumes the last visited page or loads the catalog root.
    void open(CRGUIWindowManager * wm);
    // Opens the on-screen keyboard to edit the catalog address.
    void editUrl(CRGUIWindowManager * wm);
    // Handles OPDS_CMD_FIRST..OPDS_CMD_LAST. Returns true when the command was consumed.
    bool onCommand(CRGUIWindowManager * wm, int command, int params);

    static bool isOpdsCommand(int command) { return command >= OPDS_CMD_FIRST && command <= OPDS_CMD_LAST; }

    // Keyboard callbacks write here before posting the *_APPLY command.
    char keyBuffer[1024];

private:
    OpdsBrowser() : _bookIndex(-1), _rows(0) { keyBuffer[0] = 0; }

    std::vector<opds::Feed> _history; // back stack, last element is the visible page
    std::vector<int> _screens;        // current screen of each history level
    int _bookIndex;                   // entry index shown in the book menu, -1 for the list
    int _rows;                        // rows per screen of the list (entries + search row + paging row)

    std::string rootUrl();
    bool fetch(const std::string & url, opds::Feed & feed);
    bool load(const std::string & url, bool push);
    bool loadMore();
    void showList(CRGUIWindowManager * wm);
    void showService(CRGUIWindowManager * wm);
    void showBook(CRGUIWindowManager * wm, int index);
    void showInfo();
    void download(CRGUIWindowManager * wm, int formatIndex);
    const opds::Entry * currentBook();
    bool hasSearchRow() const;
    int firstEntryOfScreen(int screen) const;
    int entriesOnScreen(int screen) const;
};

#endif // OPDSBROWSER_H
