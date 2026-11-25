/*
 * The picture mode: a PDF or DjVu file shown page by page as it is (pb_pagedoc.h draws the pages).
 *
 * The window stands over the book window, which holds no book then; the events come to it through
 * the window manager as to any top window.
 *
 * It is used as a book is (a tester: "what is relevant for PDF and DjVu should look and work as with
 * the classic formats"): the tap zones do what they do in a book, the middle one opens the touch
 * menu of the reader with its icons and its page bar, the status line of the settings is drawn
 * over or under the page. What belongs to the view of a page alone (scale, leafing, the frame) is
 * one list, opened by an icon of that menu.
 *
 * What is on the screen is a frame of a page. Two settings choose it:
 *   scale     the whole page, the page width, the whole content (margins cut off), the content
 *             width, or the own frame of the reader (a point of the page, a zoom);
 *   leafing   "next page at once" (a page turn shows the same frame of the next page),
 *             "by frames" (the frames of a page one after another, then the next page), or
 *             "as a ribbon" (the pages one under another as wide as the screen, a small gap
 *             between them; a page turn moves on by a screen, so the end of a page and the
 *             beginning of the next one are read from one screen).
 * A turn of the picture (a crooked scan, a page lying on its side) belongs to the frame. The own
 * frame and the angle are kept apart for odd and even pages: a photographed book has its margins
 * mirrored. Scale, leafing, frames, angles, the page and the bookmarks are remembered for the file.
 */
#ifndef PB_PAGEVIEW_H_INCLUDED
#define PB_PAGEVIEW_H_INCLUDED

#include <crgui.h>
#include <vector>
#include "pb_pagedoc.h"

// Command ids of the picture mode: after the lists of the font (67000..67999).
enum PbPageWindowCommands {
    PB_PAGEVIEW_CMD_FIRST = 68000,
    PB_PAGEVIEW_CMD_CLOSE = PB_PAGEVIEW_CMD_FIRST, // a list is left: the page again
    PB_PAGEVIEW_CMD_MENU,
    PB_PAGEVIEW_CMD_CONTENTS,
    PB_PAGEVIEW_CMD_GOTO,
    PB_PAGEVIEW_CMD_GOTO_APPLY,   // the number dialog: param is the page from 1
    PB_PAGEVIEW_CMD_SCALE_LIST,
    PB_PAGEVIEW_CMD_LEAFING,
    PB_PAGEVIEW_CMD_ADJUST,
    PB_PAGEVIEW_CMD_MARK,
    PB_PAGEVIEW_CMD_MARKS,
    PB_PAGEVIEW_CMD_SETTINGS,
    PB_PAGEVIEW_CMD_EXIT,
    PB_PAGEVIEW_CMD_AS_BOOK,      // a PDF with text: read as a book (pb_pdfbook.h)
    PB_PAGEVIEW_CMD_INFO,         // what the file tells of itself
    // the list of the bookmarks (pb_pageview.cpp, PbPageMarksMenu): param is the place in the list
    PB_PAGEVIEW_CMD_MARK_GO,      // to the page of the bookmark
    PB_PAGEVIEW_CMD_MARK_SAVE,    // the page shown into the place (an empty place: a new bookmark)
    PB_PAGEVIEW_CMD_MARK_DELETE,
    PB_PAGEVIEW_CMD_SCALE = 68100,  // + PbPageScale
    PB_PAGEVIEW_CMD_PAGE = 68200,   // + index in the list of bookmarks, up to 68999
    PB_PAGEVIEW_CMD_LAST = 68999
};

/// The dictionary of the reader over the pages (cr3pocketbook.cpp): the word at the point of the
/// screen (atPoint), or the first word of the screen; on a page without text the word is typed.
void pbShowDictOverPages(bool atPoint, int x, int y);
/// What was typed on the keyboard of the search (cr3pocketbook.cpp): MCMD_SEARCH opens it, as over
/// a book, and MCMD_SEARCH_FINDFIRST comes when it is closed.
lString32 pbSearchTyped();

