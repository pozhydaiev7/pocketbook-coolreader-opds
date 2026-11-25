/*
 * A book made of a PDF or a DjVu (pb_pdfbook.h) and the pages of that file: which page of the
 * file the screen of the book stands on, and the way of the book to a page. The position sync
 * (pb_kosync.h) and the way between the book and the pages as they are both go by it.
 *
 * The first element of the page N of the file carries the id "pgN" in the book (N from 1); a
 * page with nothing on it has none. The text of the book flows, so the start of a page of the
 * file stands anywhere on a screen, and several short pages may start on one screen.
 *
 *  - The page of a screen is the page of the file the screen begins on: the last page whose
 *    first element stands at or above the top of the screen.
 *  - The book sent to the page N shows the screen the page starts on. That screen begins on the
 *    page before when the start is not at its top, so it would name another page than the one
 *    it was sent to: a place taken from the server went back to it one page lower, and the way
 *    from the pages to the book and back lost a page each time. While the screen stands where
 *    the book was sent to, its page is the page it was sent to.
 *
 * Only the engine is used here, so a test on a PC builds this file alone.
 */
#ifndef PB_PDFBOOK_PLACE_H_INCLUDED
#define PB_PDFBOOK_PLACE_H_INCLUDED

#include <crengine.h>

class PbBookPlace
{
    int _gonePage; // the page of the file (from 0) the book was sent to last, -1: none
    int _gonePos;  // the place of the view after that
    /// The top of the first element of the page (from 0) in the document; false when the page
    /// has none.
    static bool anchorTop(ldomDocument * doc, int page, int & top)
    {
        ldomNode * node = doc->getElementById((lString32("pg") + lString32::itoa(page + 1)).c_str());
        if (node == NULL)
            return false;
        lvRect rc;
        node->getAbsRect(rc);
        top = rc.top;
        return true;
    }
    /// Where the screen ends in the document: the end of the last page shown, or of the window.
    static int screenBottom(LVDocView * view, int top)
    {
        if (view->getViewMode() != DVM_PAGES)
            return top + view->GetHeight();
        LVRendPageList * list = view->getPageList();
        int last = view->getCurPage() + view->getVisiblePageCount() - 1;
        if (last >= list->length())
            last = list->length() - 1;
        if (last < 0)
            return top + view->GetHeight();
        return (*list)[last]->start + (*list)[last]->height;
    }
public:
    PbBookPlace() : _gonePage(-1), _gonePos(-1) {}
    /// Another book is in the window.
    void reset()
    {
        _gonePage = -1;
        _gonePos = -1;
    }
    /// The book goes to the page of the file (from 0, of `pages`); a page with nothing on it gives
    /// the next one that has something. False when no such page is found.
    bool goPage(LVDocView * view, int page, int pages)
    {
        ldomDocument * doc = view->getDocument();
        for (int n = page; doc != NULL && n >= 0 && n < pages; n++) {
            const lString32 id = lString32("pg") + lString32::itoa(n + 1);
            if (doc->getElementById(id.c_str()) == NULL)
                continue;
            view->goLink(lString32("#") + id, false);
            _gonePage = page;
            _gonePos = view->GetPos();
            return true;
        }
        return false;
    }
    /// The page of the file (from 0) the screen of the book stands on.
    int pageOf(LVDocView * view, int pages)
    {
        ldomDocument * doc = view->getDocument();
        if (doc == NULL)
            return 0;
        const int top = view->GetPos();
        const int bottom = screenBottom(view, top);
        int first = -1;  // the page the screen begins on
        int before = -1; // the page with something on it before that one
        int last = -1;   // the last page that starts on the screen or above it
        for (int n = 0; n < pages; n++) {
            int y = 0;
            if (!anchorTop(doc, n, y))
                continue;
            if (y >= bottom)
                break;
            last = n;
            if (y <= top + 2) {
                before = first;
                first = n;
            }
        }
        // The screen the book was sent to names the page it was sent to: a page that starts on
        // this screen, or an empty page (the next one was taken for it; the empty pages lie
        // between `before` and `first`, or right before a page that starts on the screen).
        if (_gonePage >= 0 && _gonePos == top && _gonePage > before && _gonePage <= last)
            return _gonePage;
        return first < 0 ? 0 : first;
    }
};

#endif // PB_PDFBOOK_PLACE_H_INCLUDED
