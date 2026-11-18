/*
 * The picture mode: a PDF or DjVu file shown page by page as it is (pb_pagedoc.h draws the pages).
 *
 * The window stands over the book window, which holds no book then; the events come to it through
 * the window manager as to any top window.
 *
 * What is on the screen is a frame of a page. Two settings choose it:
 *   scale     the whole page, the page width, the whole content (margins cut off), the content
 *             width, or the own frame of the reader (a point of the page, a zoom);
 *   leafing   "next page at once" (a page turn shows the same frame of the next page) or
 *             "by frames" (the frames of a page one after another, then the next page).
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
    PB_PAGEVIEW_CMD_SCALE = 68100,  // + PbPageScale
    PB_PAGEVIEW_CMD_PAGE = 68200,   // + index in the list of bookmarks, up to 68999
    PB_PAGEVIEW_CMD_LAST = 68999
};

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
    /// Writes the page, the frames and the bookmarks of the file (the program goes to the background or ends).
    void keep() { saveState(); }

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

    PbPageWindow(CRGUIWindowManager * wm, PbPageDoc * doc, const lString32 & path, const lString8 & stateFile);
    void loadState();
    void saveState();
    void readDefaults();
    void changed();         // the frames drawn are no longer good
    /// The frames of a page in the order they are read; at least one.
    void framesOfPage(int page, std::vector<PbPageView> & frames);
    void drawShot(Shot & shot, int page, int frame);
    void target(int direction, int & page, int & frame);
    void turn(int direction);
    void goToPage(int page);
    void armPause();
    void showMenu();
    void showScaleList();
    void showMarks();
    void showContents();
    void adjustBegin();
    void adjustEnd(bool keep);
    bool adjustTouch(int x, int y, CRGUITouchEventType evType);
    bool redrawAfterTouch();
    void adjustButtons(std::vector<lvRect> & rects);
    void drawAdjustPanel(LVDrawBuf * canvas);
    void drawNumber(LVDrawBuf * canvas);
};

#endif // PB_PAGEVIEW_H_INCLUDED
