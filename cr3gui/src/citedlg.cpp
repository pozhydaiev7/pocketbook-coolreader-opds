#include <lvstring.h>
#include <lvref.h>
#include <lvarray.h>
#include <lvtinydom.h>
#include <lvdocview.h>

#include <crgui.h>
#include <crtrace.h>
#include <cri18n.h>

#include "mainwnd.h"
#include "bgfit.h"
#include "citedlg.h"
#include "citecore.h"
#ifdef CR_POCKETBOOK
#include "cr3pocketbook.h"
#endif

#ifdef POCKETBOOK_PRO
    #define CITETOOLBAR_BUTTONS 7
#else
    #define CITETOOLBAR_BUTTONS 5
#endif

#ifdef POCKETBOOK_PRO
// Roles of the cite toolbar buttons on touch devices.
enum CiteButtonRole {
    CITE_ROLE_SAVE,
    CITE_ROLE_MOVE,          // "A": select next/previous paragraph
    CITE_ROLE_GROW,          // "A+"
    CITE_ROLE_SHRINK,        // "A-"
    CITE_ROLE_GROW_PHRASE,   // "C+"
    CITE_ROLE_SHRINK_PHRASE, // "C-"
    CITE_ROLE_CANCEL,
    CITE_ROLE_DOWN,          // apply the selected action downwards (same as the "next" key)
    CITE_ROLE_UP,            // apply the selected action upwards (same as the "prev" key)
    CITE_ROLE_NONE
};

// Original 7-button skins (pb626fw5, pb631fw5): Save, A, A+, A-, C+, C-, Cancel.
static const CiteButtonRole citeLayout7[] = {
    CITE_ROLE_SAVE, CITE_ROLE_MOVE, CITE_ROLE_GROW, CITE_ROLE_SHRINK,
    CITE_ROLE_GROW_PHRASE, CITE_ROLE_SHRINK_PHRASE, CITE_ROLE_CANCEL
};
// 9-button skins from 4pda (pb740fw5): Cancel, A, A+, A-, C+, C-, Down, Up, Save.
static const CiteButtonRole citeLayout9[] = {
    CITE_ROLE_CANCEL, CITE_ROLE_MOVE, CITE_ROLE_GROW, CITE_ROLE_SHRINK,
    CITE_ROLE_GROW_PHRASE, CITE_ROLE_SHRINK_PHRASE, CITE_ROLE_DOWN, CITE_ROLE_UP, CITE_ROLE_SAVE
};
#endif

class CiteToolBar : public CRToolBar
{
    int _defaultButtons;
public:
    CiteToolBar(CRGUIWindowBase *window, lString16 id, CRToolBarSkinRef tbskin, bool active = true)
        :  CRToolBar(window, id, tbskin, active), _defaultButtons(CITETOOLBAR_BUTTONS)
    {
#ifdef POCKETBOOK_PRO
        // Take the button count from the skin, so 9-button skins get Up/Down buttons.
        if (!tbskin.isNull() && !tbskin->getButtons().isNull() && tbskin->getButtons()->length() == 9)
            _defaultButtons = 9;
#endif
        init();
    }
    virtual ~CiteToolBar()
    {}
    virtual void initDefault()
    {
        for (int i=0; i<_defaultButtons; i++) {
            addButton(new CRToolButton(this, i, lString16(), DCMD_BUTTON_PRESSED));
        }
    }
#ifdef POCKETBOOK_PRO
    CiteButtonRole roleOf(int index)
    {
        if (index < 0 || index >= m_buttons.length())
            return CITE_ROLE_NONE;
        int id = m_buttons[index]->getId();
        if (m_buttons.length() == 9 && id >= 0 && id < 9)
            return citeLayout9[id];
        if (id >= 0 && id < 7)
            return citeLayout7[id];
        return CITE_ROLE_NONE;
    }
    int indexOfRole(CiteButtonRole role)
    {
        for (int i = 0; i < m_buttons.length(); i++)
            if (roleOf(i) == role)
                return i;
        return -1;
    }
#endif
    bool isDefault()
    {
        bool ret = (5 == m_buttons.length());

        for ( int i=0; i<m_buttons.length() && ret==true; i++) {
            CRToolButton *current = m_buttons[i];
            if (current->getCommand() != DCMD_BUTTON_PRESSED) {
                ret = false;
            }
        }
        return ret;
    }
};

