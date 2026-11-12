#include "lvdocview.h"

// PocketBook navigation helpers carried forward from this fork.

void LVDocView::addBoundsPage( ldomNode * lsection, int fh, lUInt16 section_id )
{
    int cnt = lsection->getChildCount();

    for ( int i= 0; i < cnt; i++ ) {
        ldomNode * l1section = lsection->getChildElementNode( i, section_id );
        if ( !l1section )
            continue;
        lvRect rc;
        l1section->getAbsRect(rc);
        int p;
        if ( getViewMode() == DVM_SCROLL )
            p = (int) ( (lInt64) rc.top );
        else
            p = (int) ( (lInt64) m_pages.FindNearestPage(rc.top, 0) );
        m_section_bounds_page.add(p);
        addBoundsPage( l1section, fh, section_id );
    }
}

LVArray<int> & LVDocView::getSectionBoundsPages() {
    if (m_section_bounds_page_valid)
        return m_section_bounds_page;
    m_section_bounds_page.clear();
    m_section_bounds_page.add(0);
    // Get sections from FB2 books
    ldomNode * body = m_doc->nodeFromXPath(cs32("/FictionBook/body[1]"));
    lUInt16 section_id = m_doc->getElementNameIndex(U"section");
    if (body == NULL) {
        // Get sections from EPUB books
        body = m_doc->nodeFromXPath(cs32("/body[1]"));
        section_id = m_doc->getElementNameIndex(U"DocFragment");
    }
    int fh = GetFullHeight();
    if (body && fh > 0) {
        if ( getViewMode() == DVM_PAGES ) {
            fh = m_pages.length();
            int pc = getVisiblePageCount();
            if ( ( pc==2 && (fh&1) ) )
                fh++;
            if ( fh > 1 )
                addBoundsPage( body, fh, section_id );
        } else
            addBoundsPage( body, fh, section_id );
    }
    m_section_bounds_page_valid = true;
    return m_section_bounds_page;
}

lString32 LVDocView::getPageHeaderPages(int pageIndex, int pageCount, bool pagesTilChapterEnd) {

    LVArray<int> dummy;

    // Get page info
    lString32 pageinfo;
    if (pageCount > 0) {
        pageinfo += fmt::decimal(pageIndex + 1);
        if ( !pageinfo.empty() )
            pageinfo += " / ";

        if( pagesTilChapterEnd ) {
            LVArray<int> & sbounds = dummy;
            sbounds = getSectionBounds();
            int pageToBorder = 0;
            int lenght_index = sbounds.length();
            if ( pageCount ) {
                for ( int sbound_index = 0; sbound_index < lenght_index; sbound_index++ ) {
                    int pTB = (int) ( ( (lInt64) sbounds[sbound_index] * pageCount ) / 10000 );

                    if ( pTB >= pageIndex ) {
                        pageToBorder = pTB - pageIndex;
                        // last chapter
                        if ( sbound_index != lenght_index - 1 )
                            pageToBorder++;
                        break;
                    }
                }
            }
            if( pageToBorder + pageIndex + 1 != pageCount ) {
                pageinfo += fmt::decimal( pageToBorder );
                pageinfo += " / ";
            }
        }

        pageinfo += fmt::decimal(pageCount);
    }

    return pageinfo;
}

lString32 LVDocView::getPageHeaderTitle(int pageIndex) {

    lString32 authors = getAuthors();
    lString32 title = getTitle();

    if( !authors.empty() && !title.empty() )
        return authors + " - " + title;

    if( title.empty() && authors.empty() )
        return m_doc_props->getStringDef(DOC_PROP_FILE_NAME);

    if( !title.empty() )
        return title;

    if( !authors.empty() )
        return authors;

    return lString32("");
}

lString32 LVDocView::getCurrentChapterName() {
    return getCurrentChapterName(-2);
}

lString32 LVDocView::getCurrentChapterName(int currentPage) {
    lString32 chapterName = lString32("");
	/// returns pointer to TOC root node
	LVPtrVector < LVTocItem, false > items;

    // current page
    int cp = (currentPage==-2?getCurPage():currentPage)+1;
    int page;

    // try getting real TOC chapter
    if( getFlatToc(items) && items.length() >= 3 ) {
        page = -1;
        int vcp = getVisiblePageCount();
        if (vcp < 1 || vcp > 2)
            vcp = 1;
        for (int i = 0; i < items.length(); i++) {
            LVTocItem * item = items[i];
            int p = item->getPage();
            if (p < cp && (page == -1 || page < p)) {
                page = p;
                chapterName = item->getName();
            }
        }
        return chapterName;
    }

    // show book authors - name if there are less than 3 chapters
    chapterName = getPageHeaderTitle(cp);

    // try getting sections
    LVArray<int> dummy;
    LVArray<int> & sbounds = dummy;
    sbounds = getSectionBoundsPages();
    if( sbounds.length() > 1 ) {

        int prevPage = 0;
        int index = 0;
        page = 1;

        lString32 plus = U"";

        // Start
        if( cp >= 1 ) {
            plus = U" - Start";
        }
        prevPage = page;

        // Content
        for( int sbound_index = 0; sbound_index < sbounds.length(); sbound_index++ ) {

            page = sbounds[sbound_index]+1;
            if( page == prevPage ) {
                continue;
            }

            index++;
            if( cp >= page ) {
                plus = U" - Section " + lString32::itoa(index);
            }
            else {
                break;
            }
            prevPage = page;
        }

        // End
        if( cp == getPageCount() ) {
            plus = U" - End";
        }

        // Return if got value
        if( !plus.empty() ) {
            return chapterName + plus;
        }
    }

    // return authors + title
	return chapterName;
}

CRBookmark * LVDocView::currentPageIsBookmarked() {
    CRFileHistRecord * bookmarks = getCurrentFileHistRecord();
    if (!bookmarks) return NULL;
    int curPage = getCurPage();
    int n = bookmarks->getLastShortcutBookmark()+1;
    for ( int i=1; i<=n; i++ ) {
        CRBookmark * bm = bookmarks->getShortcutBookmark(i);
        int page = 0;
        if ( bm ) {
            ldomXPointer p = getDocument()->createXPointer( bm->getStartPos() );
            if ( !p.isNull() ) {
                /// get page number by bookmark
                page = getBookmarkPage( p );
                /// get bookmark position text
                if ( page>=0 && page==curPage )
                    return bm;
            }
        }
    }
    return NULL;
}

bool LVDocView::removeCurrentPageShortcutBookmark() {
    return removeBookmark( currentPageIsBookmarked() );
}

