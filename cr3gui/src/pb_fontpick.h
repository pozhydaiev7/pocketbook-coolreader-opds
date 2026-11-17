/*
 * Choice of the font face, the font size and the font weight of the text, with a sample.
 *
 * A list of the values with a panel under it: the first lines of the page being read, drawn
 * with the value tapped. A tap on a value chooses it and changes the sample; leaving the list
 * sets the value chosen, and the book is laid out again once, for the value the reader has
 * chosen by its look. Before, a tap in the list of the settings menu set the value and closed
 * the list, and what the value looks like was seen only when the menu was closed and the book
 * laid out again.
 *
 * Opened by the items "Default font face", "Default font size" and "Font weight" of the settings
 * menu (PB_CMD_FONT_FACE, PB_CMD_FONT_SIZE, PB_CMD_FONT_WEIGHT).
 */
#ifndef PB_FONTPICK_H
#define PB_FONTPICK_H

#include <crgui.h>
#include <vector>
#include "opdsmenu.h"

// Command ids posted by the screen: after the program update (66000..66999).
enum PbFontPickCommands {
    PB_FONTPICK_CMD_FIRST = 67000,
    PB_FONTPICK_CMD_MENU = PB_FONTPICK_CMD_FIRST, // the screen again
    PB_FONTPICK_CMD_CLOSE,                        // leaves the list and sets the value chosen
    PB_FONTPICK_CMD_PREV,
    PB_FONTPICK_CMD_NEXT,
    PB_FONTPICK_CMD_VALUE = 67100, // + index of the value: shown in the sample
    PB_FONTPICK_CMD_LAST = 67999
};

class PbFontPick : public opdsui::OpdsPanel
{
public:
    enum Kind { FACE, SIZE, WEIGHT };

    static PbFontPick & instance();
    static bool isFontPickCommand(int command) { return command >= PB_FONTPICK_CMD_FIRST && command <= PB_FONTPICK_CMD_LAST; }

    /// opens the list; fromSettings: "Back" opens the settings menu again;
    /// settingsItem: the item of the settings menu the list was opened by
    void open(CRGUIWindowManager * wm, Kind kind, bool fromSettings, int settingsItem);
    /// handles PB_FONTPICK_CMD_FIRST..PB_FONTPICK_CMD_LAST; true when consumed
    bool onCommand(CRGUIWindowManager * wm, int command, int params);

    /// the sample
    virtual void drawPanel(LVDrawBuf & buf, const lvRect & rc, lUInt32 textColor);

private:
    PbFontPick() : _kind(SIZE), _fromSettings(false), _settingsItem(0), _current(0), _tried(0), _screen(-1),
                   _size(0), _weight(400) {}

    Kind _kind;
    bool _fromSettings;
    int _settingsItem;
    std::vector<lString32> _values; // as the setting keeps them
    std::vector<lString32> _labels; // as the list shows them
    int _current; // the value of the book
    int _tried;   // the value of the sample
    int _screen;  // screen of the list, -1: the one with the value of the sample
    // the font of the sample: the value tried and the two other settings of the book
    lString8 _face;
    int _size;
    int _weight;
    lString32 _sample; // text of the sample
    LVFontRef _captionFont;

    void loadValues();
    void loadSample();
    void setTried(int index);
    void show(CRGUIWindowManager * wm);
    void close(CRGUIWindowManager * wm);
};

#endif
