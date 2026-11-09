/*
 * Reading statistics screens of CoolReader on PocketBook and the link between the book window
 * and the statistics log (pb_stats.h).
 */
#ifndef PB_STATSVIEW_H_INCLUDED
#define PB_STATSVIEW_H_INCLUDED

#include <crgui.h>
#include <lvdocview.h>
#include <string>
#include <vector>
#include "pb_stats.h"

/// file name of the log in the CoolReader state folder
#define PB_STATS_FILE_NAME "cr3stats.txt"

// Command ids posted by the statistics screens. Below PB_TOC_SAFE_CMD_RANGE (90000), after the
// OPDS commands (70000..79999).
enum PbStatsCommands {
    PB_STATS_CMD_FIRST = 80000,
    PB_STATS_CMD_MENU = PB_STATS_CMD_FIRST, // list of the statistics
    PB_STATS_CMD_CLOSE,
    PB_STATS_CMD_THIS_BOOK,
    PB_STATS_CMD_TODAY,
    PB_STATS_CMD_WEEK,
    PB_STATS_CMD_MONTH,
    PB_STATS_CMD_YEAR,
    PB_STATS_CMD_ALL_TIME,
    PB_STATS_CMD_PERIOD,       // asks for the dates
    PB_STATS_CMD_PERIOD_APPLY, // posted by the keyboard
    PB_STATS_CMD_BOOKS,        // all books
    PB_STATS_CMD_PREV,         // previous screen of a report or of the book list
    PB_STATS_CMD_NEXT,
    PB_STATS_CMD_BACK,         // from a report to the screen it was opened from
    PB_STATS_CMD_BOOK = 81000, // + book index in the log
    PB_STATS_CMD_LAST = 89999
};

class PbStatsView
{
public:
    static PbStatsView & instance();
    static bool isStatsCommand(int command) { return command >= PB_STATS_CMD_FIRST && command <= PB_STATS_CMD_LAST; }

    /// sets the log file; called once at start
    static void init(const lString16 & stateDir);
    /// the book window shows a page: times it for the log
    static void pageShown(LVDocView * doc);
    /// the book is no longer read (exit, standby): writes the timed page
    static void flush();

    /// opens the list of the statistics
    void open(CRGUIWindowManager * wm);
    /// handles PB_STATS_CMD_FIRST..PB_STATS_CMD_LAST; true when consumed
    bool onCommand(CRGUIWindowManager * wm, int command, int params);

    /// the period keyboard writes here before posting PB_STATS_CMD_PERIOD_APPLY
    char keyBuffer[64];

    /// one line of a report: optional gray label and text in one font; title lines are bigger,
    /// section headings are bold with a line above them
    struct Line {
        lString16 label;
        lString16 text;
        bool title;
        bool section;
        bool newRow;  // starts a new row of the screen
        int indent;   // in line heights
        Line() : title(false), section(false), newRow(false), indent(0) {}
    };

private:
    PbStatsView();

    enum Mode { MODE_MENU, MODE_BOOKS, MODE_REPORT };

    PbStatsLog _log;
    std::string _thisBookKey;
    std::string _thisBookTitle;
    int _screen;               // screen of the shown report
    int _booksScreen;          // screen of the book list
    Mode _mode;                // the screen shown last
    int _reportBack;           // command that leaves the report
    lString16 _reportCaption;
    std::vector<Line> _report;
    std::vector<int> _bookIndexes;
    std::vector<long long> _bookSeconds;

    void reload();
    void showMenu(CRGUIWindowManager * wm);
    void showBooks(CRGUIWindowManager * wm);
    void showReport(CRGUIWindowManager * wm);
    void bookReport(int bookIndex, int back);
    void periodReport(const lString16 & caption, int fromDay, int toDay);
    void askPeriod();
};

#endif
