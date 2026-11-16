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
#include <sys/time.h>
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
    CiteToolBar(CRGUIWindowBase *window, lString32 id, CRToolBarSkinRef tbskin, bool active = true)
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
            addButton(new CRToolButton(this, i, lString32(), DCMD_BUTTON_PRESSED));
        }
    }
#ifdef POCKETBOOK_PRO
    // The role of a button, for every skin:
    //   - a skin that lists the buttons in the [cite-toolbar] section of its keymaps.ini
    //     (pb618fw5, pb631fw5, pb700fw5, pb740fw5, pbXfw5) gives every button a command, and
    //     the command says what the button is: MCMD_CANCEL, MCMD_OK, MCMD_SCROLL_FORWARD (the
    //     Down arrow), MCMD_SCROLL_BACK (the Up arrow); the buttons with MCMD_SELECT are, in
    //     their order, A, A+, A-, C+, C-. The place of a button in such a toolbar does not matter;
    //   - the default buttons (pb62x, pb626fw5: no such section) have their roles by place.
    // Before, the role was taken by the id of the button, which is its place for the default
    // buttons only; the listed ones have the ids 1, 2, ...: every button had the role of its
    // right neighbour, and the Up arrow saved the citation.
    CiteButtonRole roleOf(int index)
    {
        if (index < 0 || index >= m_buttons.length())
            return CITE_ROLE_NONE;
        int command = m_buttons[index]->getCommand();
        if (command != DCMD_BUTTON_PRESSED) {
            static const CiteButtonRole selectRoles[] = {
                CITE_ROLE_MOVE, CITE_ROLE_GROW, CITE_ROLE_SHRINK, CITE_ROLE_GROW_PHRASE, CITE_ROLE_SHRINK_PHRASE
            };
            switch (command) {
            case MCMD_CANCEL:
                return CITE_ROLE_CANCEL;
            case MCMD_OK:
                return CITE_ROLE_SAVE;
            case MCMD_SCROLL_FORWARD:
                return CITE_ROLE_DOWN;
            case MCMD_SCROLL_BACK:
                return CITE_ROLE_UP;
            case MCMD_SELECT: {
                int n = 0;
                for (int i = 0; i < index; i++)
                    if (m_buttons[i]->getCommand() == MCMD_SELECT)
                        n++;
                return n < 5 ? selectRoles[n] : CITE_ROLE_NONE;
            }
            default:
                return CITE_ROLE_NONE;
            }
        }
        if (m_buttons.length() == 9)
            return citeLayout9[index];
        if (index < 7)
            return citeLayout7[index];
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
    // The saved citation that is being edited, NULL for a new citation. It stays in the bookmarks
    // of the book; only its highlight in the text is hidden while the window is open, so that
    // the selection alone shows what will be saved.
    CRBookmark * _edited;
    // The window was opened from the list of citations: when it closes, the page that was open
    // is restored and the list is shown again.
    bool _returnToList;
    ldomXPointer _returnPos;

    // Opens the saved citation number `index` of the bookmarks of the book for editing.
    // If its text is not found in the book, the window stays a window of a new citation.
    void startEditing( int index )
    {
        LVDocView * docview = mainwin_->getDocView();
        CRFileHistRecord * rec = docview->getCurrentFileHistRecord();
        if ( !rec || index < 0 || index >= rec->getBookmarks().length() )
            return;
        CRBookmark * bmk = rec->getBookmarks()[index];
        ldomXPointerEx start = docview->getDocument()->createXPointer( bmk->getStartPos() );
        ldomXPointerEx end = docview->getDocument()->createXPointer( bmk->getEndPos() );
        if ( start.isNull() || end.isNull() ) {
            CRLog::info("Cite: the text of citation %d is not found, a new citation instead", index);
            return;
        }
        CRLog::info("Cite: editing citation %d", index);
        _edited = bmk;
        docview->setHiddenBookmark( bmk );
        selector_.setRange( start, end );
    }
#ifdef CR_POCKETBOOK
    bool _noClose;

    // ---- Selection of the text by touch, on the page above the toolbar.
    //   a drag from a word to a word    selects the text between them, the two words included;
    //                                   the selection follows the finger, redrawn at most every
    //                                   TOUCH_REDRAW_MS
    //   a tap on a word                 moves a bound of the selection to it: a word before the
    //                                   selection becomes its first word, a word inside or after
    //                                   it becomes its last word
    //   a long tap on a word            selects that word alone (the window manager drops the
    //                                   rest of a touch after its long tap)
    // The buttons of the toolbar work as before, on the selection made either way.
    enum { TOUCH_REDRAW_MS = 400 };
    bool _touchSelecting;       // a touch that started on the page is going on
    bool _touchDragged;         // it has moved far enough to be a drag
    bool _touchAnchorValid;     // there was a word under the finger when it went down
    bool _touchLastValid;       // _touchLastStart, _touchLastEnd are set
    lvPoint _touchDownPt;
    ldomXPointerEx _touchAnchorStart;   // the word the drag started at
    ldomXPointerEx _touchAnchorEnd;
    ldomXPointerEx _touchLastStart;     // the word the selection was last drawn up to
    ldomXPointerEx _touchLastEnd;
    long long _touchLastRedrawMs;

    static long long touchNowMs()
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    }

    // The page is drawn squeezed into the part of the screen above the toolbar
    // (BackgroundFitWindow::draw). A point of the screen gives the point of the page image.
    lvPoint toPagePoint(lvPoint pt)
    {
        lvRect full = _wm->getScreen()->getRect();
        int shown = _rect.top - full.top;
        if (shown > 0)
            pt.y = full.top + (full.height() * (pt.y - full.top)) / shown + 1;
        return pt;
    }

    // Selects from the word the drag started at to the word under the finger.
    // Returns true if the selection has changed.
    bool dragSelectionTo(lvPoint pt)
    {
        ldomXPointerEx wordStart, wordEnd;
        if (!selector_.wordAt(toPagePoint(pt), wordStart, wordEnd))
            return false;
        if (!_touchAnchorValid) {
            // the finger went down beside the text: the drag starts at the first word it reaches
            _touchAnchorStart = wordStart;
            _touchAnchorEnd = wordEnd;
            _touchAnchorValid = true;
        }
        if (_touchLastValid && wordStart.compare(_touchLastStart) == 0 && wordEnd.compare(_touchLastEnd) == 0)
            return false;
        _touchLastStart = wordStart;
        _touchLastEnd = wordEnd;
        _touchLastValid = true;
        selector_.selectWords(_touchAnchorStart, _touchAnchorEnd, wordStart, wordEnd);
        _touchLastRedrawMs = touchNowMs();
        setDirty();
        return true;
    }

    // A touch on the page above the toolbar. Returns true if the screen is to be redrawn.
    bool onPageTouch(lvPoint pt, CRGUITouchEventType evType)
    {
        switch (evType) {
        case CRTOUCH_DOWN:
            _touchSelecting = true;
            _touchDragged = false;
            _touchLastValid = false;
            _touchDownPt = pt;
            _touchAnchorValid = selector_.wordAt(toPagePoint(pt), _touchAnchorStart, _touchAnchorEnd);
            return false;
        case CRTOUCH_MOVE:
            if (!_touchSelecting)
                return false;
            if (!_touchDragged) {
                int limit = _wm->getScreen()->getWidth() / 40;
                int dx = pt.x - _touchDownPt.x;
                int dy = pt.y - _touchDownPt.y;
                if (dx > -limit && dx < limit && dy > -limit && dy < limit)
                    return false;
                _touchDragged = true;
            }
            if (touchNowMs() - _touchLastRedrawMs < TOUCH_REDRAW_MS)
                return false;
            return dragSelectionTo(pt);
        case CRTOUCH_DOWN_LONG: {
            _touchSelecting = false;
            ldomXPointerEx wordStart, wordEnd;
            if (!selector_.wordAt(toPagePoint(pt), wordStart, wordEnd))
                return false;
            selector_.selectWords(wordStart, wordEnd, wordStart, wordEnd);
            setDirty();
            return true;
        }
        case CRTOUCH_UP: {
            if (!_touchSelecting)
                return false;
            _touchSelecting = false;
            if (_touchDragged)
                return dragSelectionTo(pt);
            ldomXPointerEx wordStart, wordEnd;
            if (!selector_.wordAt(toPagePoint(pt), wordStart, wordEnd))
                return false;
            selector_.moveBoundTo(wordStart, wordEnd);
            setDirty();
            return true;
        }
        default:
            return false;
        }
    }

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
            // A, A+, A-, C+, C-: the button is the action the arrows apply from now on, and the
            // action is done once at the end of the selection, so that every tap is seen.
            // Before, such a tap only chose the action and nothing changed on the page until an
            // arrow was tapped; reported as "the buttons do nothing".
            //   A    whole paragraphs of the selection; if it is whole paragraphs already,
            //        the next paragraph
            //   A+   the next paragraph is added          A-   the last paragraph is removed
            //   C+   the next sentence is added           C-   the last sentence is removed
            // An arrow then moves a bound of the selection in its direction: with A+ and C+ Down
            // extends the end and Up the start; with A- and C- Up pulls the end back and Down
            // the start; with A they select the next or the previous paragraph.
            _action = role;
            switch (role) {
            case CITE_ROLE_MOVE:
                if (!selector_.selectWholeParagraphs())
                    selector_.moveDown();
                break;
            case CITE_ROLE_GROW:
                selector_.growDown();
                break;
            case CITE_ROLE_SHRINK:
                selector_.shrinkUp();
                break;
            case CITE_ROLE_GROW_PHRASE:
                selector_.growDownPhrase();
                break;
            case CITE_ROLE_SHRINK_PHRASE:
                selector_.shrinkUpPhrase();
                break;
            default:
                break;
            }
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
            lString32 prompt;
            int index = getSelectedIndex();
            switch (index) {
            case 0:
                prompt = lString32(_("Select next/prev paragraph"));
                break;
            case 1:
                prompt = lString32(_("Select one more paragraph"));
                break;
            case 2:
                prompt = lString32(_("Deselect paragraph"));
                break;
            case 3:
                prompt = lString32(_("Select one more phrase"));
                break;
            case 4:
                prompt = lString32(_("Deselect phrase"));
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
        lString32 prompt(_("Select text"));
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
    /// mode: the parameter of MCMD_CITE, see citedlg.h
    CiteWindow( CRGUIWindowManager * wm, V3DocViewWin * mainwin, int mode) :
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
        _edited = NULL;
        _returnToList = mode >= CITE_PARAM_FROM_LIST;
        if ( _returnToList )
            _returnPos = mainwin->getDocView()->getBookmark();
        if ( mode >= CITE_PARAM_EDIT_BASE )
            startEditing( mode - CITE_PARAM_EDIT_BASE );
        selector_.highlight();
        setDirty();
        setSkinName(U"#cite-dialog");
        CRToolBarSkinRef tbSkin;
        CRWindowSkinRef windowSkin = getSkin();
        if ( !windowSkin.isNull() )
            tbSkin = windowSkin->getToolBar1Skin();
        isDefaultToolBar = false;
        if ( !tbSkin.isNull()) {
            toolbar = new CiteToolBar(this, cs32("cite-toolbar"), tbSkin);
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
      _touchSelecting = _touchDragged = _touchAnchorValid = _touchLastValid = false;
      _touchLastRedrawMs = 0;
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
        // MCMD_SELECT: the command the 9-button skins give to the buttons A, A+, A-, C+, C- in
        // the [cite-toolbar] section of their keymaps.ini. It was not handled, so those buttons
        // did nothing and the arrows always moved the selection by a paragraph.
        case MCMD_SELECT:
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
#ifdef POCKETBOOK_PRO
            // An arrow button was tapped and became the current button: the button of the
            // action the arrows apply is made current again, so that it stays highlighted.
            if (toolbar != NULL) {
                CiteButtonRole role = toolbar->roleOf(toolbar->getCurrentButtonIndex());
                int actionIndex = toolbar->indexOfRole(_action);
                if ((role == CITE_ROLE_UP || role == CITE_ROLE_DOWN) && actionIndex >= 0) {
                    toolbar->selectButton(actionIndex);
                    setDirty();
                }
            }
#endif
        }
#endif
            break;
        case MCMD_OK:
        {
            ldomXRange range;
            selector_.getRange(range);
            // a selection without text is not saved
            if ( !range.isNull() && range.getStart().compare( range.getEnd() ) < 0 ) {
                if ( _edited )
                    mainwin_->getDocView()->updateRangeBookmark( _edited, range );
                else
                    mainwin_->getDocView()->saveRangeBookmark( range, bmkt_comment, lString32::empty_str);
                mainwin_->saveHistory(lString32::empty_str);
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
    /// The whole screen belongs to the window: the toolbar, and the page above it, where the
    /// text is selected. Before, a touch on the page went to the window of the book under it.
    virtual bool onTouch( int x, int y, CRGUITouchEventType evType )
    {
        CRGUIEvent * event = new CRGUITouchEvent( x, y, evType );
        event->setTargetWindow( this );
        _wm->postEvent( event );
        return true;
    }

    virtual bool onTouchEvent( int x, int y, CRGUITouchEventType evType )
    {
        lvPoint pt( x, y );
        // a drag that started on the page goes on over the toolbar too
        if ( !_touchSelecting && _rect.isPointInside( pt ) )
            return BackgroundFitWindow::onTouchEvent( x, y, evType );
        return onPageTouch( pt, evType );
    }

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
        LVDocView * docview = _mainwin->getDocView();
        docview->clearSelection();
        docview->setHiddenBookmark( NULL );
        if ( _returnToList && !_returnPos.isNull() )
            docview->goToBookmark( _returnPos );
        docview->updateBookMarksRanges();
        // the window is deleted by closeWindow()
        CRGUIWindowManager * wm = _wm;
        bool returnToList = _returnToList;
        wm->closeWindow(this);
        if ( returnToList )
            wm->postCommand( MCMD_CITES_LIST, 0 );
    }

protected:
    CiteWindow(const CiteWindow&); //non-copyable
};

void activate_cite( CRGUIWindowManager *wm, V3DocViewWin * mainwin, int mode)
{
    CRLog::info("Entering cite mode %d\n", mode);
    wm->activateWindow(new CiteWindow(wm, mainwin, mode));
}