/// What the reader chose for a file that can be read both ways (a PDF with text).
enum PbPageMode {
    PB_PAGE_MODE_AUTO = 0,  ///< nothing chosen: the setting and the kind of the file decide
    PB_PAGE_MODE_PAGES,     ///< the pages as they are
    PB_PAGE_MODE_BOOK       ///< the text as a book
};

class PbPageWindow : public CRGUIWindowBase
{
public:
    /// The window of the open file, NULL in the book mode.
    static PbPageWindow * instance;
    /// True when the file is a PDF or DjVu by its name, or an archive with one inside.
    static bool isPageFile(const lString32 & path);
    /// True when the file is a PDF by its name, or an archive whose document is one.
    static bool isPdf(const lString32 & path);
    /// The document itself: the file, or the document of the archive taken out into a kept file.
    /// "" when there is none.
    static lString8 documentFile(const lString32 & path);
    /// PbPageMode of the file, kept in its state file.
    static int modeOf(const lString32 & path);
    static void setMode(const lString32 & path, int mode);
    /// Shows the page (from 0) and remembers it.
    void showPage(int page);
    /// Opens the file and shows its window over the book window; false when it cannot be opened.
    static bool open(CRGUIWindowManager * wm, const lString32 & path);
    /// The next frame is drawn into memory while the one on the screen is read (the timer of the pause).
    void prepareAhead();
    /// The page shown, from 0, and the number of pages: for the book state of the firmware.
    int page() const { return _page; }
    int pageCount() { return _doc->pageCount(); }
    /// The file of the reader (the archive, when the document lies in one): the file the position
    /// sync knows it by.
    const lString32 & path() const { return _path; }
    /// Writes the page, the frames and the bookmarks of the file (the program goes to the background or ends).
    void keep() { saveState(); }
    /// True when the window has a status line with a clock in it: it is drawn again when the
    /// minute changes (pbClockTimer, cr3pocketbook.cpp).
    bool showsClock() const
    {
        return _head != NULL && _pageRc.height() + _findPanel < _rect.height() && _head->getPageheaderPosition() != 0
               && (_head->getPageHeaderInfo() & PGHDR_CLOCK) != 0;
    }
    // What the touch menu of the reader and the status line show of the file, as of a book:
    /// True when the page shown has a bookmark.
    bool isMarked() const;
    /// The heading of the contents the page lies under; "" when the file has no contents.
    lString32 chapterName(int page);
    /// Where the chapters start, in 1/10000 of the file, as LVDocView::getSectionBounds gives them.
    void sectionBounds(LVArray<int> & bounds);
    /// "12 / 161", or "12 / 3 / 161" with the pages left to the end of the chapter (as
    /// LVDocView::getPageHeaderPages writes it for a book).
    lString32 progressText(int page, bool tillChapterEnd);
    // For the main menu of the reader, which is the same over the pages as over a book:
    /// True when the page shown has text: the file can be read as a book from it.
    bool canBeBook() { return _doc->hasText(_page); }
    /// True for a command this window leaves out: it belongs to a book and its text, and has
    /// nothing to act on here (the main menu does not offer it over the pages).
    static bool leavesOut(int command);

    // The dictionary over the pages (cr3pocketbook.cpp, CRPbDictionaryDialog): the words of the
    // text of the pages on the screen (a PDF with text, a DjVu with a text layer), each with the
    // rectangle it takes on the screen. A word is picked by its place: there is no recognition.
    /// Chooses the word under the point of the screen (nearest: the word nearest to it); false when
    /// the screen has no words, or (nearest false) none is under the point.
    bool dictWordAt(int x, int y, bool nearest = true);
    /// Chooses the first word of the screen, or the last one; false when it has none.
    bool dictWordEdge(bool last);
    /// Moves the choice: dx -1 / 1 the word before / after, dy -1 / 1 the line above / below.
    bool dictWordMove(int dx, int dy);
    /// The word chosen: its text and its rectangle on the screen.
    bool dictWord(lString32 & text, lvRect & rc);
    /// The screen of the pages, the word chosen shown inverted, into the canvas (clipped by the caller).
    void dictDrawPage(LVDrawBuf * canvas);
    /// A page turn under the dictionary: the next or the previous screen.
    void dictTurn(int direction) { turn(direction); }

