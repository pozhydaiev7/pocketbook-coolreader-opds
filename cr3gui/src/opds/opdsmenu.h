// Full-screen touch lists of the CoolReader PocketBook port, in the touch-TOC style:
// rows with a bold title and a second line, two-line text rows and a paging row.
// Used by the OPDS catalog browser and by the reading statistics.
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

// One row of a list: title (bold) and an optional second line, two lines of plain text,
// or the paging row with three tap zones: previous | center | next.
class OpdsItem : public CRMenuItem
{
public:
    enum Kind { ROW, TEXT, PAGER };

    OpdsItem(OpdsMenu * menu, int id, const lString16 & title, const lString16 & subtitle, Kind kind = ROW);
    virtual void Draw(LVDrawBuf & buf, lvRect & rc, CRRectSkinRef skin, CRRectSkinRef valueSkin, bool selected);
    virtual int onSelect();

    // Paging row texts; an empty side is disabled.
    lString16 left, center, right;
    // Paging row commands; 0 disables the side. The center opens the OPDS catalog menu by default.
    int leftCommand, rightCommand, centerCommand;
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
    int _lastTouchX;
    bool _paged;
    int _prevCommand;
    int _nextCommand;
    int _closeCommand;
public:
    LVFontRef titleFont;
    LVFontRef subFont;
    LVFontRef labelFont; // bold, the size of subFont
    LVFontRef bodyFont;  // description text

    // paged: page keys of the reader post OPDS_CMD_NEXT / OPDS_CMD_PREV instead of paging the menu
    OpdsMenu(CRGUIWindowManager * wm, const lString16 & caption, lvRect & rc, bool paged);
    virtual ~OpdsMenu();
    // Page keys post these commands; 0 leaves the key on that side to the menu.
    void setPageCommands(int prevCommand, int nextCommand);
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
    virtual bool onTouchEvent(int x, int y, CRGUITouchEventType evType);
    virtual bool onCommand(int command, int params = 0);

    int lastTouchX() const { return _lastTouchX; }
    // Width available for the text of a row (see OpdsItem::Draw).
    int rowTextWidth();
    void add(int command, const lString16 & title, const lString16 & subtitle = lString16::empty_str);
};

// Shows the menu as the top window.
void activate(CRGUIWindowManager * wm, OpdsMenu * menu);

} // namespace opdsui

#endif // OPDSMENU_H
