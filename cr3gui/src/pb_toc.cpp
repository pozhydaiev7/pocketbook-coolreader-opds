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

// defined in tocdlg.cpp
lString32 limitTextWidth( lString32 s, int width, LVFontRef font );

CRTocMenuItem::CRTocMenuItem( CRMenu * menu, int id, const lString32 & label, int page )
: CRMenuItem( menu, id, label, LVImageSourceRef(), LVFontRef() ),
  _pageText( lString32::itoa(page) )
{
}

void CRTocMenuItem::Draw( LVDrawBuf & buf, lvRect & rc, CRRectSkinRef skin,
    CRRectSkinRef valueSkin, bool selected )
{
    _itemDirty = false;
    skin->draw( buf, rc );
    lUInt32 textColor = skin->getTextColor();
    lUInt32 bgColor = skin->getBackgroundColor();
    buf.SetTextColor( textColor );
    buf.SetBackgroundColor( bgColor );

    // the drawText overload with flags does not apply the skin borders itself
    lvRect inner = skin->getClientRect( rc );
    LVFontRef font = skin->getFont();
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

CRTocMenu::CRTocMenu( CRGUIWindowManager * wm, CRPropRef newProps, int id,
    CRGUIAcceleratorTableRef menuAccelerators, lvRect &rc, tocentry *tocItems,
    int length, int currentPage )
: CRFullScreenMenu( wm, id, lString32(_("Contents")), 8, rc ),
  props( newProps ),
  _menuAccelerators( menuAccelerators ), _menuItemId(mm_TouchTOC)
{
    CRLog::trace("showTocTouchMenu(): Construct");

    if( !fontAntiAliasingActivated() ) {
        forcePartialBwUpdates = true;
    }

    setSkinName(U"#touch-toc");

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
    if ( showPages && !_wm->getSkin()->getMenuSkin( U"#touch-toc-dots" ).isNull() )
        setSkinName( U"#touch-toc-dots" );

    if ( length ) {

        for (int i = 0; i < length; i++) {
            if( currentPage >= tocItems[i].position ) {
                _selectedItem = i;
            }
        }

        for (int i = 0; i < length; i++) {

            lString32 text(tocItems[i].text);

            // Prepend page number
            // text = U"[" + lString32::itoa(tocItems[i].position) + U"] " + text;

            // Mark current chapter
            if( _selectedItem == i ) {
                text = U">  " + text;
            }

            // Indent level
            for( int level = tocItems[i].level; level > 1; level-- ) {
                text = U"     " + text;
            }

            if( showPages ) {
                mainMenu->addItem(new CRTocMenuItem(
                    mainMenu,
                    PB_TOC_SAFE_CMD_RANGE + tocItems[i].page,
                    lString32(UnicodeToUtf8(text).c_str()),
                    tocItems[i].position
                    ));
                continue;
            }

            mainMenu->addItem(new CRMenuItem(
                mainMenu,
                PB_TOC_SAFE_CMD_RANGE + tocItems[i].page,
                UnicodeToUtf8(text).c_str(),
                LVImageSourceRef(),
                LVFontRef()/*,
                lString32::itoa(tocItems[i].position).c_str()*/
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