    virtual ~PbPageWindow();
    virtual bool onCommand(int command, int params = 0);
    virtual bool onTouch(int x, int y, CRGUITouchEventType evType);
    virtual void setRect(const lvRect & rc);
    virtual void reactivated();
    virtual void closing();

protected:
    virtual void draw();

private:
    /// A frame drawn: what is, or will be, on the screen.
    struct Shot {
        int page, frame;
        unsigned stamp;     // of the settings it was drawn with
        bool ready;
        float angle;        // the turn it was drawn with, for the log
        std::vector<unsigned char> pixels;
        Shot() : page(-1), frame(-1), stamp(0), ready(false), angle(0) { }
    };
    /// The own frame of the reader for the odd or for the even pages.
    typedef PbPageOwnFrame OwnFrame;

    PbPageDoc * _doc;
    lString32 _path;        // the file of the reader (the archive, when the document lies in one)
    lString8 _stateFile;
    int _page, _frame;
    int _scale, _leafing;
    float _angle[2];        // by the parity of the page index: [0] the first, the third... page
    OwnFrame _own[2];
    std::vector<int> _marks;
    bool _showNumber;
    int _mode;              // PbPageMode: kept with the state, read by modeOf
    int _direction;         // of the last page turn: 1 forward, -1 back
    unsigned _stamp;        // changes with scale, leafing, frame, angle and the size of the window
    Shot _shown, _ahead;
    // the frame being set (PB_PAGEVIEW_CMD_ADJUST)
    bool _adjust;
    OwnFrame _adjFrame;
    float _adjAngle;
    lvPoint _touchStart;
    bool _touchStarted;
    // The status line is drawn by a view of the engine that holds no book: the same code, fonts
    // and settings as over a book, with the title, the chapters and the page of this file.
    LVDocView * _head;
    lvRect _pageRc;         // the part of the window the page is drawn in: without the status line
    std::vector<PbPageTocEntry> _toc;   // the contents of the file, read once
    bool _tocRead;
    int _bookAngle;         // the orientation to turn back to when the window goes; -1: none
    // the words of the screen for the dictionary, made for the place and the stamp below
    struct ScreenWord {
        lString32 text;
        lvRect rc;
    };
    std::vector<ScreenWord> _words;
    int _wordsPage, _wordsFrame;
    unsigned _wordsStamp;
    int _word;              // the word chosen, -1 for none
    /// The words of the screen as it is now (made again when the place has changed).
    void collectWords();
    /// A part of a page on the screen: one frame, or in the ribbon the rows of a strip.
    struct ScreenPart {
        int page;
        lvRect rc;          // on the screen
        PbPageView view;    // the frame the part is drawn by, in a buffer of the size of rc
    };
    void screenParts(std::vector<ScreenPart> & parts);
    /// Where a rectangle of the page of the part lies on the screen; false when it is not seen there.
    static bool boxOnPart(const ScreenPart & part, const PbPageBox & box, lvRect & rc);

