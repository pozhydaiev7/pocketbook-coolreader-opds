/*
 * Screen "Debugging" of CoolReader on PocketBook: turns the log of the program on and off from
 * the settings menu, so that a user does not have to make crlog.ini on a computer.
 *
 * The log is on while system/share/cr3/crlog.ini exists with a level other than OFF
 * (InitCREngineLog, cr3main.cpp). Turning the debugging on writes that file and starts the log
 * at once; turning it off stops the log and deletes crlog.ini and the log file.
 */
#ifndef PB_DEBUG_H_INCLUDED
#define PB_DEBUG_H_INCLUDED

#include <crgui.h>

/// settings of the log, read at the start of the program
#define PB_LOG_CONFIG_FILE "/mnt/ext1/system/share/cr3/crlog.ini"
/// the log file the screen turns on: the root of the device memory, seen from a computer
#define PB_LOG_FILE "/mnt/ext1/cr3log.txt"
/// the log of the previous run of the program, kept by InitCREngineLog
#define PB_LOG_FILE_PREV "/mnt/ext1/cr3log.prev.txt"
/// level of the log the screen turns on: INFO (the work of the program and errors) or ERROR
#define PROP_POCKETBOOK_LOG_LEVEL "cr3.pocketbook.log.level"

// Command ids posted by the screen: after the contents list (65000..65009).
enum PbDebugCommands {
    PB_DEBUG_CMD_FIRST = 65100,
    PB_DEBUG_CMD_MENU = PB_DEBUG_CMD_FIRST, // the screen again
    PB_DEBUG_CMD_CLOSE,
    PB_DEBUG_CMD_TOGGLE, // turns the log on, or off with the log file deleted
    PB_DEBUG_CMD_LEVEL,  // INFO <-> ERROR
    PB_DEBUG_CMD_LAST = 65109
};

class PbDebug
{
public:
    static PbDebug & instance();
    static bool isDebugCommand(int command) { return command >= PB_DEBUG_CMD_FIRST && command <= PB_DEBUG_CMD_LAST; }

    /// opens the screen; fromSettings: "Back" opens the settings menu again
    void open(CRGUIWindowManager * wm, bool fromSettings);
    /// handles PB_DEBUG_CMD_FIRST..PB_DEBUG_CMD_LAST; true when consumed
    bool onCommand(CRGUIWindowManager * wm, int command, int params);

private:
    PbDebug() : _fromSettings(false) {}

    bool _fromSettings; // opened from the settings menu

    void showMenu(CRGUIWindowManager * wm);
};

#endif
