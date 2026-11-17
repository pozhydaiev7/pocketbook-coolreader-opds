/*
 * Choice of the font face, the font size and the font weight with a sample. See pb_fontpick.h.
 * The screen is a full-screen touch list of the OPDS catalog (opds/opdsmenu.h).
 */
#include "pb_fontpick.h"

#include <crengine.h>
#include <cri18n.h>
#include <stdlib.h>

#include "cr3pocketbook.h"
#include "mainwnd.h"
#include "settings.h"

extern V3DocViewWin * main_win;

using namespace opdsui;

namespace {

DECL_DEF_CR_FONT_SIZES;

lString32 tr(const char * s)
{
    return lString32(_(s));
}

// the weights of the item "Font weight" of the settings menu (font_weights in settings.cpp)
const int fontWeights[] = { 300, 400, 425, 450, 475, 500, 525, 550, 600, 650, 700, 800, 900 };

lString32 weightLabel(int weight)
{
    if (weight == 400)
        return tr("Normal");
    if (weight == 700)
        return tr("Bold");
    return lString32::itoa(weight);
}

// longest text the sample is made of: more than any panel shows
const int SAMPLE_MAX_TEXT = 1200;

} // namespace

PbFontPick & PbFontPick::instance()
{
    static PbFontPick pick;
    return pick;
}

void PbFontPick::loadValues()
{
    CRPropRef props = main_win->getProps();
    _values.clear();
    _labels.clear();
    _face = UnicodeToUtf8(props->getStringDef(PROP_FONT_FACE, "Arial"));
    _size = main_win->getDocView()->getFontSize();
    _weight = props->getIntDef(PROP_FONT_BASE_WEIGHT, 400);
    _current = 0;
    switch (_kind) {
    case FACE: {
        lString32Collection list;
        fontMan->getFaceList(list);
        for (int i = 0; i < list.length(); i++) {
            _values.push_back(list[i]);
            _labels.push_back(list[i]);
            if (UnicodeToUtf8(list[i]) == _face)
                _current = i;
        }
        break;
    }
    case SIZE: {
        int count = (int)(sizeof(cr_font_sizes) / sizeof(cr_font_sizes[0]));
        for (int i = 0; i < count; i++) {
            _values.push_back(lString32::itoa(cr_font_sizes[i]));
            _labels.push_back(lString32::itoa(cr_font_sizes[i]));
            // the nearest size of the list
            if (abs(cr_font_sizes[i] - _size) < abs(cr_font_sizes[_current] - _size))
                _current = i;
        }
        break;
    }
    case WEIGHT: {
        int count = (int)(sizeof(fontWeights) / sizeof(fontWeights[0]));
        for (int i = 0; i < count; i++) {
            _values.push_back(lString32::itoa(fontWeights[i]));
            _labels.push_back(weightLabel(fontWeights[i]));
            if (abs(fontWeights[i] - _weight) < abs(fontWeights[_current] - _weight))
                _current = i;
        }
        break;
    }
    }
}

// The text of the page being read, as one paragraph. A page without text (a cover, a picture)
// gives a sentence with every letter of the alphabet.
void PbFontPick::loadSample()
{
    _sample.clear();
    LVDocView * view = main_win->getDocView();
    LVRef<ldomXRange> range = view->getPageDocumentRange(-1);
    lString32 text;
    if (!range.isNull())
        text = range->getRangeText(U' ', SAMPLE_MAX_TEXT);
    bool space = true;
    for (int i = 0; i < text.length() && _sample.length() < SAMPLE_MAX_TEXT; i++) {
        lChar32 ch = text[i];
        bool blank = ch == U' ' || ch == U'\t' || ch == U'\r' || ch == U'\n' || ch == 0xA0;
        if (blank) {
            if (!space)
                _sample << U' ';
            space = true;
        } else {
            _sample << ch;
            space = false;
        }
    }
    if (_sample.length() < 40)
        _sample = tr("The quick brown fox jumps over the lazy dog. 0123456789");
}