class CiteWindow : public BackgroundFitWindow
{
    CiteSelection selector_;
    V3DocViewWin * mainwin_;
    CiteToolBar *toolbar;
    lvRect toolbarRect;
    bool isDefaultToolBar;
#ifdef CR_POCKETBOOK
    bool _noClose;

    int getSelectedIndex()
    {
        if ( NULL == toolbar )
            return -1;
        CRToolButton *button = toolbar->getCurrentButton();
        if (NULL != button && DCMD_BUTTON_PRESSED == button->getCommand() )
            #ifdef POCKETBOOK_PRO
                return button->getId();
            #else
                return button->getParam();
            #endif
        return -1;
    }

    int getCommandFromIndex(int command)
    {
        if ( NULL == toolbar )
            return -1;
#ifdef POCKETBOOK_PRO
        // Up/Down buttons (and an unknown button) apply the last selected action.
        CiteButtonRole role = toolbar->roleOf(toolbar->getCurrentButtonIndex());
        if (role == CITE_ROLE_UP || role == CITE_ROLE_DOWN || role == CITE_ROLE_NONE)
            role = _action;
        return roleCommand(role, command);
#else
        int index = getSelectedIndex();
        switch (index) {

            case 0:
                //move
                if (command == MCMD_SCROLL_BACK)
                    return MCMD_SELECT_5; //up
                else if (command == MCMD_SCROLL_FORWARD)
                    return MCMD_SELECT_6; //down
                break;
            case 1:
                //grow
                if (command == MCMD_SCROLL_BACK)
                    return MCMD_SELECT_1; //up
                else if (command == MCMD_SCROLL_FORWARD)
                    return MCMD_SELECT_0; //down
                break;
            case 2:
                //shrink
                if (command == MCMD_SCROLL_BACK)
                    return MCMD_SELECT_9;
                else if (command == MCMD_SCROLL_FORWARD)
                    return MCMD_SELECT_2;
                break;
            case 3:
                // grow phrase:
                if (command == MCMD_SCROLL_BACK)
                    return MCMD_SELECT_3; // up
                else if (command == MCMD_SCROLL_FORWARD)
                    return MCMD_SELECT_8; // down
            case 4:
                // Shrink phrase
                if (command == MCMD_SCROLL_BACK)
                    return MCMD_SELECT_7;
                else if (command == MCMD_SCROLL_FORWARD)
                    return MCMD_SELECT_4;
                break;

        default:
            break;
        }
        return -1;
#endif
    }
#ifdef POCKETBOOK_PRO
    CiteButtonRole _action; // action applied by the Up/Down buttons and the prev/next keys

    static int roleCommand(CiteButtonRole role, int command)
    {
        bool up = (command == MCMD_SCROLL_BACK);
        switch (role) {
        case CITE_ROLE_SAVE:          return MCMD_OK;
        case CITE_ROLE_CANCEL:        return MCMD_CANCEL;
        case CITE_ROLE_MOVE:          return up ? MCMD_SELECT_5 : MCMD_SELECT_6;
        case CITE_ROLE_GROW:          return up ? MCMD_SELECT_1 : MCMD_SELECT_0;
        case CITE_ROLE_SHRINK:        return up ? MCMD_SELECT_9 : MCMD_SELECT_2;
        case CITE_ROLE_GROW_PHRASE:   return up ? MCMD_SELECT_3 : MCMD_SELECT_8;
        case CITE_ROLE_SHRINK_PHRASE: return up ? MCMD_SELECT_7 : MCMD_SELECT_4;
        default:                      return -1;
        }
    }

