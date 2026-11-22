// Contents of the book as a plain touch list. See pb_toclist.h.
#include "pb_toclist.h"

#include <cri18n.h>
#include <lvstring.h>

#include "pb_toc.h"
#include "cr3pocketbook.h"
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
    std::vector<int> levels;
    for (int i = 0; i < length; i++) {
        Entry e;
        e.level = toc[i].level;
        e.page = toc[i].page;
        e.text = Utf8ToUnicode(lString8(toc[i].text != NULL ? toc[i].text : ""));
        _entries.push_back(e);
        levels.push_back(e.level);
        if (currentPage >= toc[i].position)
            _current = i;
    }
    _tree.init(levels, _current, pbTocCollapsed());
    _tree.rows(_rows);
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

    // a long list is turned with the arrows of the title
    int total = (int)_rows.size();
    _perScreen = rows;
    if (_perScreen < 1)
        _perScreen = 1;
    int screens = total > 0 ? (total + _perScreen - 1) / _perScreen : 1;
    if (_screen < 0)
        _screen = PbTocTree::rowOf(_rows, _current) / _perScreen;
    if (_screen >= screens)
        _screen = screens - 1;
    menu->setScreens(_screen, screens);

    menu->add(PB_TOC_LIST_CMD_CLOSE, lString32(MARK_PREV) + lString32(_("Back to the book")));
    int levelIndent = menu->subFont->getHeight();
    const bool branched = _tree.branched();
    int first = _screen * _perScreen;
    for (int r = first; r < total && r < first + _perScreen; r++) {
        const int i = _rows[r];
        const Entry & e = _entries[i];
        OpdsItem * item = new OpdsItem(menu, PB_TOC_SAFE_CMD_RANGE + e.page, e.text, lString32::empty_str);
        item->mark = i == _current; // chapter of the current page
        item->indent = (e.level > 1 ? e.level - 1 : 0) * levelIndent;
        item->aside = lString32::itoa(e.page);
        if (branched) {
            item->branch = !_tree.hasChildren(i) ? 0 : _tree.isOpen(i) ? 2 : 1;
            item->toggleCommand = PB_TOC_LIST_CMD_TOGGLE;
            item->toggleParam = i;
        }
        menu->addItem(item);
    }
    activate(wm, menu);
}

bool PbTocList::onCommand(CRGUIWindowManager * wm, int command, int params)
{
    switch (command) {
    case PB_TOC_LIST_CMD_CLOSE:
        return true;
    case PB_TOC_LIST_CMD_PREV:
        if (_screen > 0)
            _screen--;
        show(wm);
        return true;
    case PB_TOC_LIST_CMD_NEXT:
        if ((_screen + 1) * _perScreen < (int)_rows.size())
            _screen++;
        show(wm);
        return true;
    case PB_TOC_LIST_CMD_FILLER:
        show(wm);
        return true;
    case PB_TOC_LIST_CMD_TOGGLE:
        // the screen stays where it is: the rows under the entry open or close below it
        _tree.toggle(params);
        _tree.rows(_rows);
        show(wm);
        return true;
    default:
        return true;
    }
}