void PbFontPick::setTried(int index)
{
    if (index < 0 || index >= (int)_values.size())
        return;
    _tried = index;
    switch (_kind) {
    case FACE:
        _face = UnicodeToUtf8(_values[index]);
        break;
    case SIZE:
        _size = _values[index].atoi();
        break;
    case WEIGHT:
        _weight = _values[index].atoi();
        break;
    }
}

void PbFontPick::open(CRGUIWindowManager * wm, Kind kind, bool fromSettings, int settingsItem)
{
    _kind = kind;
    _fromSettings = fromSettings;
    _settingsItem = settingsItem;
    loadValues();
    if (_values.empty())
        return;
    loadSample();
    setTried(_current);
    _screen = -1;
    CRLog::info("Font choice %d opened: %d values, the book has \"%s\", size %d, weight %d",
                (int)kind, (int)_values.size(), _face.c_str(), _size, _weight);
    show(wm);
}

void PbFontPick::show(CRGUIWindowManager * wm)
{
    lvRect rc = wm->getScreen()->getRect();
    lString32 caption = _kind == FACE ? tr("Default font face")
                      : _kind == SIZE ? tr("Default font size") : tr("Font weight");
    OpdsMenu * menu = new OpdsMenu(wm, caption, rc, true);
    int rows = menu->setupRows(false);
    _captionFont = menu->subFont;
    // the rows of the screen: "Back", the values, the sample
    int panelRows = rows * 2 / 5;
    if (panelRows < 3)
        panelRows = 3;
    int perScreen = rows - 1 - panelRows;
    if (perScreen < 2) {
        perScreen = 2;
        panelRows = rows - 1 - perScreen;
    }
    int count = (int)_values.size();
    int screens = (count + perScreen - 1) / perScreen;
    if (_screen < 0)
        _screen = _tried / perScreen;
    if (_screen >= screens)
        _screen = screens - 1;
    menu->setCloseCommand(PB_FONTPICK_CMD_CLOSE);
    menu->setPageCommands(PB_FONTPICK_CMD_PREV, PB_FONTPICK_CMD_NEXT);
    menu->setScreens(_screen, screens);
    menu->setSwipeTurnsScreens(true);

    menu->add(PB_FONTPICK_CMD_CLOSE, lString32(MARK_PREV) + (_fromSettings ? tr("Back to settings") : tr("Back to the book")));
    int size = menu->titleFont->getSize();
    for (int n = 0; n < perScreen; n++) {
        int i = _screen * perScreen + n;
        if (i >= count) {
            // the last screen is filled up, so that the sample keeps its place
            menu->addItem(new OpdsItem(menu, PB_FONTPICK_CMD_MENU, lString32::empty_str, lString32::empty_str, OpdsItem::TEXT));
            continue;
        }
        OpdsItem * item = new OpdsItem(menu, PB_FONTPICK_CMD_VALUE + i, _labels[i], lString32::empty_str);
        // the triangle: the value of the sample; the note at the right: the value of the book
        item->mark = i == _tried;
        if (i == _current)
            item->aside = tr("in the book");
        if (_kind == FACE)
            item->font = fontMan->GetFont(size, 400, false, css_ff_sans_serif, UnicodeToUtf8(_values[i]));
        menu->addItem(item);
    }
    for (int n = 0; n < panelRows; n++) {
        OpdsItem * item = new OpdsItem(menu, PB_FONTPICK_CMD_MENU, lString32::empty_str, lString32::empty_str, OpdsItem::TEXT);
        if (n == panelRows - 1) {
            item->panel = this;
            item->panelRows = panelRows;
        }
        menu->addItem(item);
    }
    activate(wm, menu);
}

