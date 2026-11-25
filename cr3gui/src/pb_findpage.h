/*
 * The places of a text on one page of a book: the search of the reader goes page by page and
 * stops on a page that has one (viewdlg.cpp, CRViewDialog::findPagesText).
 *
 * The engine looks through whole pieces of text, and a piece is a paragraph: of a paragraph that
 * begins on the page and goes on to the next ones it gives the places on those pages too. Taken
 * as they came, they made the search stop on a page that showed nothing found, once for every
 * page the paragraph ran over before the word. Measured on the PC bench with the engine of the
 * reader: 41 stops for a word that stands on 33 pages of a novel, 395 for a name on 378. The
 * places kept here are the ones whose line is on the page.
 *
 * Depends on the engine alone: the PC bench builds it without the reader.
 */
#ifndef PB_FINDPAGE_H
#define PB_FINDPAGE_H

#include <lvdocview.h>

/// Looks for the pattern on a page of the book, the case of the letters aside. words: the places
/// on the page, to be shown selected. False when the page has none.
inline bool pbFindOnPage(LVDocView * view, int page, const lString32 & pattern, LVArray<ldomWord> & words)
{
    words.clear();
    LVRef<ldomXRange> range = view->getPageDocumentRange(page);
    if (range.isNull())
        return false;
    const int height = view->getPageHeight(page);
    if (!range->findText(pattern, true, false, words, 200, height, true))
        return false;
    const int top = view->getPageStartY(page);
    // (not in the page mode: nothing to tell the pages apart by)
    if (top < 0 || height <= 0)
        return words.length() > 0;
    int kept = 0;
    for (int i = 0; i < words.length(); i++) {
        ldomXPointer start(words[i].getNode(), words[i].getStart());
        const int y = start.toPoint().y;
        if (y >= top && y < top + height)
            words[kept++] = words[i];
    }
    if (kept < words.length())
        words.erase(kept, words.length() - kept);
    return kept > 0;
}

#endif // PB_FINDPAGE_H
