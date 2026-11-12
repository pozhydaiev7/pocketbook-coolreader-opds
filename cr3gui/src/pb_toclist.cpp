// Contents of the book as a plain touch list. See pb_toclist.h.
#include "pb_toclist.h"

#include <cri18n.h>
#include <lvstring.h>

#include "pb_toc.h"
#include "opds/opdsmenu.h"

using namespace opdsui;

PbTocList & PbTocList::instance()
{
    static PbTocList list;
    return list;
}

void PbTocList::open(CRGUIWindowManager * wm, const tocentry * toc, int length, int currentPage)
{
    // own copy: the reader frees its contents array when it jumps to a page
    _entries.clear();
    _current = 0;
    for (int i = 0; i < length; i++) {
        Entry e;
        e.level = toc[i].level;
        e.page = toc[i].page;
        e.text = Utf8ToUnicode(lString8(toc[i].text != NULL ? toc[i].text : ""));
        _entries.push_back(e);
        if (currentPage >= toc[i].position)
            _current = i;
    }
    _screen = -1; // the screen of the current chapter
    show(wm);
}

void PbTocList::show(CRGUIWindowManager * wm)
{
    lvRect rc = wm->getScreen()->getRect();
    OpdsMenu * menu = new OpdsMenu(wm, lString16(_("Contents")), rc, false);
    menu->setPlainRows();
    int rows = menu->setupRows(false);
    menu->setCloseCommand(PB_TOC_LIST_CMD_CLOSE);
    menu->setPageCommands(PB_TOC_LIST_CMD_PREV, PB_TOC_LIST_CMD_NEXT);

    // the first row goes back to the book; a long list is turned with the arrows of the title
    int total = (int)_entries.size();
    _perScreen = rows - 1;
    if (_perScreen < 1)
        _perScreen = 1;
    int screens = total > 0 ? (total + _perScreen - 1) / _perScreen : 1;
    if (_screen < 0)
        _screen = _current / _perScreen;
    if (_screen >= screens)
        _screen = screens - 1;
    menu->setScreens(_screen, screens);

    menu->add(PB_TOC_LIST_CMD_CLOSE, lString16(MARK_PREV) + lString16(_("Back to the book")));
    int levelIndent = menu->subFont->getHeight();
    int first = _screen * _perScreen;
    for (int i = first; i < total && i < first + _perScreen; i++) {
        const Entry & e = _entries[i];
        OpdsItem * item = new OpdsItem(menu, PB_TOC_SAFE_CMD_RANGE + e.page, e.text, lString16::empty_str);
        item->mark = i == _current; // chapter of the current page
        item->indent = (e.level > 1 ? e.level - 1 : 0) * levelIndent;
        item->aside = lString16::itoa(e.page);
        menu->addItem(item);
    }
    activate(wm, menu);
}

bool PbTocList::onCommand(CRGUIWindowManager * wm, int command, int params)
{
    (void)params;
    switch (command) {
    case PB_TOC_LIST_CMD_CLOSE:
        return true;
    case PB_TOC_LIST_CMD_PREV:
        if (_screen > 0)
            _screen--;
        show(wm);
        return true;
    case PB_TOC_LIST_CMD_NEXT:
        if ((_screen + 1) * _perScreen < (int)_entries.size())
            _screen++;
        show(wm);
        return true;
    case PB_TOC_LIST_CMD_FILLER:
        show(wm);
        return true;
    default:
        return true;
    }
}