// The sample: a line with the word "Sample" and the font it is drawn with, then the text of the
// page in that font, as many whole lines as the panel holds.
void PbFontPick::drawPanel(LVDrawBuf & buf, const lvRect & rc, lUInt32 textColor)
{
    LVFontRef cf = _captionFont;
    if (cf.isNull())
        return;
    int pad = cf->getHeight() / 2;
    lvRect r(rc.left + pad, rc.top, rc.right - pad, rc.bottom);
    // a line above the panel, in the text color
    int t = cf->getHeight() / 16;
    if (t < 1)
        t = 1;
    buf.FillRect(rc.left, rc.top + t, rc.right, rc.top + 2 * t, textColor);
    int y = rc.top + 3 * t + cf->getHeight() / 4;
    // the two numbers are named: bare, they tell nothing to a reader who has not set them
    lString32 font = Utf8ToUnicode(_face) + lString32(DOT) + tr("size") + U" " + lString32::itoa(_size)
            + lString32(DOT) + tr("weight") + U" " + lString32::itoa(_weight);
    lString32 label = tr("Sample");
    buf.SetTextColor(OPDSUI_LABEL_COLOR);
    cf->DrawTextString(&buf, r.left, y, label.c_str(), label.length(), U'?', NULL, false, 0);
    font = fitText(cf, font, r.width() - textWidth(cf, label) - cf->getHeight());
    cf->DrawTextString(&buf, r.right - textWidth(cf, font), y, font.c_str(), font.length(), U'?', NULL, false, 0);
    buf.SetTextColor(textColor);
    y += cf->getHeight() + cf->getHeight() / 4;

    LVFontRef sf = fontMan->GetFont(_size, _weight, false, css_ff_serif, _face);
    if (sf.isNull())
        return;
    // the line step of the book
    int interline = main_win->getProps()->getIntDef(PROP_INTERLINE_SPACE, 100);
    if (interline < 50 || interline > 200)
        interline = 100;
    int step = sf->getHeight() * interline / 100;
    if (step < 1)
        step = 1;
    std::vector<lString32> lines = wrapText(sf, _sample, r.width(), r.width());
    for (size_t i = 0; i < lines.size(); i++) {
        // whole lines only; the first line is drawn even when the panel is lower than the font
        if (i > 0 && y + sf->getHeight() > rc.bottom)
            break;
        lString32 line = fitText(sf, lines[i], r.width());
        sf->DrawTextString(&buf, r.left, y, line.c_str(), line.length(), U'?', NULL, false, 0);
        y += step;
    }
}

void PbFontPick::close(CRGUIWindowManager * wm)
{
    _sample.clear();
    _captionFont.Clear();
    // opened from the settings menu: back to that menu, at its item
    if (_fromSettings) {
        _fromSettings = false;
        wm->postCommand(MCMD_SETTINGS, _settingsItem);
    }
}

bool PbFontPick::onCommand(CRGUIWindowManager * wm, int command, int params)
{
    (void)params;
    if (_values.empty())
        return true;
    if (command >= PB_FONTPICK_CMD_VALUE) {
        setTried(command - PB_FONTPICK_CMD_VALUE);
        show(wm);
        return true;
    }
    switch (command) {
    case PB_FONTPICK_CMD_CLOSE:
        // Leaving the list sets the value chosen in it, as leaving the settings menu applies
        // what was changed there: no button to press. The value of the book, tapped again
        // before leaving, leaves the book as it is.
        if (_tried != _current) {
            const char * name = _kind == FACE ? PROP_FONT_FACE : _kind == SIZE ? PROP_FONT_SIZE : PROP_FONT_BASE_WEIGHT;
            CRLog::info("Font choice %d: \"%s\" applied", (int)_kind, UnicodeToUtf8(_values[_tried]).c_str());
            main_win->applySetting(name, _values[_tried]);
        }
        close(wm);
        return true;
    case PB_FONTPICK_CMD_PREV:
        if (_screen > 0)
            _screen--;
        show(wm);
        return true;
    case PB_FONTPICK_CMD_NEXT:
        _screen++;
        show(wm);
        return true;
    default:
        // the screen again: a tap on the sample or on an empty row
        show(wm);
        return true;
    }
}
