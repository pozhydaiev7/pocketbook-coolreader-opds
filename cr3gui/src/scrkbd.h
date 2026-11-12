//
// C++ Interface: on-screen keyboard
//
// Description: 
//
//
// Author: Vadim Lopatin <vadim.lopatin@coolreader.org>, (C) 2008
//
// Copyright: See COPYING file that comes with this distribution
//
// scrkbd.h

#ifndef SCRKBD_H_INCLUDED
#define SCRKBD_H_INCLUDED

#include "mainwnd.h"


class CRScreenKeyboard : public CRGUIWindowBase
{
protected:
    lString32 & _buffer;
    lString32 _value;
    CRMenuSkinRef _skin;
    lString32 _title;
    int _resultCmd;
    lChar32 _lastDigit;
    int _rows;
    int _cols;
    lString32Collection _keymap;
    virtual void draw();
    virtual lChar32 digitsToChar( lChar32 digit1, lChar32 digit2 );
    bool digitEntered( lChar32 c );
public:
	void setDefaultLayout();
	void setLayout( CRKeyboardLayoutRef layout );
    CRScreenKeyboard(CRGUIWindowManager * wm, int id, const lString32 & caption, lString32 & buffer, lvRect & rc);

    virtual ~CRScreenKeyboard() { }

    //virtual const lvRect & getRect();

    //virtual lvPoint getSize();

    /// returns true if command is processed
    virtual bool onCommand( int command, int params );

};


#endif
