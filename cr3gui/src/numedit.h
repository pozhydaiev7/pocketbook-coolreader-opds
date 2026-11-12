//
// C++ Interface: number editor dialog
//
// Description: 
//
//
// Author: Vadim Lopatin <vadim.lopatin@coolreader.org>, (C) 2008
//
// Copyright: See COPYING file that comes with this distribution
//
// numedit.h

#ifndef NUMEDIT_H_INCLUDED
#define NUMEDIT_H_INCLUDED


#include "mainwnd.h"

class CRNumberEditDialog : public CRGUIWindowBase
{
    protected:
        lString32 _title;
        lString32 _value;
        int _minvalue;
        int _maxvalue;
        int _resultCmd;
        CRMenuSkinRef _skin;
        virtual void draw();
    public:
        CRNumberEditDialog( CRGUIWindowManager * wm, lString32 title, lString32 initialValue, int resultCmd, int minvalue, int maxvalue );
        virtual ~CRNumberEditDialog()
        {
        }
        bool digitEntered( lChar32 c );
        /// returns true if command is processed
        virtual bool onCommand( int command, int params );
};


#endif
