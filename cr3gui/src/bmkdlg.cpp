//
// C++ Implementation: bookmarks dialog
//
// Description: 
//      Allows to set or go to bookmarks
//
// Author: Vadim Lopatin <vadim.lopatin@coolreader.org>, (C) 2008
//
// Copyright: See COPYING file that comes with this distribution
//
// bmkdlg.cpp

#include <cri18n.h>
#include "bmkdlg.h"
#include "mainwnd.h"
#include "citedlg.h"


CRBookmarkMenuItem::CRBookmarkMenuItem( CRMenu * menu, int shortcut, CRBookmark * bookmark, int page )
: CRMenuItem(menu, shortcut, lString32(_("Empty slot")), LVImageSourceRef(), LVFontRef() ), _bookmark( bookmark ), _page(page)
{

}

void CRBookmarkMenuItem::Draw( LVDrawBuf & buf, lvRect & rc, CRRectSkinRef skin, CRRectSkinRef valueSkin, bool selected )
{
    _itemDirty = false;
    if ( !_bookmark ) {
        CRMenuItem::Draw( buf, rc, skin, valueSkin, selected );
        return;
    }
    lvRect itemBorders = skin->getBorderWidths();
    skin->draw( buf, rc );
    buf.SetTextColor( 0x000000 );
    buf.SetBackgroundColor( 0xFFFFFF );
    int imgWidth = DrawIcon( buf, rc, itemBorders );

    lvRect textRect = rc;
    textRect.left += imgWidth;
    lvRect posRect = textRect;
    lString32 text = _bookmark->getPosText();
    if ( !text.empty() ) {
        posRect.bottom = posRect.top + skin->getFont()->getHeight() + itemBorders.top + itemBorders.bottom;
        textRect.top = posRect.bottom - itemBorders.bottom;
    }
    lString32 postext(_("Page $1 ($2%)"));
    postext.replaceIntParam(1, _page+1);
    postext.replaceParam(2, lString32::itoa( _bookmark->getPercent()/100 ) << "." << fmt::decimal(_bookmark->getPercent()%100));
    postext << "  " << _bookmark->getTitleText();
    skin->drawText( buf, posRect, postext );
    if ( !text.empty() )
        valueSkin->drawText( buf, textRect, text );
}

void CRBookmarkMenu::setMode( bool goToMode )
{
    //if ( _goToMode==goToMode )
    //    return;
    CRLog::trace("CRBookmarkMenu::setMode");
#ifdef CR_POCKETBOOK
    lString32 selKeyName = getCommandKeyName( MCMD_SELECT );
#else
    lString32 selKeyName = getItemNumberKeysName();
#endif
    lString32 modeKeyName = getCommandKeyName( MCMD_NEXT_MODE );
    bool hasModeSwitch = !modeKeyName.empty();
    _goToMode = goToMode;
    if ( _goToMode ) {
        _caption = lString32(_("Go to bookmark"));
        _label = _caption;
        _statusText = lString32(
                hasModeSwitch
                ? _("Short press $1 - go to bookmark,\n$2 - switch to SET mode")
                : _("Short press $1 - go to bookmark,\nlong press - set bookmark")
                );
    } else {
        _caption = lString32(_("Set bookmark"));
        _label = _caption;
        _statusText = lString32(
                hasModeSwitch
                ? _("$1 - set bookmark,\n$2 - switch to GO mode")
                : _("Short press $1 - set bookmark,\nlong press - go to bookmark")
                );
    }
    _statusText.replaceParam(1, selKeyName);
    _statusText.replaceParam(2, modeKeyName);
    setDirty();
}

/// returns index of selected item, -1 if no item selected
int CRBookmarkMenu::getSelectedItemIndex()
{
    CRFileHistRecord * bookmarks = _docview->getCurrentFileHistRecord();
    int curPage = _docview->getCurPage();
    int n = bookmarks->getLastShortcutBookmark()+1;
    for ( int i=1; i<=n; i++ ) {
        CRBookmark * bm = bookmarks->getShortcutBookmark(i);
        int page = 0;
        if ( bm ) {
            ldomXPointer p = _docview->getDocument()->createXPointer( bm->getStartPos() );
            if ( !p.isNull() ) {
                /// get page number by bookmark
                page = _docview->getBookmarkPage( p );
                /// get bookmark position text
                if ( page>0 && page==curPage )
                    return i-1;
            }
        }
    }
    return -1;
}

