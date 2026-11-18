/*
    First version of CR3 for EWL, based on etimetool example by Lunohod
*/
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
//#include <Ewl.h>
#include <crengine.h>
#include <crgui.h>
#include <crtrace.h>

#include "cr3main.h"
#include "mainwnd.h"

bool loadKeymaps( CRGUIWindowManager & winman, const char * keymapFile, const char * locations[] )
{
	bool res = false;
	for ( int i=0; locations[i]; i++ ) {
		lString8 location( locations[i] );
		char lastChar = location[ location.length() - 1 ];
		if ( lastChar!='/' && lastChar!='\\' )
#if defined(_WIN32) && !defined(__WINE__)
			location << "\\";
#else
			location << "/";
#endif
		lString8 def = location + "keydefs.ini";
                lString8 map = location + keymapFile;
#ifndef CR_POCKETBOOK
		lString8 layout = location + "kblayout.ini";
		winman.getKeyboardLayouts().openFromFile( layout.c_str() );
#endif
		CRGUIAcceleratorTableList tables;

		if ( tables.openFromFile(  def.c_str(), map.c_str() ) ) {
			res = true;
                        winman.setKeymapFilePath(Utf8ToUnicode(map));
			winman.getAccTables().addAll( tables );
		}
	}
    if ( winman.getAccTables().empty() ) {
        CRLog::error("keymap files keydefs.ini and keymaps.ini were not found! please place them to ~/.crengine or /etc/cr3");
    }
#if 0
    static const int menu_acc_table[] = {
        XK_Escape, 0, MCMD_CANCEL, 0,
        XK_Return, 0, MCMD_OK, 0, 
        XK_Return, 1, MCMD_OK, 0, 
        '0', 0, MCMD_SCROLL_FORWARD, 0,
        XK_Down, 0, MCMD_SCROLL_FORWARD, 0,
        '9', 0, MCMD_SCROLL_BACK, 0,
        XK_Up, 0, MCMD_SCROLL_BACK, 0,
        '0', 1, MCMD_LONG_FORWARD, 0,
        XK_Down, 1, MCMD_LONG_FORWARD, 0,
        '9', 1, MCMD_LONG_BACK, 0,
        XK_Up, 1, MCMD_LONG_BACK, 0,
        '1', 0, MCMD_SELECT_1, 0,
        '2', 0, MCMD_SELECT_2, 0,
        '3', 0, MCMD_SELECT_3, 0,
        '4', 0, MCMD_SELECT_4, 0,
        '5', 0, MCMD_SELECT_5, 0,
        '6', 0, MCMD_SELECT_6, 0,
        '7', 0, MCMD_SELECT_7, 0,
        '8', 0, MCMD_SELECT_8, 0,
        0
    };
    if ( winman.getAccTables().get("menu").isNull() )
        winman.getAccTables().add("menu", menu_acc_table );
    static const int acc_table_dialog[] = {
        XK_Escape, 0, MCMD_CANCEL, 0,
        XK_Return, 1, MCMD_OK, 0, 
        XK_Return, 0, MCMD_OK, 0, 
        XK_Down, 0, MCMD_SCROLL_FORWARD, 0,
        XK_Up, 0, MCMD_SCROLL_BACK, 0,
        '0', 0, MCMD_SELECT_0, 0,
        '1', 0, MCMD_SELECT_1, 0,
        '2', 0, MCMD_SELECT_2, 0,
        '3', 0, MCMD_SELECT_3, 0,
        '4', 0, MCMD_SELECT_4, 0,
        '5', 0, MCMD_SELECT_5, 0,
        '6', 0, MCMD_SELECT_6, 0,
        '7', 0, MCMD_SELECT_7, 0,
        '8', 0, MCMD_SELECT_8, 0,
        '9', 0, MCMD_SELECT_9, 0,
        0
    };
    if ( winman.getAccTables().get("dialog").isNull() )
        winman.getAccTables().add("dialog", acc_table_dialog );
    static const int default_acc_table[] = {
        '6', 0, MCMD_GO_LINK, 0,
        '8', 0, MCMD_SETTINGS_FONTSIZE, 0,
        '8', 1, MCMD_SETTINGS_ORIENTATION, 0,
        XK_Escape, 0, MCMD_QUIT, 0,
        XK_Return, 0, MCMD_MAIN_MENU, 0,
        XK_Return, 1, MCMD_SETTINGS, 0,
        '0', 0, DCMD_PAGEDOWN, 0,
        XK_Up, 0, DCMD_PAGEDOWN, 0,
        '0', KEY_FLAG_LONG_PRESS, DCMD_PAGEDOWN, 10,
        XK_Up, KEY_FLAG_LONG_PRESS, DCMD_PAGEDOWN, 10,
        XK_Down, 0, DCMD_PAGEUP, 0,
        XK_Down, KEY_FLAG_LONG_PRESS, DCMD_PAGEUP, 10,
        '9', 0, DCMD_PAGEUP, 0,
        '9', KEY_FLAG_LONG_PRESS, DCMD_PAGEUP, 10,
#ifdef WITH_DICT
        '2', 0, MCMD_DICT, 0,
#endif
        '+', 0, DCMD_ZOOM_IN, 0,
        '=', 0, DCMD_ZOOM_IN, 0,
        '-', 0, DCMD_ZOOM_OUT, 0,
        '_', 0, DCMD_ZOOM_OUT, 0,
        0
    };
    if ( winman.getAccTables().get("main").isNull() )
        winman.getAccTables().add("main", default_acc_table );
#endif
	return res;
}

