#ifndef CR3_POCKETBOOK_H
#define CR3_POCKETBOOK_H

#ifdef __WINE__
#undef LoadBitmap
#define LoadBitmap PB_LoadBitmap

#undef EnumFonts
#define EnumFonts PB_EnumFonts
#endif

#define PB_COMMANDS_START 6200

enum CRPbCommands {
	PB_CMD_BEGIN = PB_COMMANDS_START,
	PB_QUICK_MENU,
	PB_QUICK_MENU_SELECT,
	PB_CMD_ROTATE,
	PB_CMD_ROTATE_ANGLE_SET,
    PB_CMD_CONTENTS,
    PB_CMD_FRONT_LIGHT,
    PB_CMD_FULL_UPDATE,
    PB_CMD_INVERT_DISPLAY,
    PB_CMD_STATUS_LINE,
	PB_CMD_LEFT,
	PB_CMD_RIGHT,
	PB_CMD_UP,
	PB_CMD_DOWN,
	PB_CMD_SELECT_DICT,
	PB_CMD_TRANSLATE,
	PB_CMD_MP3,
	PB_CMD_VOLUME,
	PB_CMD_BOOKMARK_REMOVE,
	PB_CMD_MAIN_MENU,

    PB_CMD_UPDATE_WINDOW,
    PB_CMD_PAGEUP_REPEAT,
    PB_CMD_PAGEDOWN_REPEAT,
    PB_CMD_REPEAT_FINISH,
    PB_CMD_CLEAR_CACHE,
    PB_CMD_SET_BOOKMARK,
    PB_CMD_UNSET_BOOKMARK,
    PB_CMD_ENTER_STANDBY,

    #ifdef POCKETBOOK_PRO
        PB_CMD_TASK_MANAGER,
        #ifndef POCKETBOOK_PRO_PRO2
            PB_CMD_SYSTEM_PANEL,
        #endif
        PB_CMD_LOCK_DEVICE,
        PB_CMD_OTA_UPDATE,
        PB_CMD_OTA_UPDATE_DEV,
        #ifdef POCKETBOOK_PRO_FW5
            PB_CMD_OPEN_SYSTEM_PANEL,
            PB_CMD_FRONT_LIGHT_TOGGLE,
        #endif
    #endif

    PB_CMD_OPDS_BROWSER,
    PB_CMD_OPDS_CATALOGS,
    PB_CMD_READING_STATS,
    PB_CMD_DEBUG,
    // the position sync of KOReader (pb_kosync.h): its screen (param 1: opened from the settings),
    // the place of the open book sent, taken
    PB_CMD_KOSYNC,
    PB_CMD_KOSYNC_PUSH,
    PB_CMD_KOSYNC_PULL,
    // the lists of the font face, the font size and the font weight with a sample (pb_fontpick.h)
    PB_CMD_FONT_FACE,
    PB_CMD_FONT_SIZE,
    PB_CMD_FONT_WEIGHT,
    // a PDF read as a book: back to its pages as they are (pb_pdfbook.h)
    PB_CMD_PDF_PAGES,
    // the screen turned for the pages of a PDF or a DjVu, the orientation of the books kept as it
    // is in the settings: param is the angle as PROP_POCKETBOOK_ORIENTATION has it (0..3)
    PB_CMD_ROTATE_FOR_PAGES,
    // The name of a bookmark of a book. param: the place of the bookmark in the list (from 1),
    // plus 1000 when the list was in the mode that goes to a bookmark (it is shown again in its
    // mode). _NAME: the keyboard with the name of the bookmark of the place. _NEW: the keyboard
    // for the name of a bookmark that is set in the place when the keyboard is confirmed, with
    // or without a name. From the keyboard: confirmed (_APPLY), or left (_CANCEL).
    PB_CMD_BOOKMARK_NAME,
    PB_CMD_BOOKMARK_NEW,
    PB_CMD_BOOKMARK_NAME_APPLY,
    PB_CMD_BOOKMARK_NAME_CANCEL,

    PB_CMD_NONE
};

#define PB_QUICK_MENU_BMP_ID "fbreader_menu"
#define PB_QUICK_MENU_TEXT_ID "qmenu.fbreader.0.text"
#define PB_QUICK_MENU_TEXT_ID_IDX 15

