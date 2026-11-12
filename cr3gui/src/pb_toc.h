//
// C++ Interface: pb_toc
//

#ifndef CR3_PB_TOC_H_INCLUDED
#define CR3_PB_TOC_H_INCLUDED

#include <inkview.h>
#include "mainwnd.h"
#include "settings.h"

#define PB_TOC_SAFE_CMD_RANGE 90000

class V3DocViewWin;

extern lString32 pbSkinFileName;
extern V3DocViewWin * main_win;

// TOC row drawn like a printed contents line: "Chapter 5 .......... 98"; without the page numbers
// a plain row. The chapter of the open page has a triangle mark before its title.
class CRTocMenuItem : public CRMenuItem
{
    private:
        lString32 _pageText;
        bool _withPage;
        bool _current;

    public:
        CRTocMenuItem( CRMenu * menu, int id, const lString32 & label, int page, bool withPage = true,
            bool current = false );

        virtual void Draw( LVDrawBuf & buf, lvRect & rc, CRRectSkinRef skin,
            CRRectSkinRef valueSkin, bool selected );
};

class CRTocMenu : public CRFullScreenMenu
{
    protected:
        CRPropRef props;
        CRGUIAcceleratorTableRef _menuAccelerators;

    public:
        CRTocMenu( CRGUIWindowManager * wm, CRPropRef props, int id,
            CRGUIAcceleratorTableRef menuAccelerators, lvRect & rc,
            tocentry *tocItems, int length, int currentPage );

        virtual bool onCommand( int command, int params );

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