#if 0
bool initHyph(const char * fname)
{
    //HyphMan hyphman;
    //return;

    LVStreamRef stream = LVOpenFileStream( fname, LVOM_READ);
    if (!stream)
    {
        printf("Cannot load hyphenation file %s\n", fname);
        return false;
    }
    TexHyph * method = new TexHyph();
    if ( method->load( stream ) ) {
        _method = method;
        return true;
    }
    _method = NO_HYPH;
    return false;
}
#endif

lString8 readFileToString( const char * fname )
{
    lString8 buf;
    LVStreamRef stream = LVOpenFileStream(fname, LVOM_READ);
    if (!stream)
        return buf;
    int sz = stream->GetSize();
    if (sz>0)
    {
        buf.insert( 0, sz, ' ' );
        stream->Read( buf.modify(), sz, NULL );
    }
    return buf;
}

void ShutdownCREngine()
{
    HyphMan::uninit();
    ShutdownFontManager();
    CRLog::setLogger( NULL );
}

#if (USE_FREETYPE==1)
bool getDirectoryFonts( lString32Collection & pathList, lString32Collection & ext, lString32Collection & fonts, bool absPath )
{
    int foundCount = 0;
    lString32 path;
    for ( int di=0; di<pathList.length();di++ ) {
        path = pathList[di];
        LVContainerRef dir = LVOpenDirectory(path.c_str());
        if ( !dir.isNull() ) {
            CRLog::trace("Checking directory %s", UnicodeToUtf8(path).c_str() );
            for ( int i=0; i < dir->GetObjectCount(); i++ ) {
                const LVContainerItemInfo * item = dir->GetObjectInfo(i);
                lString32 fileName = item->GetName();
                lString8 fn = UnicodeToLocal(fileName);
                    //printf(" test(%s) ", fn.c_str() );
                if ( !item->IsContainer() ) {
                    bool found = false;
                    lString32 lc = fileName;
                    lc.lowercase();
                    for ( int j=0; j<ext.length(); j++ ) {
                        if ( lc.endsWith(ext[j]) ) {
                            found = true;
                            break;
                        }
                    }
                    if ( !found )
                        continue;
                    lString32 fn;
                    if ( absPath ) {
                        fn = path;
                        if ( !fn.empty() && fn[fn.length()-1]!=PATH_SEPARATOR_CHAR)
                            fn << PATH_SEPARATOR_CHAR;
                    }
                    fn << fileName;
                    foundCount++;
                    fonts.add( fn );
                }
            }
        }
    }
    return foundCount > 0;
}
#endif

