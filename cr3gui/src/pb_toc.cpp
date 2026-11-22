//
// C++ Implementation: pb_toc
//

#include <crgui.h>
#include <cri18n.h>
#include <math.h>
#include "viewdlg.h"
#include "mainwnd.h"
#include "cr3pocketbook.h"
#include "pb_toc.h"
#include "opds/opdsmenu.h"

// defined in tocdlg.cpp
lString32 limitTextWidth( lString32 s, int width, LVFontRef font );

// spaces left in the label of the current chapter for its triangle mark, and in the label of every
// row of a branched contents for the mark of a branch (before the other one)
#define TOC_MARK_ROOM U"   "
#define TOC_MARK_ROOM_LEN 3
#define TOC_BRANCH_ROOM U"   "
#define TOC_BRANCH_ROOM_LEN 3

CRTocMenuItem::CRTocMenuItem( CRMenu * menu, int id, const lString32 & label, int page, bool withPage,
    bool current, int branch, int entry )
: CRMenuItem( menu, id, label, LVImageSourceRef(), LVFontRef() ),
  _pageText( lString32::itoa(page) ), _withPage( withPage ), _current( current ), _branch( branch ),
  _entry( entry )
{
}

// The x of the room a mark takes in the label: after the leading spaces of the label but those of
// the rooms that follow it (back: the count of such spaces).
static int markX( const lvRect & inner, const lString32 & label, LVFontRef font, int back )
{
    int spaces = 0;
    while ( spaces < (int)label.length() && label[spaces] == ' ' )
        spaces++;
    spaces -= back;
    return inner.left + ( spaces > 0 ? font->getTextWidth( label.c_str(), spaces ) : 0 );
}

// The marks of a row: the hollow triangle of its branch, then the filled one of the current
// chapter, after the indent of its level, in the rooms left for them in the label.
static void drawTocMarks( LVDrawBuf & buf, const lvRect & inner, const lString32 & label, LVFontRef font,
    lUInt32 color, bool current, int branch, lvRect & toggleRect, const lvRect & row )
{
    const int markRoom = current ? TOC_MARK_ROOM_LEN : 0;
    if ( current )
        opdsui::drawRowMark( buf, markX( inner, label, font, markRoom ), inner.top, inner.height(),
            font->getHeight(), color );
    toggleRect = lvRect();
    if ( branch > 0 ) {
        int x = markX( inner, label, font, markRoom + TOC_BRANCH_ROOM_LEN );
        int w = opdsui::drawBranchMark( buf, x, inner.top, inner.height(), font->getHeight(), color, branch - 1 );
        // the place for the finger: the indent before the mark and the mark, the whole height
        toggleRect = lvRect( row.left, row.top, x + w, row.bottom );
    }
}

void CRTocMenuItem::Draw( LVDrawBuf & buf, lvRect & rc, CRRectSkinRef skin,
    CRRectSkinRef valueSkin, bool selected )
{
    if ( !_withPage ) {
        // a plain row, as the menu draws it
        CRMenuItem::Draw( buf, rc, skin, valueSkin, selected );
        LVFontRef font = getFont();
        if ( font.isNull() )
            font = skin->getFont();
        drawTocMarks( buf, skin->getClientRect( rc ), _label, font, skin->getTextColor(), _current, _branch,
            _toggleRect, rc );
        return;
    }
    _itemDirty = false;
    skin->draw( buf, rc );
    lUInt32 textColor = skin->getTextColor();
    lUInt32 bgColor = skin->getBackgroundColor();
    buf.SetTextColor( textColor );
    buf.SetBackgroundColor( bgColor );

    // the drawText overload with flags does not apply the skin borders itself
    lvRect inner = skin->getClientRect( rc );
    LVFontRef font = skin->getFont();
    drawTocMarks( buf, inner, _label, font, textColor, _current, _branch, _toggleRect, rc );
    int pageWidth = font->getTextWidth( _pageText.c_str(), _pageText.length() );
    int gap = font->getHeight() / 2;

    // page number at the right edge
    skin->drawText( buf, inner, _pageText, font, textColor, bgColor,
        SKIN_HALIGN_RIGHT | SKIN_VALIGN_CENTER );

    // title, shortened with "..." when it does not fit before the page number
    lvRect titleRc = inner;
    titleRc.right = inner.right - pageWidth - gap;
    if ( titleRc.width() <= 0 )
        return;
    lString32 title = limitTextWidth( _label, titleRc.width(), font );
    skin->drawText( buf, titleRc, title, font, textColor, bgColor,
        SKIN_HALIGN_LEFT | SKIN_VALIGN_CENTER );

    // dotted leader on the text baseline between the title and the page number
    int titleWidth = font->getTextWidth( title.c_str(), title.length() );
    int dot = font->getHeight() / 16;
    if ( dot < 1 )
        dot = 1;
    int y = inner.top + (inner.height() - font->getHeight()) / 2 + font->getBaseline() - dot;
    int x0 = inner.left + titleWidth + gap;
    int x1 = inner.right - pageWidth - gap;
    for ( int x = x0; x + dot <= x1; x += dot * 3 )
        buf.FillRect( x, y, x + dot, y + dot, textColor );
}

