//
// C++ Interface: fullscreen menu dialog
//
// Description: 
//
//
// Author: Vadim Lopatin <vadim.lopatin@coolreader.org>, (C) 2008
//
// Copyright: See COPYING file that comes with this distribution
//
// fsmenu.h

#ifndef FSMENU_H_INCLUDED
#define FSMENU_H_INCLUDED

#include <crgui.h>

class CRFullScreenMenu : public CRMenu
{
public:
    CRFullScreenMenu(CRGUIWindowManager * wm, int id, const lString32 & caption, int numItems, lvRect & rc);

    virtual const lvRect & getRect();

    virtual lvPoint getMaxItemSize();

    virtual lvPoint getSize();

    virtual lString32 getItemNumberKeysName();

    virtual lString32 getCommandKeyName( int cmd, int param=0 );

    //virtual void Draw( LVDrawBuf & buf, int x, int y );

};


#endif