#ifdef CR_POCKETBOOK
#include "cr3pocketbook.h"
#include "inkview.h"

static CRBookmarkMenu *bmkDialog = NULL;

static imenu _contextMenu[] = {
	{ITEM_ACTIVE, DCMD_BOOKMARK_SAVE_N, NULL, NULL},
	{ITEM_ACTIVE, DCMD_BOOKMARK_GO_N, NULL, NULL},
	{ITEM_ACTIVE, PB_CMD_BOOKMARK_REMOVE, NULL, NULL},
	{ 0, 0, NULL, NULL }
};

static void handle_contextMenu(int index)
{
	CRLog::trace("CRBookmarkMenu handle_contextMenu(%d)", index);
	bmkDialog->handleContextMenu(index);
}

int CRBookmarkMenu::getDefaultSelectionIndex()
{
	if ( _goToMode )
		return -1;
    CRFileHistRecord * bookmarks = _docview->getCurrentFileHistRecord();
    for ( int i=0; i<_items.length(); i++ ) {
        CRBookmark * bm = bookmarks->getShortcutBookmark(i);
        if ( bm == NULL) 
			return i;
    }
    return -1;
}

void CRBookmarkMenu::showContextMenu()
{
	CRBookmarkMenuItem *item = static_cast<CRBookmarkMenuItem *>(_items[_selectedItem]);
	CRMenuSkinRef skin = getSkin();
	CRRectSkinRef separatorSkin = skin->getSeparatorSkin();
    int separatorHeight = 0;
    if ( !separatorSkin.isNull() )
        separatorHeight = separatorSkin->getMinSize().y;

    lvRect clientRect;
    getClientRect(clientRect);
    lvPoint itemSize = getMaxItemSize();
	_contextMenu[2].type = item->getBookmark() ? ITEM_ACTIVE : ITEM_INACTIVE;
        int y = clientRect.top + (itemSize.y + separatorHeight) * (_selectedItem - _topItem) +
			((itemSize.y + separatorHeight)/4);
	if (_contextMenu[0].text == NULL) {
		_contextMenu[0].text = (char *)_("Set bookmark");
		_contextMenu[1].text = (char *)_("Go to bookmark");
		_contextMenu[2].text = (char *)_("Delete bookmark");
	}
	OpenMenu(_contextMenu, 
		_goToMode ? DCMD_BOOKMARK_GO_N : DCMD_BOOKMARK_SAVE_N,
		ScreenWidth()/4, 
		y, 
		handle_contextMenu);
}

void CRBookmarkMenu::handleContextMenu(int index)
{
    // the command may close the menu, which is deleted then: see CRCitesMenu::handleContextMenu
    CRGUIWindowManager * wm = _wm;
    wm->postCommand(index, 0);
    wm->processPostedEvents();
    wm->resetTillUp();
}

#endif

#define MIN_BOOKMARK_ITEMS 32
CRBookmarkMenu::CRBookmarkMenu(CRGUIWindowManager * wm, LVDocView * docview, int numItems, lvRect & rc, bool goToMode)
    : CRFullScreenMenu( wm, MCMD_BOOKMARK_LIST, lString32(_("Bookmarks")), numItems, rc )
    , _docview(docview)
{
    CRFileHistRecord * bookmarks = docview->getCurrentFileHistRecord();
    CRGUIAcceleratorTableRef acc = _wm->getAccTables().get("bookmarks");
    if ( acc.isNull() )
        acc = _wm->getAccTables().get("menu");
    setAccelerators( acc );
    setSkinName(lString32("#bookmarks"));
    int mc = getSkin()->getMinItemCount();
    if ( _pageItems < mc )
        _pageItems = mc;
    int n = bookmarks->getLastShortcutBookmark()+1;
    n = (n + _pageItems - 1) / _pageItems * _pageItems;
    int minitems = (MIN_BOOKMARK_ITEMS + _pageItems - 1) / _pageItems * _pageItems;
    if ( n<minitems )
        n = minitems;
    for ( int i=1; i<=n; i++ ) {
        CRBookmark * bm = bookmarks->getShortcutBookmark(i);
        int page = 0;
        if ( bm ) {
            ldomXPointer p = docview->getDocument()->createXPointer( bm->getStartPos() );
            if ( !p.isNull() ) {
                /// get page number by bookmark
                page = docview->getBookmarkPage( p );
                /// get bookmark position text
                if ( page<0 )
                    bm = NULL;
            }
        }
        CRBookmarkMenuItem * item = new CRBookmarkMenuItem( this, i, bm, page );
        addItem( item );
    }
    setMode( goToMode );
#ifdef CR_POCKETBOOK
    bmkDialog = this;
#endif
}