bool CRTocMenu::onCommand( int command, int params )
{
    CRLog::trace("CRTocMenu::onCommand(%d, %d)", command, params);
    return CRMenu::onCommand( command, params );
}

bool CRTocMenu::onTouchEvent( int x, int y, CRGUITouchEventType evType )
{
    // (the rows are entries of the contents only when there are entries)
    if ( evType == CRTOUCH_UP && !_entries.empty() ) {
        lvPoint pt( x, y );
        const int top = getTopItem();
        for ( int i = top; i < top + _pageItems && i < _items.length(); i++ ) {
            int entry = static_cast<CRTocMenuItem *>( _items[i] )->toggleAt( pt );
            if ( entry < 0 )
                continue;
            // the screen stays where it is: the rows under the entry open or close below it
            const int page = getCurPage();
            _tree.toggle( entry );
            fillItems();
            // The row chosen is that of the chapter read, as when the list is opened: the number
            // it had is another row now, and past the last one after a branch closed (the page
            // keys then asked for a row that is not there).
            std::vector<int> rows;
            _tree.rows( rows );
            _selectedItem = PbTocTree::rowOf( rows, _current );
            setCurPage( page );
            invalidateControls();
            _pageUpdate = true;
            setDirty();
            return true;
        }
    }
    return CRFullScreenMenu::onTouchEvent( x, y, evType );
}

void CRTocMenu::fillItems()
{
    _items.clear();
    std::vector<int> rows;
    _tree.rows( rows );
    const bool branched = _tree.branched();
    for ( size_t r = 0; r < rows.size(); r++ ) {
        const int i = rows[r];
        const Entry & e = _entries[i];
        lString32 text( e.text );
        // Current chapter: room for its triangle mark, drawn by the item
        if ( i == _current )
            text = TOC_MARK_ROOM + text;
        // room for the mark of a branch, in every row of a branched contents
        if ( branched )
            text = TOC_BRANCH_ROOM + text;
        // Indent level
        for ( int level = e.level; level > 1; level-- )
            text = U"     " + text;
        const int branch = !branched ? -1 : !_tree.hasChildren( i ) ? 0 : _tree.isOpen( i ) ? 2 : 1;
        addItem( new CRTocMenuItem( this, PB_TOC_SAFE_CMD_RANGE + e.page, text, e.position, _showPages,
                                    i == _current, branch, i ) );
    }
}

CRTocMenu::CRTocMenu( CRGUIWindowManager * wm, CRPropRef newProps, int id,
    CRGUIAcceleratorTableRef menuAccelerators, lvRect &rc, tocentry *tocItems,
    int length, int currentPage )
: CRFullScreenMenu( wm, id, lString32(_("Contents")), 8, rc ),
  props( newProps ),
  _menuAccelerators( menuAccelerators ), _current( 0 ), _showPages( true ), _menuItemId(mm_TouchTOC)
{
    CRLog::trace("showTocTouchMenu(): Construct");

    if( !fontAntiAliasingActivated() ) {
        forcePartialBwUpdates = true;
    }

    setSkinName(U"#touch-toc");

    _fullscreen = true;

    setAccelerators( _menuAccelerators );

    _selectedItem = 0;

    _showPages = props->getIntDef( PROP_TOC_PAGE_NUMBERS, 1 ) == 1;
    // rows with dotted leaders look like a printed contents page: no separator lines
    if ( _showPages && !_wm->getSkin()->getMenuSkin( U"#touch-toc-dots" ).isNull() )
        setSkinName( U"#touch-toc-dots" );

    if ( length ) {
        std::vector<int> levels;
        for ( int i = 0; i < length; i++ ) {
            Entry e;
            e.level = tocItems[i].level;
            e.page = tocItems[i].page;
            e.position = tocItems[i].position;
            e.text = lString32( tocItems[i].text );
            _entries.push_back( e );
            levels.push_back( e.level );
            if ( currentPage >= tocItems[i].position )
                _current = i;
        }
        _tree.init( levels, _current, pbTocCollapsed() );
        fillItems();
        std::vector<int> rows;
        _tree.rows( rows );
        _selectedItem = PbTocTree::rowOf( rows, _current );
    }

    // Empty contents - shouldn't reach this
    else {
        addItem( new CRMenuItem(
            this,
            PB_TOC_SAFE_CMD_RANGE + 1,
            "Empty TOC",
            LVImageSourceRef(),
            LVFontRef()
            ) );
    }

    reconfigure(0);

    setCurPage( (int)floor(_selectedItem/_pageItems) );

    if( forcePartialBwUpdates ) {
        SetWeakTimer("FullUpdate", FullUpdate, 150);
    }

}