#if (USE_FREETYPE==1)
// The list of the fonts kept between the runs. A start registered every font file of the device
// (about 190 on a PocketBook) by opening it with FreeType: 80 to 180 ms. The faces the font
// manager registered are written to a file with the size and the time of every font file; the
// next start registers them from that file. Any difference in the set of the font files, in
// their order, sizes or times, or in the tag (the version of the program) drops the file: the
// fonts are read again and the file is written anew.
static lString8 fontListCacheFile;
static lString8 fontListCacheTag;
static bool fontListFromCache = false;

void SetFontListCacheFile( const char * file, const char * tag )
{
    fontListCacheFile = lString8(file);
    fontListCacheTag = lString8(tag);
}

bool FontListWasCached()
{
    return fontListFromCache;
}

// "F <size> <time> <path>" of a font file; empty when the file cannot be examined
static lString8 fontListStamp( const lString8 & path )
{
    struct stat st;
    if ( stat(path.c_str(), &st) != 0 )
        return lString8::empty_str;
    char buf[80];
    snprintf(buf, sizeof(buf), "F %lld %lld ", (long long)st.st_size, (long long)st.st_mtime);
    return lString8(buf) + path;
}

static lString8 fontListHeader()
{
    return lString8("cr3fontlist 1 ") + fontListCacheTag;
}

