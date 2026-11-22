//
// C++ Interface: pb_toc
//

#ifndef CR3_PB_TOC_H_INCLUDED
#define CR3_PB_TOC_H_INCLUDED

#include <inkview.h>
#include <vector>
#include "mainwnd.h"
#include "settings.h"
#include "pb_toctree.h"

#define PB_TOC_SAFE_CMD_RANGE 90000

class V3DocViewWin;

extern lString32 pbSkinFileName;
extern V3DocViewWin * main_win;

// TOC row drawn like a printed contents line: "Chapter 5 .......... 98"; without the page numbers
// a plain row. The chapter of the open page has a triangle mark before its title; an entry with
// entries under it has the hollow triangle of its branch before that (pb_toctree.h).
class CRTocMenuItem : public CRMenuItem
{
    private:
        lString32 _pageText;
        bool _withPage;
        bool _current;
        int _branch;        // -1 none, 0 no branch under it (room kept), 1 closed, 2 open
        int _entry;         // the entry of the contents
        lvRect _toggleRect; // where the mark of the branch was drawn, screen coordinates

    public:
        CRTocMenuItem( CRMenu * menu, int id, const lString32 & label, int page, bool withPage = true,
            bool current = false, int branch = -1, int entry = -1 );

        virtual void Draw( LVDrawBuf & buf, lvRect & rc, CRRectSkinRef skin,
            CRRectSkinRef valueSkin, bool selected );
        /// the entry whose branch mark is at the point, -1 for none
        int toggleAt( const lvPoint & pt ) const { return _branch > 0 && _toggleRect.isPointInside( pt ) ? _entry : -1; }
};

class CRTocMenu : public CRFullScreenMenu
{
    protected:
        CRPropRef props;
        CRGUIAcceleratorTableRef _menuAccelerators;
        struct Entry {
            int level, page, position;
            lString32 text;
        };
        std::vector<Entry> _entries;
        PbTocTree _tree;
        int _current;       // the entry of the page read
        bool _showPages;
        /// the rows of the entries seen
        void fillItems();

    public:
        CRTocMenu( CRGUIWindowManager * wm, CRPropRef props, int id,
            CRGUIAcceleratorTableRef menuAccelerators, lvRect & rc,
            tocentry *tocItems, int length, int currentPage );

        virtual bool onCommand( int command, int params );
        /// a tap on the mark of a branch opens or closes it; the rest of a row goes to its page
        virtual bool onTouchEvent( int x, int y, CRGUITouchEventType evType );

        virtual ~CRTocMenu()
        {
            forcePartialBwUpdates = false;
            CRLog::trace("Calling fontMan->gc() on Toc menu destroy");
            fontMan->gc();
            CRLog::trace("Done fontMan->gc() on Toc menu destroy");
        }

    private:
        int _menuItemId;

};

#endif //CR3_PB_TOC_H_INCLUDED
