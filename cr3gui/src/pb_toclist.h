/*
 * Contents of the book as a plain touch list: the "old" contents mode of CoolReader on PocketBook.
 * It replaces the firmware contents window (OpenContents), which crashes on firmware 6.11.
 * Works with every skin: the screens are the touch lists of opdsmenu.h.
 */
#ifndef PB_TOCLIST_H_INCLUDED
#define PB_TOCLIST_H_INCLUDED

#include <crgui.h>
#include <inkview.h>
#include <vector>

// Command ids of the list screens: below the OPDS commands (70000), above the PocketBook ones.
// A chapter row posts PB_TOC_SAFE_CMD_RANGE + page, as the new touch contents does.
enum PbTocListCommands {
    PB_TOC_LIST_CMD_FIRST = 65000,
    PB_TOC_LIST_CMD_CLOSE = PB_TOC_LIST_CMD_FIRST,
    PB_TOC_LIST_CMD_PREV,
    PB_TOC_LIST_CMD_NEXT,
    PB_TOC_LIST_CMD_FILLER, // empty row: the same screen again
    PB_TOC_LIST_CMD_LAST = 65009
};

class PbTocList
{
public:
    static PbTocList & instance();
    static bool isTocListCommand(int command)
    {
        return command >= PB_TOC_LIST_CMD_FIRST && command <= PB_TOC_LIST_CMD_LAST;
    }
    /// shows the contents on the screen with the chapter of the current page (1-based)
    void open(CRGUIWindowManager * wm, const tocentry * toc, int length, int currentPage);
    /// handles PB_TOC_LIST_CMD_FIRST..PB_TOC_LIST_CMD_LAST; true when consumed
    bool onCommand(CRGUIWindowManager * wm, int command, int params);
private:
    struct Entry {
        int level;
        int page;
        lString32 text;
    };
    std::vector<Entry> _entries;
    int _current;  // entry of the current page
    int _screen;
    int _perScreen;
    PbTocList() : _current(0), _screen(0), _perScreen(1) {}
    void show(CRGUIWindowManager * wm);
};

#endif // PB_TOCLIST_H_INCLUDED