// Registers the fonts by the kept list; false (and nothing registered) when the list does not
// describe these very files
static bool registerFontsFromList( lString32Collection & fonts )
{
    if ( fontListCacheFile.empty() )
        return false;
    FILE * f = fopen(fontListCacheFile.c_str(), "rb");
    if ( !f )
        return false;
    lString8Collection lines;
    static char buf[2048];
    while ( fgets(buf, sizeof(buf), f) ) {
        int n = (int)strlen(buf);
        while ( n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r') )
            buf[--n] = 0;
        lines.add(lString8(buf));
    }
    fclose(f);
    if ( lines.length() < 1 || lines[0] != fontListHeader() )
        return false;
    lString8Collection paths, faces;
    int at = 1;
    for ( int fi=0; fi<fonts.length(); fi++ ) {
        lString8 fn = UnicodeToLocal(fonts[fi]);
        lString8 stamp = fontListStamp(fn);
        if ( stamp.empty() || at >= lines.length() || lines[at] != stamp )
            return false;
        at++;
        lString8 fileFaces;
        while ( at < lines.length() && lines[at].length() > 2 && lines[at][0] == 'D' && lines[at][1] == ' ' ) {
            fileFaces << lines[at].substr(2) << "\n";
            at++;
        }
        paths.add(fn);
        faces.add(fileFaces);
    }
    if ( at != lines.length() )
        return false; // the list knows more files than there are
    for ( int i=0; i<paths.length(); i++ ) {
        if ( !faces[i].empty() )
            fontMan->RegisterFontFaces(paths[i], faces[i]);
    }
    return true;
}

// Registers the fonts by their files and writes the list for the next start
static void registerFontsFromFiles( lString32Collection & fonts )
{
    lString8 list = fontListHeader() + "\n";
    bool complete = !fontListCacheFile.empty();
    for ( int fi=0; fi<fonts.length(); fi++ ) {
        lString8 fn = UnicodeToLocal(fonts[fi]);
        CRLog::trace("loading font: %s", fn.c_str());
        lString8 fileFaces;
        if ( !fontMan->RegisterFont(fn) ) {
            CRLog::trace("    failed\n");
        }
        fileFaces = fontMan->GetLastRegisteredFaces();
        lString8 stamp = fontListStamp(fn);
        if ( stamp.empty() )
            complete = false;
        list << stamp << "\n";
        int start = 0;
        while ( start < (int)fileFaces.length() ) {
            int end = start;
            while ( end < (int)fileFaces.length() && fileFaces[end] != '\n' )
                end++;
            list << "D " << fileFaces.substr(start, end - start) << "\n";
            start = end + 1;
        }
    }
    if ( !complete )
        return;
    lString8 tmp = fontListCacheFile + ".tmp";
    FILE * f = fopen(tmp.c_str(), "wb");
    if ( !f )
        return;
    bool ok = fwrite(list.c_str(), 1, list.length(), f) == (size_t)list.length();
    ok = fclose(f) == 0 && ok;
    if ( ok )
        rename(tmp.c_str(), fontListCacheFile.c_str());
    else
        unlink(tmp.c_str());
}
#endif

bool InitCREngine( const char * exename, lString32Collection & fontDirs )
{
    CRLog::trace("InitCREngine(%s)", exename);

    crSetSignalHandler();
    for ( int k=0; k<fontDirs.length(); k++ )
        CRLog::trace(" fontDir: %s", LCSTR(fontDirs[k]));
    lString32 appname( exename );
    int lastSlash=-1;
    lChar32 slashChar = '/';
    for ( int p=0; p<(int)appname.length(); p++ ) {
        if ( appname[p]=='\\' ) {
            slashChar = '\\';
            lastSlash = p;
        } else if ( appname[p]=='/' ) {
            slashChar = '/';
            lastSlash=p;
        }
    }

    lString32 appPath;
    if ( lastSlash>=0 )
        appPath = appname.substr( 0, lastSlash+1 );

    lString32 fontDir = appPath + "fonts";
    fontDir << slashChar;
    lString8 fontDir8 = UnicodeToLocal(fontDir);
    //const char * fontDir8s = fontDir8.c_str();
    //InitFontManager( fontDir8 );
    InitFontManager(lString8::empty_str);

    // Load font definitions into font manager
    // fonts are in files font1.lbf, font2.lbf, ... font32.lbf
    if (!fontMan->GetFontCount()) {

    lString32Collection fontExt;
    #if (USE_FREETYPE==1)
        fontExt.add(".ttf");
        fontExt.add(".otf");
        fontExt.add(".pfa");
        fontExt.add(".pfb");
    #else
        fontExt.add(".lbf");
    #endif
    #if (USE_FREETYPE==1)
        lString32Collection fonts;
        fontDirs.add( fontDir );
    #ifdef _LINUX
    #ifndef LBOOK
        static const char * msfonts[] = {
            "arial.ttf", "arialbd.ttf", "ariali.ttf", "arialbi.ttf",
            "cour.ttf", "courbd.ttf", "couri.ttf", "courbi.ttf",
            "times.ttf", "timesbd.ttf", "timesi.ttf", "timesbi.ttf",
            NULL
        };
        fontDirs.add("/usr/local/share/crengine/fonts");
        fontDirs.add("/usr/local/share/fonts/truetype/freefont");
        fontDirs.add("/usr/share/crengine/fonts");
        fontDirs.add("/usr/share/fonts/truetype/freefont");
        fontDirs.add("/root/fonts/truetype");
        //fontDirs.add( lString32(U"/usr/share/fonts/truetype/msttcorefonts") );
        for ( int fi=0; msfonts[fi]; fi++ )
            fonts.add( lString32("/usr/share/fonts/truetype/msttcorefonts/") + lString32(msfonts[fi]) );
    #endif
    #endif
        getDirectoryFonts( fontDirs, fontExt, fonts, true );

        // load fonts from file
        CRLog::debug("%d font files found", fonts.length());
        // the kept list of the fonts when it describes these files, the files themselves otherwise
        fontListFromCache = registerFontsFromList( fonts );
        if ( !fontListFromCache )
            registerFontsFromFiles( fonts );
    #else
            #define MAX_FONT_FILE 128
            for (int i=0; i<MAX_FONT_FILE; i++)
            {
                char fn[1024];
                sprintf( fn, "font%d.lbf", i );
                printf("try load font: %s\n", fn);
                fontMan->RegisterFont( lString8(fn) );
            }
    #endif
    }

    // init hyphenation manager
    //char hyphfn[1024];
    //sprintf(hyphfn, "Russian_EnUS_hyphen_(Alan).pdb" );
    //if ( !initHyph( (UnicodeToLocal(appPath) + hyphfn).c_str() ) ) {
#ifdef _LINUX
    //    initHyph( "/usr/share/crengine/hyph/Russian_EnUS_hyphen_(Alan).pdb" );
#endif
    //}

    if (!fontMan->GetFontCount())
    {
        //error
#if (USE_FREETYPE==1)
        printf("Fatal Error: Cannot open font file(s) .ttf \nCannot work without font\n" );
#else
        printf("Fatal Error: Cannot open font file(s) font#.lbf \nCannot work without font\nUse FontConv utility to generate .lbf fonts from TTF\n" );
#endif
        return false;
    }

    printf("%d fonts loaded.\n", fontMan->GetFontCount());

    return true;

}

void InitCREngineLog( const char * cfgfile )
{
    if ( !cfgfile ) {
        //CRLog::setStdoutLogger();
        //CRLog::setLogLevel( CRLog::LL_TRACE );
        return;
    }
    lString32 logfname(
#ifdef __arm__
                                               "/dev/null"
#else
                                               "stdout"
#endif
            );
    lString32 loglevelstr("INFO");
	bool autoFlush = false;
    CRPropRef logprops = LVCreatePropsContainer();
    {
        LVStreamRef cfg = LVOpenFileStream( cfgfile, LVOM_READ );
#ifdef __arm__
        // no crlog.ini on the device: no log at all. A log of the INFO level written to /dev/null
        // formatted every message for nothing.
        if ( cfg.isNull() )
            return;
#endif
        if ( !cfg.isNull() ) {
            logprops->loadFromStream( cfg.get() );
            logfname = logprops->getStringDef( PROP_LOG_FILENAME,
#ifdef __arm__
                                               "/dev/null"
#else
                                               "stdout"
#endif
                                               );
            loglevelstr = logprops->getStringDef( PROP_LOG_LEVEL,
#ifdef __arm__
                                                  "INFO"
#else
                                                  "TRACE"
#endif
                                                  );
			autoFlush = logprops->getBoolDef( PROP_LOG_AUTOFLUSH, false );
        }
    }
    CRLog::log_level level = CRLog::LL_INFO;
    loglevelstr.uppercase(); // "info" and "INFO" mean the same
    if (loglevelstr == "OFF") {
        level = CRLog::LL_FATAL;
        logfname.clear();
    } else if (loglevelstr == "FATAL") {
        level = CRLog::LL_FATAL;
    } else if (loglevelstr == "ERROR") {
        level = CRLog::LL_ERROR;
    } else if (loglevelstr == "WARN") {
        level = CRLog::LL_WARN;
    } else if (loglevelstr == "INFO") {
        level = CRLog::LL_INFO;
    } else if (loglevelstr == "DEBUG") {
        level = CRLog::LL_DEBUG;
    } else if (loglevelstr == "TRACE") {
        level = CRLog::LL_TRACE;
    }
    if ( !logfname.empty() ) {
        if (logfname == "stdout")
            CRLog::setStdoutLogger();
        else if (logfname == "stderr")
            CRLog::setStderrLogger();
        else {
            lString8 fname = UnicodeToUtf8( logfname );
#ifdef __arm__
            // The log file is started anew here. The log of the previous run is kept as
            // "<name>.prev.txt": a fault is often looked at after the program was started
            // again, and an update of the program restarts it by itself.
            int len = fname.length();
            if ( len > 4 && strcmp( fname.c_str() + len - 4, ".txt" ) == 0 ) {
                lString8 prev = fname.substr( 0, len - 4 );
                prev << ".prev.txt";
                remove( prev.c_str() );
                rename( fname.c_str(), prev.c_str() );
            }
#endif
            CRLog::setFileLogger( fname.c_str(), autoFlush );
        }
    }
    CRLog::setLogLevel( level );
    CRLog::trace("Log initialization done.");
}