/// returns true if command is processed
bool CRBookmarkMenu::onCommand( int command, int params )
{
    if ( command>=MCMD_SELECT_1 && command<=MCMD_SELECT_9 ) {
        int index = command - MCMD_SELECT_1 + 1;
        if ( index >=1 && index <= _pageItems ) {
            index += _topItem;
            int cmd = _goToMode ? DCMD_BOOKMARK_GO_N : DCMD_BOOKMARK_SAVE_N;
            closeMenu( cmd, index );
            return true;
        }
    } else if ( command>=MCMD_SELECT_1_LONG && command<=MCMD_SELECT_9_LONG ) {
        int index = command - MCMD_SELECT_1_LONG + 1;
        if ( index >=1 && index <= _pageItems ) {
            index += _topItem;
            int cmd = _goToMode ? DCMD_BOOKMARK_SAVE_N : DCMD_BOOKMARK_GO_N;
            closeMenu( cmd, index );
            return true;
        }
    } else if ( command==MCMD_NEXT_MODE || command==MCMD_PREV_MODE ) {
        setMode( !_goToMode );
        return true;
    } else if (command == MCMD_SELECT) {
		if (_selectedItem >= 0)
			closeMenu( _goToMode ? DCMD_BOOKMARK_GO_N : DCMD_BOOKMARK_SAVE_N, _selectedItem + 1 );
		return true;
	} else if (command == MCMD_SELECT_LONG) {
#ifdef CR_POCKETBOOK
		if (_selectedItem >= 0)
			showContextMenu();
#else
		if (_selectedItem >= 0)
			closeMenu( _goToMode ? DCMD_BOOKMARK_SAVE_N : DCMD_BOOKMARK_GO_N, _selectedItem + 1 );
#endif
		return true;
	} else if (command == MCMD_PREV_PAGE) {
		if (_topItem == 0) {
			closeMenu(0);
			return true;
		}
	}
#ifdef CR_POCKETBOOK
	 else if (command == DCMD_BOOKMARK_SAVE_N || command == DCMD_BOOKMARK_GO_N) {
		 closeMenu( command, _selectedItem + 1 );
		 return true;
	 } else if (command == PB_CMD_BOOKMARK_REMOVE && _selectedItem >= 0) {
		 CRBookmarkMenuItem *item = static_cast<CRBookmarkMenuItem *>(_items[_selectedItem]);
		 CRBookmark *bm = item->getBookmark();
		 if (bm && _docview->removeBookmark(bm)) {
			 item->setBookmark(NULL);
			 setDirty();
		 }
		 return true;
	 }
#endif	
    else if ((DCMD_BUTTON_PRESSED == command || DCMD_BUTTON_PRESSED_LONG == command) &&
        BTN_CLOSE == params) {
        closeMenu( 0 );
        return true;
    }
    return CRMenu::onCommand(command, params);
    //closeMenu( 0 );
    //return true;
}

#ifdef CR_POCKETBOOK
extern V3DocViewWin * main_win;

static CRCitesMenu *citesDialog = NULL;

// The actions with a citation of the list: shown by a tap on the citation on the devices with a
// touch screen, by a long press of OK on the others. The index of an item is the command the
// list gets.
static imenu _cites_contextMenu[] = {
        {ITEM_ACTIVE, DCMD_BOOKMARK_GO_N, NULL, NULL},
        {ITEM_ACTIVE, MCMD_CITE, NULL, NULL},
        {ITEM_ACTIVE, PB_CMD_BOOKMARK_REMOVE, NULL, NULL},
        { 0, 0, NULL, NULL }
};