#define PB_QUICK_MENU_ACTION_ID "qmenu.fbreader.0.action"
#define PB_QUICK_MENU_ACTION_ID_IDX 15

#define KEY_BUFFER_LEN 256

#define PROP_POCKETBOOK_ORIENTATION "cr3.pocketbook.orientation"
#define PROP_POCKETBOOK_DICT "cr3.pocketbook.dictionary"
#define PROP_POCKETBOOK_DICT_PAGES "cr3.pocketbook.dict.pages"
#define PROP_POCKETBOOK_DICT_AUTO_TRANSLATE "cr3.pocketbook.dict.auto"
// place of the dictionary window: away from the selected word, at the top or at the bottom
#define PROP_POCKETBOOK_DICT_POSITION "cr3.pocketbook.dict.position"
#define PB_DICT_POSITION_AUTO 0
#define PB_DICT_POSITION_TOP 1
#define PB_DICT_POSITION_BOTTOM 2
// keyboard of the dictionary search on touch devices: 0 the CoolReader keyboard (follows the night
// mode), 1 the firmware keyboard; devices without a touch screen always use the firmware keyboard
#define PROP_POCKETBOOK_DICT_KEYBOARD "cr3.pocketbook.dict.keyboard"
#define PB_DICT_KEYBOARD_CR 0
#define PB_DICT_KEYBOARD_FIRMWARE 1
// word lookup: 0 the article of the word or the list of the nearest words, 1 exact: only the article
// of the word (any letter case, word forms of StarDict), otherwise "word not found"
#define PROP_POCKETBOOK_DICT_EXACT "cr3.pocketbook.dict.exact"
// font of the word list of the dictionary window: percent of the size the skin gives it. The rows
// of the list are as high as the font asks, so a smaller font shows more words in the window.
#define PROP_POCKETBOOK_DICT_LIST_FONT "cr3.pocketbook.dict.list.font"
// font of the article of the dictionary window: percent of the size the program gives it (that of
// the texts of the touch menu). Set apart from the font of the list (a tester: the rows of the list
// are tapped and must stay large enough for a finger, the article is only read and may be small).
#define PROP_POCKETBOOK_DICT_ARTICLE_FONT "cr3.pocketbook.dict.article.font"
// 0 turns off the direct copy of the page to the screen canvas (no menu item, cr3.ini only)
#define PROP_POCKETBOOK_DIRECT_BLIT "cr3.pocketbook.direct.blit"
// 0: the image of the next page is not drawn ahead while a page is read
// (Display settings: Prepare next page)
#define PROP_POCKETBOOK_PAGE_PREPARE "cr3.pocketbook.page.prepare"
// dictionaries searched together, names separated by '|'; fewer than two means one dictionary
#define PROP_POCKETBOOK_DICT_MULTI "cr3.pocketbook.dict.multi"
#define PROP_POCKETBOOK_ROTATE_MODE "cr3.pocketbook.rotate_mode"
#define PROP_POCKETBOOK_ROTATE_ANGLE "cr3.pocketbook.rotate_angle"
#define PROP_POCKETBOOK_GRAYBUFFER_BPP "cr3.pocketbook.buffer.bpp"
// the contents open with the first level seen and the branch of the page read (1, the default),
// or with every level (0); a branch opens and closes by its triangle (pb_toctree.h)
#define PROP_POCKETBOOK_TOC_COLLAPSED "cr3.pocketbook.toc.collapsed"
// The picture mode of PDF and DjVu files (pb_pageview.h), the settings group "PDF and DjVu": the
// scale (PbPageScale without the own frame) and the leafing (PbPageLeafing) of every file, changed
// in the list of a file too (pb_pagecommon.h; before 0.0.8.2-opds-alpha3 they were what a file
// opened for the first time started with), and whether the page number is shown in the corner. One set of values
// for both kinds of files since 0.0.8-opds-alpha10 (the owner, after a tester); before, each kind
// had its own, "cr3.pocketbook.pdf.*" and "cr3.pocketbook.djvu.*": those of PDF are taken over at
// the first start (PB_OLD_PDF_* in cr3pocketbook.cpp), those of DjVu are read no more.
#define PROP_POCKETBOOK_PAGES_SCALE "cr3.pocketbook.pages.scale"
#define PROP_POCKETBOOK_PAGES_LEAFING "cr3.pocketbook.pages.leafing"
#define PROP_POCKETBOOK_PAGES_PAGE_NUMBER "cr3.pocketbook.pages.page.number"
// A PDF with text, a DjVu with a text layer: 1 read as a book (the text in the fonts of the reader,
// pb_pdfbook.h), 0 shown page by page as it is. What the reader chose for a file (the menu of the
// picture mode, the settings menu of the book) goes before this.
#define PROP_POCKETBOOK_PAGES_TEXT "cr3.pocketbook.pages.text"
// The orientation of the screen over the pages of a PDF or a DjVu: -1 that of the books (the
// default), 0..3 an angle as PROP_POCKETBOOK_ORIENTATION has it. A tester reads only these files in
// landscape, the text of their pages being too small upright. Turned when the pages are shown,
// turned back when they are left; a turn chosen over the pages changes this value then.
#define PROP_POCKETBOOK_PAGES_ORIENTATION "cr3.pocketbook.pages.orientation"
// PbOpenWith (pb_openwith.h): read from the list of the firmware at every start, written to it when changed
#define PROP_POCKETBOOK_PDF_OPEN_WITH "cr3.pocketbook.pdf.openwith"
#define PROP_POCKETBOOK_DJVU_OPEN_WITH "cr3.pocketbook.djvu.openwith"

