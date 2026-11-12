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
lString16 limitTextWidth( lString16 s, int width, LVFontRef font );

// spaces left in the label of the current chapter for its triangle mark
#define TOC_MARK_ROOM L"   "
#define TOC_MARK_ROOM_LEN 3

CRTocMenuItem::CRTocMenuItem( CRMenu * menu, int id, const lString16 & label, int page, bool withPage,
    bool current )
: CRMenuItem( menu, id, label, LVImageSourceRef(), LVFontRef() ),
  _pageText( lString16::itoa(page) ), _withPage( withPage ), _current( current )
{
}

// The triangle mark of the current chapter: after the indent of its level, in the room left for
// it in the label.
static void drawTocMark( LVDrawBuf & buf, const lvRect & inner, const lString16 & label, LVFontRef font,
    lUInt32 color )
{
    int spaces = 0;
    while ( spaces < (int)label.length() && label[spaces] == ' ' )
        spaces++;
    spaces -= TOC_MARK_ROOM_LEN;
    int x = inner.left + ( spaces > 0 ? font->getTextWidth( label.c_str(), spaces ) : 0 );
    opdsui::drawRowMark( buf, x, inner.top, inner.height(), font->getHeight(), color );
}

void CRTocMenuItem::Draw( LVDrawBuf & buf, lvRect & rc, CRRectSkinRef skin,
    CRRectSkinRef valueSkin, bool selected )
{
    if ( !_withPage ) {
        // a plain row, as the menu draws it
        CRMenuItem::Draw( buf, rc, skin, valueSkin, selected );
        if ( _current ) {
            LVFontRef font = getFont();
            if ( font.isNull() )
                font = skin->getFont();
            drawTocMark( buf, skin->getClientRect( rc ), _label, font, skin->getTextColor() );
        }
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
    if ( _current )
        drawTocMark( buf, inner, _label, font, textColor );
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
    lString16 title = limitTextWidth( _label, titleRc.width(), font );
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

CRTocMenu::CRTocMenu( CRGUIWindowManager * wm, CRPropRef newProps, int id,
    CRGUIAcceleratorTableRef menuAccelerators, lvRect &rc, tocentry *tocItems,
    int length, int currentPage )
: CRFullScreenMenu( wm, id, lString16(_("Contents")), 8, rc ),
  props( newProps ),
  _menuAccelerators( menuAccelerators ), _menuItemId(mm_TouchTOC)
{
    CRLog::trace("showTocTouchMenu(): Construct");

    if( !fontAntiAliasingActivated() ) {
        forcePartialBwUpdates = true;
    }

    setSkinName(L"#touch-toc");

    _fullscreen = true;

    LVFontRef valueFont( fontMan->GetFont(
        VALUE_FONT_SIZE,
        400,
        true,
        css_ff_sans_serif,
        lString8("Arial")
        ) );

    CRMenu * mainMenu = this;
    mainMenu->setAccelerators( _menuAccelerators );

    _selectedItem = 0;

    bool showPages = props->getIntDef( PROP_TOC_PAGE_NUMBERS, 1 ) == 1;
    // rows with dotted leaders look like a printed contents page: no separator lines
    if ( showPages && !_wm->getSkin()->getMenuSkin( L"#touch-toc-dots" ).isNull() )
        setSkinName( L"#touch-toc-dots" );

    if ( length ) {

        for (int i = 0; i < length; i++) {
            if( currentPage >= tocItems[i].position ) {
                _selectedItem = i;
            }
        }

        for (int i = 0; i < length; i++) {

            lString16 text(tocItems[i].text);

            // Prepend page number
            // text = L"[" + lString16::itoa(tocItems[i].position) + L"] " + text;

            // Current chapter: room for its triangle mark, drawn by the item
            if( _selectedItem == i ) {
                text = TOC_MARK_ROOM + text;
            }

            // Indent level
            for( int level = tocItems[i].level; level > 1; level-- ) {
                text = L"     " + text;
            }

            mainMenu->addItem(new CRTocMenuItem(
                mainMenu,
                PB_TOC_SAFE_CMD_RANGE + tocItems[i].page,
                lString16(UnicodeToUtf8(text).c_str()),
                tocItems[i].position,
                showPages,
                _selectedItem == i
                ));
        }
    }

    // Empty contents - shouldn't reach this
    else {
        mainMenu->addItem( new CRMenuItem(
            mainMenu,
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
