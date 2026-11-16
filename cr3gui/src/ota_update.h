/**
 * Program update over the network (OTA) from the releases of the GitHub repository.
 *
 * The screen "Program update" checks for a newer version, lists the versions, shows what is new
 * in a version and downloads its package after a confirmation. The package is checked (size,
 * SHA-256, contents) and left as /mnt/ext1/latest.zip; the launcher (system/bin/cr3-pb.app)
 * installs it at the restart with install.sh of the package and goes back to the previous
 * version when the new one does not start. The core without firmware calls is ota_core.h.
 */

#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <crgui.h>
#include <string>
#include <vector>

#include "cr3pocketbook.h"
#include "ota_core.h"

#ifndef FLASHDIR
    #ifndef DIRPREFIX
    #define DIRPREFIX ""
    #endif

    #define FLASHDIR DIRPREFIX "/mnt/ext1"
#endif

#define OTA_DOWNLOAD_DIR FLASHDIR
// the launcher installs this file at the next start
#define OTA_PACKAGE_FILE OTA_DOWNLOAD_DIR "/latest.zip"
// the download goes here and gets the name of the package only after the checks
#define OTA_PACKAGE_PART OTA_DOWNLOAD_DIR "/latest.zip.part"
// the launcher starts the program again when it finds this file after the exit
#define OTA_RESTART_MARK OTA_DOWNLOAD_DIR "/.cr3restart"
// written by the installer or by the launcher when an update failed: the version, or an empty line
#define OTA_FAILED_MARKER OTA_DOWNLOAD_DIR "/system/.cr3rollback"

// list of the releases of the repository
#define OTA_RELEASES_URL "https://api.github.com/repos/pozhydaiev7/pocketbook-coolreader-opds/releases?per_page=30"
// The first version with this update. Older versions are not offered: a device that got one
// could not be updated over the network again.
#define OTA_FIRST_VERSION "0.0.6-17-67-opds-alpha18"
// Seconds between the automatic checks; 0: at every start of the program. A check once a day
// (the first builds) looked like an update that does not work: a version published an hour
// after the check was not offered until the next day.
#define OTA_CHECK_PERIOD 0

// 1: checks for a newer version at every start of the program, when Wi-Fi is connected already
#define PROP_POCKETBOOK_OTA_AUTO "cr3.pocketbook.ota.auto"
// 1: test versions (alpha, beta, pre-releases of GitHub) are offered too
#define PROP_POCKETBOOK_OTA_TEST "cr3.pocketbook.ota.test"
// time of the last check, seconds since 1970
#define PROP_POCKETBOOK_OTA_LAST_CHECK "cr3.pocketbook.ota.last_check"
// the version the automatic check does not offer again: declined by the user or failed to start
#define PROP_POCKETBOOK_OTA_SKIP "cr3.pocketbook.ota.skip"
// another address of the list of releases (no menu item, cr3.ini only): for tests
#define PROP_POCKETBOOK_OTA_URL "cr3.pocketbook.ota.url"

// Command ids posted by the update screens. Below the OPDS commands (70000), after the contents
// list (65000..65009).
enum PbOtaCommands {
    PB_OTA_CMD_FIRST = 66000,
    PB_OTA_CMD_MENU = PB_OTA_CMD_FIRST, // the screen "Program update"
    PB_OTA_CMD_CLOSE,
    PB_OTA_CMD_CHECK,         // checks for a newer version now
    PB_OTA_CMD_VERSIONS,      // list of the versions
    PB_OTA_CMD_TOGGLE_AUTO,
    PB_OTA_CMD_TOGGLE_TEST,
    PB_OTA_CMD_PREV,          // previous screen of the list or of the card
    PB_OTA_CMD_NEXT,
    PB_OTA_CMD_BACK,          // from the card of a version to the screen it was opened from
    PB_OTA_CMD_INSTALL,       // asks to confirm the download of the version of the card
    PB_OTA_CMD_OFFERED,       // "Details" of the window of the automatic check: the card of that version
    PB_OTA_CMD_VERSION = 66100, // + index in the list: the card of the version
    PB_OTA_CMD_LAST = 66999
};

class PbOta
{
public:
    static PbOta & instance();
    static bool isOtaCommand(int command) { return command >= PB_OTA_CMD_FIRST && command <= PB_OTA_CMD_LAST; }

    /// the firmware has the network: the update can be offered
    static bool supported();
    /// values of the update settings that are not in cr3.ini yet
    static void setDefaults(CRPropRef props);
    /// the first screen of the program is shown: tells about a failed update and plans the
    /// automatic check
    static void started();

    /// opens the screen "Program update"; fromSettings: "Back" opens the settings menu again
    void open(CRGUIWindowManager * wm, bool fromSettings);
    /// handles PB_OTA_CMD_FIRST..PB_OTA_CMD_LAST; true when consumed
    bool onCommand(CRGUIWindowManager * wm, int command, int params);

    /// the automatic check: only when the network is connected already; asks nothing when there
    /// is no newer version
    void autoCheck();
    /// answer of the window "version ... is available" of the automatic check
    void offerAnswered(int button);
    /// answer of the window "download and install?"
    void installAnswered(int button);
    /// "Details" was answered (by a timer, after the window is gone): posts PB_OTA_CMD_OFFERED,
    /// so the card is opened the way a tap in the list of versions opens it
    void openOfferedCard();
    /// "Yes" was answered: downloads and checks the package of the card, then asks to restart
    /// (by a timer, after the window is gone)
    void download();

private:
    PbOta();

    enum Mode { MODE_MENU, MODE_LIST, MODE_CARD };

    std::vector<ota::Release> _all;  // releases of the last answer of GitHub, the newest first
    std::vector<ota::Release> _list; // the offered ones, as the list shows them
    ota::Release _card;              // the version of the card
    int _cardBack;                   // command that leaves the card
    int _cardScreen;                 // screen of the card
    int _listScreen;                 // screen of the list
    Mode _mode;                      // the screen shown last
    bool _fromSettings;              // opened from the settings menu
    long long _fetched;              // time of the last answer of GitHub in this run
    int _verified;                   // certificate of the server: -1 no request yet, 0 not checked, 1 checked
    lString32 _result;               // result of the last check of this run, for the screen
    lString32 _error;                // why the last request of the list failed, for the screen
    lString8 _question;              // text of the question window: kept while the window is shown

    bool fetch(bool connect, lString32 & error);
    std::vector<ota::Release> offered();
    void check(CRGUIWindowManager * wm);
    void showMenu(CRGUIWindowManager * wm);
    void showList(CRGUIWindowManager * wm);
    void showCard(CRGUIWindowManager * wm);
    void showCurrent(CRGUIWindowManager * wm);
    void openCard(CRGUIWindowManager * wm, const ota::Release & release, int back);
    void askInstall();
};

#endif // OTA_UPDATE_H
