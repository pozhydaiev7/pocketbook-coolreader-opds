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
    OpdsMenu * menu = new OpdsMenu(wm, lString32(_("Contents")), rc, false);
    menu->setPlainRows();
    int rows = menu->setupRows(false);
    menu->setCloseCommand(PB_TOC_LIST_CMD_CLOSE);
    menu->setPageCommands(PB_TOC_LIST_CMD_PREV, PB_TOC_LIST_CMD_NEXT);

    // the first row goes back to the book, the last one pages the list when it is long
    int total = (int)_entries.size();
    _perScreen = rows - 1;
    if (total > _perScreen)
        _perScreen = rows - 2;
    if (_perScreen < 1)
        _perScreen = 1;
    int screens = total > 0 ? (total + _perScreen - 1) / _perScreen : 1;
    if (_screen < 0)
        _screen = _current / _perScreen;
    if (_screen >= screens)
        _screen = screens - 1;

    menu->add(PB_TOC_LIST_CMD_CLOSE, lString32(MARK_PREV) + lString32(_("Back to the book")));
    int levelIndent = menu->subFont->getHeight();
    int first = _screen * _perScreen;
    int shown = 0;
    for (int i = first; i < total && i < first + _perScreen; i++, shown++) {
        const Entry & e = _entries[i];
        lString32 title = e.text;
        if (i == _current)
            title = lString32(U"> ") + title; // chapter of the current page
        OpdsItem * item = new OpdsItem(menu, PB_TOC_SAFE_CMD_RANGE + e.page, title, lString32::empty_str);
        item->indent = (e.level > 1 ? e.level - 1 : 0) * levelIndent;
        item->aside = lString32::itoa(e.page);
        menu->addItem(item);
    }
    if (screens > 1) {
        for (int i = shown; i < _perScreen; i++)
            menu->addItem(new OpdsItem(menu, PB_TOC_LIST_CMD_FILLER, lString32::empty_str, lString32::empty_str));
        OpdsItem * pager = new OpdsItem(menu, PB_TOC_LIST_CMD_FILLER, lString32::empty_str, lString32::empty_str,
                                        OpdsItem::PAGER);
        if (_screen > 0) {
            pager->left = lString32(MARK_PREV) + lString32(_("Back"));
            pager->leftCommand = PB_TOC_LIST_CMD_PREV;
        }
        if (_screen + 1 < screens) {
            pager->right = lString32(_("Next")) + lString32(MARK_NEXT);
            pager->rightCommand = PB_TOC_LIST_CMD_NEXT;
        }
        pager->center = lString32(_("Page")) + U" " + lString32::itoa(_screen + 1) + U"/" + lString32::itoa(screens);
        pager->centerCommand = 0;
        menu->addItem(pager);
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