    // The search in the text of the pages (pb_pagesearch.h): a PDF with text, a DjVu with a text
    // layer. The places found are shown inverted on every screen while the search lasts, and a
    // panel under the page goes to the next and to the previous screen that has one.
    lString32 _find;        // what is looked for, as it is compared; empty: no search
    lString32 _findTyped;   // as it was typed, for the panel
    std::vector<signed char> _findKnown;    // by page: 0 not looked at, 1 nothing there, 2 has places
    struct FoundPage {
        int page;
        unsigned used;
        std::vector<PbPageBox> boxes;
        FoundPage() : page(-1), used(0) { }
    };
    FoundPage _found[4];    // the pages looked at last, with their places
    unsigned _foundTurn;
    bool _findSawText;      // a page looked at had text
    bool _findHidden;       // a place was found in a part of a page the scale does not show
    int _findLooked;        // pages looked at
    double _findStarted, _findShown;    // of the pass through the file: for its progress bar
    int _findPanel;         // the height of the panel under the page part, 0 without a search
    bool _findPressed;      // the finger went down on the panel
    bool searching() const { return !_find.empty(); }
    int searchPanelHeight() const;
    /// MCMD_SEARCH_FINDFIRST: looks for what was typed, from the screen shown on.
    void searchStart(const lString32 & typed);
    void searchEnd();
    /// To the next (1) or the previous (-1) screen that has a place; round the end of the file.
    void searchMove(int direction);
    /// The places of a page (page units); kept for the pages looked at last.
    const std::vector<PbPageBox> & foundOn(int page);
    /// Where the search stands on a page: its frames that have a place, or in the ribbon the rows
    /// of its strip the places lie at; in their order.
    void searchStops(int page, std::vector<int> & stops);
    /// The stops as the walk of the search asks for them (PbPageSearchWalk).
    struct StopsOf;
    friend struct StopsOf;
    /// The place right after the screen shown: the next frame, or in the ribbon the page and the
    /// row of its strip right under the screen.
    void screenEnd(int & page, int & pos);
    void searchGo(int page, int pos, int direction);
    void searchButtons(std::vector<lvRect> & rects);
    void drawFound(LVDrawBuf * canvas);
    void drawSearchPanel(LVDrawBuf * canvas);
    bool searchTouch(int x, int y, CRGUITouchEventType evType);

    PbPageWindow(CRGUIWindowManager * wm, PbPageDoc * doc, const lString32 & path, const lString8 & stateFile);
    void loadState();
    void saveState();
    void readDefaults();
    void changed();         // the frames drawn are no longer good
    /// The frames of a page in the order they are read; at least one.
    void framesOfPage(int page, std::vector<PbPageView> & frames);
    void drawShot(Shot & shot, int page, int frame);
    // The ribbon: the place is the page at the top of the screen and the row of its strip there
    // (kept in _frame, as the index of a frame is in the other leafings).
    bool stripOfPage(int page, PbPageStrip & strip);
    int ribbonGap() const;
    /// Moves the place along the ribbon by delta rows; false when it does not move (the end of
    /// the file is on the screen already, or the place is the beginning).
    bool ribbonMove(int & page, int & row, int delta);
    void drawRibbon(Shot & shot, int page, int row);
    void target(int direction, int & page, int & frame);
    void turn(int direction);
    void goToPage(int page);
    void armPause();
    void showMenu();
    void showScaleList();
    /// the list of the bookmarks, as that of a book (goMode: a tap goes to the page, else sets)
    void showMarks(bool goMode);
    /// PB_PAGEVIEW_CMD_MARK_GO, _SAVE, _DELETE with the place in the list
    void markCommand(int command, int index);
    void showContents();
    void showInfo();
    void toggleMark();
    const std::vector<PbPageTocEntry> & toc();
    /// The screen is turned to the orientation of the pages (PROP_POCKETBOOK_PAGES_ORIENTATION),
    /// or back to that of the books when the pages have none of their own.
    void followOrientation();
    void setupHead();       // the settings of the status line are read; the page part is laid out
    void layout();
    void drawHead(LVDrawBuf * canvas);
    void adjustBegin();
    void adjustEnd(bool keep);
    bool adjustTouch(int x, int y, CRGUITouchEventType evType);
    bool redrawAfterTouch();
    void adjustButtons(std::vector<lvRect> & rects);
    void drawAdjustPanel(LVDrawBuf * canvas);
    void drawNumber(LVDrawBuf * canvas);
};

#endif // PB_PAGEVIEW_H_INCLUDED