    // A toolbar button was tapped (it is already the current button).
    void onButtonTapped()
    {
        CiteButtonRole role = toolbar ? toolbar->roleOf(toolbar->getCurrentButtonIndex()) : CITE_ROLE_NONE;
        switch (role) {
        case CITE_ROLE_SAVE:
            _wm->postCommand(MCMD_OK, 0);
            break;
        case CITE_ROLE_CANCEL:
            _wm->postCommand(MCMD_CANCEL, 0);
            break;
        case CITE_ROLE_UP:
            _wm->postCommand(roleCommand(_action, MCMD_SCROLL_BACK), 0);
            break;
        case CITE_ROLE_DOWN:
            _wm->postCommand(roleCommand(_action, MCMD_SCROLL_FORWARD), 0);
            break;
        case CITE_ROLE_NONE:
            break;
        default:
            _action = role; // A, A+, A-, C+, C-: choose the action
            setDirty();
            break;
        }
    }
#endif
#endif
protected:
    virtual void draw()
    {
        BackgroundFitWindow::draw();
        CRWindowSkinRef windowSkin = getSkin();
        if ( windowSkin.isNull() )
            return;
        CRRectSkinRef skin = windowSkin->getClientSkin();
        LVDrawBuf * buf = _wm->getScreen()->getCanvas().get();
        skin->draw( *buf, _rect );
#ifdef CR_POCKETBOOK
        lvRect borders = skin->getBorderWidths();
        lvRect keyRect;
        if ( isDefaultToolBar) {
            lString16 prompt;
            int index = getSelectedIndex();
            switch (index) {
            case 0:
                prompt = lString16(_("Select next/prev paragraph"));
                break;
            case 1:
                prompt = lString16(_("Select one more paragraph"));
                break;
            case 2:
                prompt = lString16(_("Deselect paragraph"));
                break;
            case 3:
                prompt = lString16(_("Select one more phrase"));
                break;
            case 4:
                prompt = lString16(_("Deselect phrase"));
                break;
            }
            int promptWidth = skin->measureText(prompt).x;
            keyRect = _rect;
            keyRect.right = keyRect.left + promptWidth + borders.left + borders.right;
            if ( !keyRect.isEmpty() ) {
                skin->draw( *_wm->getScreen()->getCanvas(), keyRect );
                skin->drawText( *_wm->getScreen()->getCanvas(), keyRect, prompt );
            }
        }
        if (toolbar != NULL)
            toolbar->draw(*_wm->getScreen()->getCanvas(), toolbarRect);
#else
        lString16 prompt(_("Select text"));
        buf->FillRect( _rect, 0xAAAAAA );
        lvRect keyRect = _rect;
        LVFontRef font = fontMan->GetFont( 20, 600, false, css_ff_sans_serif, lString8("Arial")); //skin->getFont();
        //        int margin = 4;
        keyRect.right = _rect.right;
        if ( !keyRect.isEmpty() ) {
            skin->draw( *_wm->getScreen()->getCanvas(), keyRect );
            skin->drawText( *_wm->getScreen()->getCanvas(), keyRect, prompt );
        }
#endif
    }

public:
    CiteWindow( CRGUIWindowManager * wm, V3DocViewWin * mainwin) :
        BackgroundFitWindow(wm, mainwin),
        selector_(*mainwin->getDocView()),
        mainwin_(mainwin)
    {
        CRGUIAcceleratorTableRef acc = _wm->getAccTables().get("cite");
        if ( acc.isNull() )
            this->setAccelerators( mainwin->getDialogAccelerators() );
        else
            setAccelerators( acc );
        _rect = _wm->getScreen()->getRect();
        selector_.highlight();
        setDirty();
        setSkinName(L"#cite-dialog");
        CRToolBarSkinRef tbSkin;
        CRWindowSkinRef windowSkin = getSkin();
        if ( !windowSkin.isNull() )
            tbSkin = windowSkin->getToolBar1Skin();
        isDefaultToolBar = false;
        if ( !tbSkin.isNull()) {
            toolbar = new CiteToolBar(this, cs16("cite-toolbar"), tbSkin);
            isDefaultToolBar = toolbar->isDefault();
            int index = toolbar->findButton(DCMD_BUTTON_PRESSED, 0);
            if (index > 0)
                toolbar->selectButton(index);
            toolbar->selectNextButton();
#ifdef POCKETBOOK_PRO
            // Start with "A" (select paragraph) selected, as described for the 4pda build.
            int moveIndex = toolbar->indexOfRole(CITE_ROLE_MOVE);
            if (moveIndex >= 0)
                toolbar->selectButton(moveIndex);
#endif

            toolbar->getRect(toolbarRect);
            _rect.top = _rect.bottom - toolbarRect.height();
        } else {
            toolbar = NULL;
            _rect.top = _rect.bottom;
        }
#ifdef CR_POCKETBOOK
      _noClose= true;
#endif
#ifdef POCKETBOOK_PRO
      _action = CITE_ROLE_MOVE;
#endif
    }

#ifdef POCKETBOOK_PRO
    // Keeps the Up/Down action in sync when the current button is changed with the keys.
    void syncActionWithCurrentButton()
    {
        CiteButtonRole role = toolbar ? toolbar->roleOf(toolbar->getCurrentButtonIndex()) : CITE_ROLE_NONE;
        if (role >= CITE_ROLE_MOVE && role <= CITE_ROLE_SHRINK_PHRASE)
            _action = role;
    }
#endif