#define PB_CR3_CACHE_SIZE (0x100000 * 64)

#define CR_PB_VERSION "0.0.8.2-opds"
#define CR_PB_BUILD_DATE "2026-11-27"

#define PB_ROTATE_MODE_360 0
#define PB_ROTATE_MODE_180 1
#define PB_ROTATE_MODE_180_SLOW_NEXT 2
#define PB_ROTATE_MODE_180_SLOW_PREV_NEXT 3
#define PB_ROTATE_MODE_180_FAST_NEXT 4
#define PB_ROTATE_MODE_180_FAST_PREV_NEXT 5
#define PB_ROTATE_MODE_180_FAST_NEXT_PREV 6

#define PB_FRESH_UPDATE_MARKER USERDATA"/.cr3updated"
#define PB_DEV_MARKER          USERDATA"/.cr3-use-devel-branch"

#if defined(POCKETBOOK_PRO_FW5)
    #define CR_PB_SDK "pro5"
#elif defined(POCKETBOOK_PRO)
    #if !defined(POCKETBOOK_PRO_PRO2)
        #define CR_PB_SDK "pro4"
    #else
        #define CR_PB_SDK "pro2"
    #endif
#else
    #define CR_PB_SDK "360"
#endif

#ifdef SYSTEMSFRONTLIGHT
    #define PB_FRONT_LIGHT_BIN SYSTEMSFRONTLIGHT
#else
    #define PB_FRONT_LIGHT_BIN SYSTEMDATA"/bin/front-light.app"
#endif

#ifdef NETAGENT
    #define PB_NETWORK_BIN NETAGENT
#else
    #define PB_NETWORK_BIN SYSTEMDATA"/bin/netagent"
#endif

#ifdef AUTO_CONNECT_APP
    #define PB_AUTO_CONNECT_BIN AUTO_CONNECT_APP
#else
    #define PB_AUTO_CONNECT_BIN SYSTEMDATA"/bin/auto_connect.app"
#endif

#ifdef POCKETBOOK_PRO_FW5
    #define PB_BROWSER_SCRIPT SYSTEMDATA"/bin/webbrowser.sh"
    #define PB_BROWSER_EXEC_SCRIPT SYSTEMDATA"/bin/run_script"
#endif
#define PB_BROWSER_BINARY SYSTEMDATA"/bin/browser.app"
#define PB_BROWSER_EXEC SYSTEMDATA"/bin/openbook"
#define PB_BROWSER_QUERY_GOOGLE "https://www.google.com/search?hl=[LANG]&q="
#define PB_BROWSER_QUERY_WIKIPEDIA "https://[LANG].wikipedia.org/?search="


#define PB_NETWORK_TEST_URL "https://www.google.com"

const char* TR(const char *label);

int getPB_keyboardType();
int getPB_screenType();

bool isGSensorSupported();
bool isFrontLightSupported();
bool isBrowserSupported();

#ifdef POCKETBOOK_PRO
bool isNetworkSupported();
bool pbNetworkConnected();
bool pbNetwork(const char *action);
bool isAutoConnectSupported();
bool isTaskManagerSupported();
#endif