static void handle_citesContextMenu(int index)
{
    citesDialog->handleContextMenu(index);
}

void CRCitesMenu::showContextMenu()
{
    CRBookmarkMenuItem *item = static_cast<CRBookmarkMenuItem *>(_items[_selectedItem]);
    if (item->getBookmark() == NULL)
        return;
    CRMenuSkinRef skin = getSkin();
    CRRectSkinRef separatorSkin = skin->getSeparatorSkin();
    int separatorHeight = 0;
    if ( !separatorSkin.isNull() )
        separatorHeight = separatorSkin->getMinSize().y;
    lvRect clientRect;
    getClientRect(clientRect);
    lvPoint itemSize = getMaxItemSize();
    int y = clientRect.top + (itemSize.y + separatorHeight) * (_selectedItem - _topItem) +
                        ((itemSize.y + separatorHeight)/4);
    if (_cites_contextMenu[0].text == NULL) {
        _cites_contextMenu[0].text = (char *)_("Go to citation");
        _cites_contextMenu[1].text = (char *)_("Edit citation");
        _cites_contextMenu[2].text = (char *)_("Delete citation");
    }
    OpenMenu(_cites_contextMenu, DCMD_BOOKMARK_GO_N, ScreenWidth()/4, y,
                handle_citesContextMenu);
}

void CRCitesMenu::handleContextMenu(int index)
{
    // "Go to citation" and "Edit citation" close the list, and the list is deleted while its
    // command is processed: the window manager is taken before that. It was read from the
    // deleted list after it; with "Edit citation", which opens a window in the memory just
    // freed, the program stopped with a segmentation fault.
    CRGUIWindowManager * wm = _wm;
    // index <= 0: the menu was closed without a choice
    if (index > 0) {
        CRLog::info("CRCitesMenu: action %d of the citation menu", index);
        wm->postCommand(index, 0);
        wm->processPostedEvents();
    }
    wm->resetTillUp();
    CRLog::info("CRCitesMenu: action done");
}
#endif

CRCitesMenu::CRCitesMenu(CRGUIWindowManager * wm, LVDocView * docview, int numItems, lvRect & rc)
    : CRFullScreenMenu( wm, MCMD_CITES_LIST, lString32(_("Citations")), numItems, rc )
    , _docview(docview)
{
    CRGUIAcceleratorTableRef acc = _wm->getAccTables().get("bookmarks");
    if ( acc.isNull() )
        acc = _wm->getAccTables().get("menu");
    setAccelerators( acc );
    setSkinName(lString32("#cites-list"), lString32("#bookmarks"));
    int mc = getSkin()->getMinItemCount();
    if ( _pageItems < mc )
        _pageItems = mc;
    // The first row of the list always opens the window of a new citation. Before, that row was
    // shown only while the book had no citations, so a second citation could not be added here.
    createDefaultItem();
    CRFileHistRecord * rec = docview->getCurrentFileHistRecord();
    LVPtrVector < CRBookmark > &bookmarks = rec->getBookmarks();
    for ( int i=0; i < bookmarks.length(); i++ ) {
        CRBookmark * bmk = bookmarks[i];
        if (!bmk || ((bmk->getType() != bmkt_comment && bmk->getType() != bmkt_correction)))
            continue;
        ldomXPointer p = docview->getDocument()->createXPointer( bmk->getStartPos() );
        if ( p.isNull() )
            continue;
        int page = docview->getBookmarkPage( p );
        /// get bookmark position text
        if ( page<0 )
            continue;
        CRBookmarkMenuItem * item = new CRBookmarkMenuItem( this, i, bmk, page );
        addItem( item );
    }
#ifdef CR_POCKETBOOK
    citesDialog = this;
#endif
}

