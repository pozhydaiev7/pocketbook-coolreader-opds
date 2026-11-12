/*
 * On-screen touch keyboard drawn by CoolReader on PocketBook, used instead of the firmware keyboard
 * where CoolReader needs full control over it: it follows the night mode of CoolReader (the firmware
 * keyboard keeps its colors). English, Russian and Ukrainian layouts with the keys placed as on the
 * firmware keyboard: 5 rows of 11 keys, the digits in the first row, the signs and capital letters
 * on the Shift page.
 * Devices without a touch screen keep the firmware keyboard (see pbKeyboardUsable()).
 */
#ifndef PB_KEYBOARD_H_INCLUDED
#define PB_KEYBOARD_H_INCLUDED

#include <crgui.h>
#include <vector>

/// last layout of the keyboard: 0 English, 1 Russian, 2 Ukrainian
#define PROP_POCKETBOOK_KEYBOARD_LAYOUT "cr3.pocketbook.keyboard.layout"

/// true when the CoolReader keyboard can be used: the device has a touch screen
bool pbKeyboardUsable();

class PbKeyboard : public CRGUIWindowBase
{
public:
    /// buffer: the text to edit (UTF-8), the result is written there; handler gets the buffer
    /// when OK is tapped with some text, it is not called when the keyboard is cancelled
    PbKeyboard(CRGUIWindowManager * wm, const char * title, char * buffer, int maxlen,
               void (*handler)(char *), bool inverted);
    virtual ~PbKeyboard() {}
    virtual bool onCommand(int command, int params = 0);
    virtual bool onTouch(int x, int y, CRGUITouchEventType evType);
    virtual bool onTouchEvent(int x, int y, CRGUITouchEventType evType);
    // The window manager draws only dirty windows, from the bottom up: when the window below
    // (the dictionary, full screen) is redrawn, the keyboard has to be drawn again over it,
    // otherwise it disappears while it still gets every touch.
    virtual bool isDirty() { return true; }
protected:
    virtual void draw();
private:
    enum Layout { LAYOUT_EN, LAYOUT_RU, LAYOUT_UK, LAYOUT_COUNT };
    enum Special {
        PBK_SHIFT = -1, PBK_BACKSPACE = -2, PBK_SPACE = -3, PBK_LAYOUT = -5,
        PBK_OK = -6, PBK_CANCEL = -7
    };
    struct Key {
        int code;        // character, or Special
        lvRect rc;       // cell of the key, its frame is drawn inside
    };
    lString32 _title;
    char * _buffer;
    int _maxlen;
    void (*_handler)(char *);
    bool _inverted;
    lString32 _text;
    bool _textSelected;  // the starting text is selected: the first letter replaces it
    int _layout;
    bool _shift;         // the Shift page of the layout is shown, for one character
    int _pressed;        // key under the finger, -1 when none
    std::vector<Key> _keys;
    lvRect _textRc;
    LVFontRef _keyFont;
    LVFontRef _keyBoldFont; // labels of the service keys
    LVFontRef _textFont;
    int _line;              // frame width

    void build();
    void addRow(const char * row, int left, int top, int cellWidth, int cellHeight);
    static int keyGap(int cellHeight);
    int keyAt(int x, int y) const;
    lString32 label(const Key & key) const;
    void drawTextField(LVDrawBuf & buf, bool invert);
    void drawKey(LVDrawBuf & buf, int index, bool invert);
    // redraws one key and/or the text field and updates only their rectangles of the screen:
    // e-ink updates the rows that changed over the width of the rectangle, so a whole-window
    // update made every key of the row blink
    void refresh(int key, bool text);
    enum Changed { CHANGED_CLOSED, CHANGED_TEXT, CHANGED_ALL };
    Changed press(const Key & key);
    void finish(bool accept);
};

/// Opens the CoolReader keyboard over the windows of the window manager.
void pbOpenCrKeyboard(CRGUIWindowManager * wm, const char * title, char * buffer, int maxlen,
                      void (*handler)(char *), bool inverted);

#endif // PB_KEYBOARD_H_INCLUDED
