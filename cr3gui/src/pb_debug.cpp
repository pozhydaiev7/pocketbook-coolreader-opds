/*
 * Screen "Debugging" of CoolReader on PocketBook. See pb_debug.h.
 * The screen is a full-screen touch list of the OPDS catalog (opds/opdsmenu.h).
 */
#include "pb_debug.h"

#include <inkview.h>
#include <crengine.h>
#include <cri18n.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cr3main.h"
#include "cr3pocketbook.h"
#include "mainwnd.h"
#include "opdsmenu.h"

extern V3DocViewWin * main_win;

using namespace opdsui;

namespace {

lString32 tr(const char * s)
{
    return lString32(_(s));
}

// Settings of the log as crlog.ini has them: the level in upper case ("" when there is no file)
// and the log file.
void readLogConfig(lString32 & level, lString32 & file)
{
    level.clear();
    file.clear();
    LVStreamRef cfg = LVOpenFileStream(PB_LOG_CONFIG_FILE, LVOM_READ);
    if (cfg.isNull())
        return;
    CRPropRef p = LVCreatePropsContainer();
    p->loadFromStream(cfg.get());
    // the defaults of InitCREngineLog on the device
    level = p->getStringDef(PROP_LOG_LEVEL, "INFO");
    level.uppercase();
    file = p->getStringDef(PROP_LOG_FILENAME, "/dev/null");
}

// The program writes a log
bool logIsOn()
{
    lString32 level, file;
    readLogConfig(level, file);
    return !level.empty() && !(level == "OFF");
}

// The level the screen turns the log on with: INFO or ERROR
lString32 chosenLevel()
{
    lString32 level = main_win->getProps()->getStringDef(PROP_POCKETBOOK_LOG_LEVEL, "INFO");
    level.uppercase();
    return level == "ERROR" ? level : lString32("INFO");
}

bool writeLogConfig(const lString32 & level)
{
    FILE * f = fopen(PB_LOG_CONFIG_FILE, "wb");
    if (!f)
        return false;
    // every line goes to the file at once: the lines before a crash are what the log is for
    fprintf(f, "crengine.log.filename=%s\ncrengine.log.level=%s\ncrengine.log.autoflush=1\n", PB_LOG_FILE,
            UnicodeToUtf8(level).c_str());
    return fclose(f) == 0;
}

void turnOn()
{
    if (!writeLogConfig(chosenLevel()))
        return;
    // the same way the program starts the log
    InitCREngineLog(PB_LOG_CONFIG_FILE);
    CRLog::info("Debugging is turned on: CoolReader %s of %s", CR_PB_VERSION, CR_PB_BUILD_DATE);
}

// Stops the log and deletes its settings and its file
void turnOff()
{
    lString32 level, file;
    readLogConfig(level, file);
    CRLog::setLogger(NULL); // closes the log file
    unlink(PB_LOG_CONFIG_FILE);
    // the file named by crlog.ini, when it is a file of the device memory, and the file of this screen
    lString8 named = UnicodeToUtf8(file);
    if (strncmp(named.c_str(), "/mnt/", 5) == 0 && strcmp(named.c_str(), PB_LOG_FILE) != 0)
        unlink(named.c_str());
    unlink(PB_LOG_FILE);
    unlink(PB_LOG_FILE_PREV);
}

// "24 KB" of the log file; empty when there is no file
lString32 logFileSize()
{
    struct stat st;
    if (stat(PB_LOG_FILE, &st) != 0)
        return lString32::empty_str;
    char buf[32];
    if (st.st_size >= 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f ", (double)st.st_size / (1024.0 * 1024.0));
        return lString32(buf) + tr("MB");
    }
    snprintf(buf, sizeof(buf), "%d ", (int)((st.st_size + 1023) / 1024));
    return lString32(buf) + tr("KB");
}

} // namespace

PbDebug & PbDebug::instance()
{
    static PbDebug debug;
    return debug;
}

void PbDebug::open(CRGUIWindowManager * wm, bool fromSettings)
{
    _fromSettings = fromSettings;
    showMenu(wm);
}

void PbDebug::showMenu(CRGUIWindowManager * wm)
{
    bool on = logIsOn();
    lvRect rc = wm->getScreen()->getRect();
    OpdsMenu * menu = new OpdsMenu(wm, tr("Debugging"), rc, false);
    menu->setupRows(true);
    menu->setCloseCommand(PB_DEBUG_CMD_CLOSE);
    menu->add(PB_DEBUG_CMD_CLOSE, lString32(MARK_PREV) + (_fromSettings ? tr("Back to settings") : tr("Back to the book")));
    menu->add(PB_DEBUG_CMD_TOGGLE, tr("Debugging") + U": " + tr(on ? "On" : "Off"),
              on ? tr("Turn off and delete the log file") : tr("Turn on: the program writes a log of its work"));
    menu->add(PB_DEBUG_CMD_LEVEL, tr("Log level") + U": " + (chosenLevel() == "ERROR" ? U"Error" : U"Info"),
              tr("Info: the work of the program and errors. Error: errors only"));
    if (on) {
        lString32 file(PB_LOG_FILE);
        lString32 size = logFileSize();
        if (!size.empty())
            file << lString32(DOT) << size;
        menu->add(PB_DEBUG_CMD_MENU, tr("Log file"), file);
    }
    activate(wm, menu);
}

bool PbDebug::onCommand(CRGUIWindowManager * wm, int command, int params)
{
    (void)params;
    switch (command) {
    case PB_DEBUG_CMD_CLOSE:
        // opened from the settings menu: back to that menu, at its item
        if (_fromSettings) {
            _fromSettings = false;
            wm->postCommand(MCMD_SETTINGS, PB_CMD_DEBUG);
        }
        return true;
    case PB_DEBUG_CMD_TOGGLE:
        if (logIsOn())
            turnOff();
        else
            turnOn();
        // the library of the firmware shows the new log file, or no longer shows the deleted one
        pbRequestLibraryScan();
        showMenu(wm);
        return true;
    case PB_DEBUG_CMD_LEVEL: {
        lString32 level = chosenLevel() == "ERROR" ? lString32("INFO") : lString32("ERROR");
        main_win->getProps()->setString(PROP_POCKETBOOK_LOG_LEVEL, level);
        main_win->saveSettings(lString32::empty_str);
        // a log that is being written changes its level at once
        if (logIsOn() && writeLogConfig(level)) {
            CRLog::setLogLevel(level == "ERROR" ? CRLog::LL_ERROR : CRLog::LL_INFO);
            CRLog::info("Log level is %s now", UnicodeToUtf8(level).c_str());
        }
        showMenu(wm);
        return true;
    }
    default:
        // the screen again: a tap on the row of the log file
        showMenu(wm);
        return true;
    }
}
