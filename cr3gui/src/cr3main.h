//
// C++ Interface: settings
//
// Description: 
//
//
// Author: Vadim Lopatin <vadim.lopatin@coolreader.org>, (C) 2008
//
// Copyright: See COPYING file that comes with this distribution
//
//
#ifndef CR3MAIN_H_INCLUDED
#define CR3MAIN_H_INCLUDED


#ifndef CRSKIN
#define CRSKIN "/usr/share/crengine"
#endif

bool initHyph(const char * fname);

void ShutdownCREngine();

#if (USE_FREETYPE==1)
bool getDirectoryFonts( lString32Collection & pathList, lString32 ext, lString32Collection & fonts, bool absPath );
#endif

bool InitCREngine( const char * exename, lString32Collection & fontPathList );

#if (USE_FREETYPE==1)
/// File that keeps the list of the fonts between the runs (see cr3main.cpp); called before
/// InitCREngine(). tag: the list made with another tag is not used (the version of the program).
void SetFontListCacheFile( const char * file, const char * tag );
/// true: the last InitCREngine() registered the fonts from the kept list, not from the font files
bool FontListWasCached();
#endif

void InitCREngineLog( const char * cfgfile );

bool loadKeymaps(CRGUIWindowManager & winman,  const char * keymapFile, const char * locations[] );

#endif