    bool onCommand( int command, int params )
    {
        switch ( command ) {
#ifdef POCKETBOOK_PRO
        case DCMD_BUTTON_PRESSED:
            onButtonTapped();
            break;
#endif
        case MCMD_SELECT_1:
            selector_.growUp();
            setDirty();
            break;
        case MCMD_SELECT_2:
            selector_.shrinkDown();
            setDirty();
            break;
        case MCMD_SELECT_5:
            selector_.moveUp();
            setDirty();
            break;
        case MCMD_SELECT_6:
            selector_.moveDown();
            setDirty();
            break;
        case MCMD_SELECT_3:
            selector_.growUpPhrase();
            setDirty();
            break;
        case MCMD_SELECT_4:
            selector_.shrinkDownPhrase();
            setDirty();
            break;
        case MCMD_SELECT_7:
            selector_.shrinkUpPhrase();
            setDirty();
            break;
        case MCMD_SELECT_8:
            selector_.growDownPhrase();
            setDirty();
            break;
        case MCMD_SELECT_9:
            selector_.shrinkUp();
            setDirty();
            break;
        case MCMD_SELECT_0:
            selector_.growDown();
            setDirty();
            break;
        case MCMD_SCROLL_FORWARD:
        case MCMD_SCROLL_BACK:
#ifdef CR_POCKETBOOK
        {
            int cmd = getCommandFromIndex(command);
            if (cmd != -1)
                _wm->postCommand(cmd, params);
        }
#endif
            break;
        case MCMD_OK:
        {
            ldomXRange range;
            selector_.getRange(range);
            if ( !range.isNull() ) {
                mainwin_->getDocView()->saveRangeBookmark( range, bmkt_comment, lString16::empty_str);
                mainwin_->saveHistory(lString16::empty_str);
            }
            close();
        };
            break;
        case MCMD_CANCEL:
            close();
            break;
#ifdef CR_POCKETBOOK
        case PB_CMD_RIGHT:
            if (toolbar && toolbar->selectNextButton())
                setDirty();
#ifdef POCKETBOOK_PRO
            syncActionWithCurrentButton();
#endif
            break;
        case PB_CMD_LEFT:
            if (toolbar && toolbar->selectPrevButton())
                setDirty();
#ifdef POCKETBOOK_PRO
            syncActionWithCurrentButton();
#endif
            break;
#endif
        }
        return true;
    }

#ifdef CR_POCKETBOOK
    virtual void covered() {
        if ( _noClose ) {
            CRGUIWindowManager * wm = 0;
            if ( this && ( wm = mainwin_->getWindowManager() ) )
                wm->activateWindow( this );
            else
                close();
        }
    }
#endif

    void close() {
        _noClose= false;
        CRLog::info("Closing cite");
        _mainwin->getDocView()->clearSelection();
        _mainwin->getDocView()->updateBookMarksRanges();
        _wm->closeWindow(this);
    }

protected:
    CiteWindow(const CiteWindow&); //non-copyable
};

void activate_cite( CRGUIWindowManager *wm, V3DocViewWin * mainwin)
{
    CRLog::info("Entering cite mode\n");
    wm->activateWindow(new CiteWindow(wm, mainwin));
}
