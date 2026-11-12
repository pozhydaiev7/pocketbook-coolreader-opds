// Full-screen touch lists of the CoolReader PocketBook port, in the touch-TOC style:
// rows with a bold title and a second line and two-line text rows. A long list is shown screen by
// screen: the screens are turned with the arrows of the title, the screen number is between them,
// as in every menu of CoolReader.
// Used by the OPDS catalog browser, the reading statistics and the contents list.
#ifndef OPDSMENU_H
#define OPDSMENU_H

#include <crgui.h>
#include <string>
#include <vector>
#include "fsmenu.h"

namespace opdsui {

// UTF-8 (lString16(const char*) decodes UTF-8).
extern const char * const MARK_UP;   // "« "  one level up
extern const char * const MARK_PREV; // "‹ "  previous screen
extern const char * const MARK_NEXT; // " ›"  next screen
extern const char * const MARK_OPEN; // "» "  sub-catalog
extern const char * const DOT;       // "  ·  "
extern const char * const DASH;      // " — "
extern const lChar16 ELLIPSIS;

lString16 u16(const std::string & s);

// Cuts text to fit maxWidth, ending with an ellipsis.
lString16 fitText(LVFontRef font, const lString16 & text, int maxWidth);

int textWidth(LVFontRef font, const lString16 & text);

// Splits a paragraph into lines: the first one fits firstWidth, the others fit width.
// A word longer than the line stays whole and is cut with an ellipsis when drawn.
std::vector<lString16> wrapText(LVFontRef font, const lString16 & text, int firstWidth, int width);

// One line of a text screen: an optional label (gray), then the text (black), in one font
// and on one baseline. A line without text is a gap between blocks.
struct InfoLine {
    lString16 label;
    lString16 text;
    LVFontRef font;
    int indent;
    // a thin line above it: the line starts a section of the screen
    bool rule;
    InfoLine() : indent(0), rule(false) {}
};

// color of the labels of text lines
#define OPDSUI_LABEL_COLOR 0x555555

// Menu skin for the list screens. The touch-TOC skin exists only in the FW5 touch skins;
// with any other skin a missing menu skin has no item or title parts, and drawing it crashes.
lString16 menuSkinName(CRGUIWindowManager * wm);

class OpdsMenu;

// One row of a list: title (bold) and an optional second line, or two lines of plain text.
// A tap closes the list and posts the id of the row.
class OpdsItem : public CRMenuItem
{
public:
    enum Kind { ROW, TEXT };

    OpdsItem(OpdsMenu * menu, int id, const lString16 & title, const lString16 & subtitle, Kind kind = ROW);
    virtual void Draw(LVDrawBuf & buf, lvRect & rc, CRRectSkinRef skin, CRRectSkinRef valueSkin, bool selected);

    // Text row lines.
    InfoLine first, second;
    // Regular row: left indent of the title in pixels and a short text at the right edge
    // (a page number), in the font of the second line.
    int indent;
    lString16 aside;

private:
    void drawLine(LVDrawBuf & buf, const InfoLine & line, const lvRect & r, int y, int baseline);

    OpdsMenu * _owner;
    lString16 _title;
    lString16 _subtitle;
    Kind _kind;
};

// Full-screen touch list in the touch-TOC style, with its own fonts and row count.
class OpdsMenu : public CRFullScreenMenu
{
    int _rows;
    bool _paged;
    int _prevCommand;
    int _nextCommand;
    int _closeCommand;
    int _screen;   // screen of the list shown, from 0
    int _screens;  // screens of the list; 0: the menu counts its own pages
protected:
    // the page counter of the title shows the screens of the list
    virtual void drawTitleBar();
public:
    LVFontRef titleFont;
    LVFontRef subFont;
    LVFontRef labelFont; // bold, the size of subFont
    LVFontRef bodyFont;  // description text

    // paged: page keys of the reader and the arrows of the title post OPDS_CMD_NEXT / OPDS_CMD_PREV
    // instead of paging the menu
    OpdsMenu(CRGUIWindowManager * wm, const lString16 & caption, lvRect & rc, bool paged);
    virtual ~OpdsMenu();
    // Page keys and the arrows of the title post these commands; 0 leaves that side to the menu.
    void setPageCommands(int prevCommand, int nextCommand);
    // The list is one of several screens: the title shows "screen / screens" between its arrows,
    // an arrow is active when there is a screen on its side.
    void setScreens(int screen, int screens);
    virtual bool isScrollEnabled(bool down);
    // The arrow at the caption and the Back key post this command (one level up);
    // by default they post the menu id (OPDS_CMD_CLOSE: back to the book).
    void setCloseCommand(int command) { _closeCommand = command; }
    // Rows without separator lines between them (text screens), when the skin has such a menu.
    void setPlainRows();

    // Shortens the caption so it does not run under the close button and the page counter.
    void fitCaption();
    // Number of rows that fit on the screen with two text lines per row.
    int setupRows(bool twoLines);
    virtual void reconfigure(int flags);
    virtual bool onCommand(int command, int params = 0);

    // Width available for the text of a row (see OpdsItem::Draw).
    int rowTextWidth();
    void add(int command, const lString16 & title, const lString16 & subtitle = lString16::empty_str);
};

// Shows the menu as the top window.
void activate(CRGUIWindowManager * wm, OpdsMenu * menu);

} // namespace opdsui

#endif // OPDSMENU_H