void executeCommand(int commandId, int commandParam);

void pbLaunchWaitBinary(const char *binary, const char *param1, const char *param2, const char *param3);
void pbLaunchWaitBinary(const char *binary, const char *param1, const char *param2);
void pbLaunchWaitBinary(const char *binary, const char *param);
void pbLaunchWaitBinary(const char *binary);

void enterStandByMode();
void toggleInvertDisplay();
void toggleStatusLine();
void launchBrowser(lString32 url);
void drawTemporaryZoom();
void exitApp();
bool showPagesTilChapterEnd();
bool showChapterMarks();
bool fontAntiAliasingActivated();

lString32 getPbModelNumber();

// True for the FW5+ touch skins that use the touch quick menu and the touch TOC
// (pb626fw5, pb631fw5, and the 4pda skins pb740fw5, pb700fw5, pbXfw5, pb617fw5, pb618fw5).
bool isTouchFw5Skin();

// Saves the position of the current book and opens a downloaded file in the reader.
void pbOpenDownloadedBook(const lString32 & path);

// "Close book": asks the firmware to put the program to the background, where the book is saved
// and the program ends; false when the firmware cannot, and the book is closed by the caller.
bool pbQuitViaBackground();
// True from that request on: the files are written, nothing is written again.
bool pbQuitInProgress();

// Starts the scan of the firmware library in the background, so that the library shows a file
// CoolReader has written or deleted (a downloaded book, the log file). See opdsbrowser.cpp.
void pbRequestLibraryScan();

// A question of the firmware with two buttons (Dialog) that the program asks on its own, not as an
// answer to a tap: the handler gets the button, 1 or 2. The program closes the question itself,
// without an answer, when its window is about to be drawn over (the program back on the screen,
// another book opened, the screen drawn): the window would stay open, unseen, and take every
// touch. pbQuestionClose(): closes the question that is open, if one is; why goes to the log.
void pbQuestion(int icon, const char * title, const char * text, const char * button1, const char * button2,
                void (*handler)(int));
void pbQuestionClose(const char * why);

// Called by the handler of a menu of the firmware (OpenMenu) with the index the firmware gives:
// when the menu was closed without a choice (index <= 0), the tap that closed it is not acted on
// where the firmware gives it, or its lift, to the program too.
void pbFirmwareMenuClosed(int index);

// Opens the firmware on-screen keyboard safely from a CR3 menu command (see CRPocketBookKeyboardWindow).
// The handler gets the entered text, or an empty string/NULL on cancel.
void pbOpenKeyboard(const char * title, char * buffer, int maxlen, void (*handler)(char *));

// A message of the firmware shown a moment later, over the window the command that asks for it
// opens or goes back to (shown at once, it was drawn over by that window and gone at once).
void pbMessageLater(int icon, const char * title, const char * text, int timeout);

// The network connected through the firmware: NetConnect(NULL), the last network or the dialog of
// the networks. The firmware may take the screen for that dialog and give it back: while
// pbNetConnecting is set, the program sent to the background and back has not been left by the
// reader (no place of a book is sent for it, no window over the book is closed: the one that
// asked for the network is still at work).
extern bool pbNetConnecting;
int pbNetConnect();

#define MIN_PAGE_TURN_SWIPE_WIDTH 0.1 /* 10% */

// True when the contents open with their branches closed (PROP_POCKETBOOK_TOC_COLLAPSED).
bool pbTocCollapsed();

#ifdef POCKETBOOK_PRO
void toggleSystemPanel();
void toggleSystemPanel(bool screenUpdate);
void showSystemPanel(bool screenUpdate);
void hideSystemPanel(bool screenUpdate);
bool systemPanelShown();
bool canUseNewTouchToc();

#ifdef POCKETBOOK_PRO_FW5
#define FRONTLIGHT_SWIPE_USABLE_SCREEN_HEIGHT 0.6 /* 60% */
void toggleFrontLight();
void setFrontLightValue(int value);
int getFrontLightValue();

void setFrontLightColorValue(int value);
int getFrontLightColorValue();
#endif

#endif

//#ifndef BACKGROUND_CACHE_FILE_CREATION
//#define BACKGROUND_CACHE_FILE_CREATION
//#endif
#endif //CR3_POCKETBOOK_H