/// returns true if command is processed
bool CRCitesMenu::onCommand( int command, int params )
{
    if ( command>=MCMD_SELECT_1 && command<=MCMD_SELECT_9 ) {
        int index = command - MCMD_SELECT_1 + 1;
        if ( index >=1 && index <= _pageItems ) {
            index += _topItem;
            goToCitePage( index - 1);
            return true;
        }
    } else if (command == MCMD_SELECT) {
        if (_selectedItem >= 0 && _selectedItem < _items.length()) {
#ifdef CR_POCKETBOOK
            // A tap on a citation shows what can be done with it: go to it, edit it, delete it.
            // Before, the tap went to the citation, and the menu was shown only by a long tap,
            // which nothing on the screen tells about.
            CRBookmarkMenuItem *item = static_cast<CRBookmarkMenuItem *>(_items[_selectedItem]);
            if (item->getBookmark() != NULL && QueryTouchpanel() != 0) {
                showContextMenu();
                return true;
            }
#endif
            goToCitePage(_selectedItem );
        }
        return true;
    } else if (command == MCMD_SELECT_LONG) {
#ifdef CR_POCKETBOOK
        if (_selectedItem >= 0 && _selectedItem < _items.length())
            showContextMenu();
#endif
        return true;
    } else if (command == MCMD_PREV_PAGE) {
        if (_topItem == 0) {
            closeMenu(0);
            return true;
        }
    }
#ifdef CR_POCKETBOOK
    else if (command == DCMD_BOOKMARK_GO_N) {
        goToCitePage( _selectedItem );
        return true;
    } else if (command == MCMD_CITE) {
        editCite( _selectedItem );
        return true;
    } else if (command == PB_CMD_BOOKMARK_REMOVE && _selectedItem >= 0 && _selectedItem < _items.length()) {
        CRBookmarkMenuItem *item = static_cast<CRBookmarkMenuItem *>(_items[_selectedItem]);
        CRBookmark *bm = item->getBookmark();
        if (bm && _docview->removeBookmark(bm)) {
            // saved at once, as a new citation is; before, a deleted citation was back after a
            // failure of the program
            if (main_win != NULL)
                main_win->saveHistory(lString32::empty_str);
            item =static_cast<CRBookmarkMenuItem *>(_items.remove(_selectedItem));
            delete item;
            // the first row (new citation) is always there, so the list is never empty
            if (_selectedItem >= _items.length())
                _selectedItem = _items.length() - 1;
            // the last page may have become empty
            if (_topItem >= _items.length())
                _topItem = (_items.length() - 1) / _pageItems * _pageItems;
            invalidateControls();
            setDirty();
            _pageUpdate = true;
        }
        return true;
    }
#endif
    else if ((DCMD_BUTTON_PRESSED == command || DCMD_BUTTON_PRESSED_LONG == command) &&
        BTN_CLOSE == params) {
        closeMenu( 0 );
        return true;
    }
    return CRMenu::onCommand(command, params);
}

void CRCitesMenu::goToCitePage(int selecteditem)
{
    if (selecteditem >= 0 && selecteditem < _items.length()) {
        CRBookmarkMenuItem *item = static_cast<CRBookmarkMenuItem *>(_items[selecteditem]);
        if (item->getBookmark() == NULL)
            closeMenu(MCMD_CITE, CITE_PARAM_FROM_LIST); // the row of a new citation
        else
            closeMenu( DCMD_GO_PAGE, item->getPage() );
    }
}

/// Closes the list and opens the window of citations on a saved citation, to change its text.
void CRCitesMenu::editCite(int selecteditem)
{
    if (selecteditem < 0 || selecteditem >= _items.length())
        return;
    CRBookmark * bmk = static_cast<CRBookmarkMenuItem *>(_items[selecteditem])->getBookmark();
    CRFileHistRecord * rec = _docview->getCurrentFileHistRecord();
    int index = (bmk && rec) ? rec->getBookmarks().indexOf(bmk) : -1;
    if (index >= 0)
        closeMenu(MCMD_CITE, CITE_PARAM_EDIT_BASE + index);
}

int CRCitesMenu::getSelectedItemIndex()
{
    int curPage = _docview->getCurPage();
    for (int i = 0; i < _items.length(); i++) {
        CRBookmarkMenuItem *item = static_cast<CRBookmarkMenuItem *>(_items[i]);
        if (item->getBookmark() != NULL && item->getPage() == curPage)
            return i;
    }
    return -1;
}

void CRCitesMenu::createDefaultItem()
{
    CRBookmarkMenuItem * item = new CRBookmarkMenuItem( this, 0, NULL, 0 );
    item->setLabel(lString32(_("Cite selection dialog")));
    addItem( item );
}
