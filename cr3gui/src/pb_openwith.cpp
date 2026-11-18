/*
 * "Open with CoolReader": the lines of the list of the reader. See pb_openwith.h.
 *
 * No part of the engine is used here, so a PC check builds this file alone with
 * -DPB_OPENWITH_PC_TEST and gives the two lists as files.
 */
#include "pb_openwith.h"

#include <stdio.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <vector>

#ifndef PB_OPENWITH_PC_TEST
#include <inkview.h>
#include <crlog.h>
#define OW_INFO(...) CRLog::info(__VA_ARGS__)
#define OW_ERROR(...) CRLog::error(__VA_ARGS__)
#else
#define OW_INFO(...) (void)0
#define OW_ERROR(...) (void)0
#endif

namespace {

const char * const THIS_PROGRAM = "cr3-pb.app";

/// An extension of a group. kind and icon are those of its line when neither list has one; NULL:
/// the extension is touched only when a list knows it.
struct Ext {
    const char * ext;
    const char * kind;
    const char * icon;
};

struct Group {
    const char * prop;
    const char * name;
    Ext exts[11];   // the first one names the state of the group; ends with ext == NULL
};

// What the engine opens by the name of a file (crengine/src/bookformats.cpp), and the two kinds
// of the picture mode. The web extensions after mht are those the package has always listed.
const Group GROUPS[PB_OPEN_GROUP_COUNT] = {
    { "cr3.pocketbook.openwith.fb2", "FB2",
      { { "fb2", "@FB2_file", "ICON_FB2" }, { "fb2.zip", NULL, NULL }, { "fb3", NULL, NULL }, { NULL, NULL, NULL } } },
    { "cr3.pocketbook.openwith.epub", "EPUB",
      { { "epub", "@EPUB_file", "ICON_EPUB" }, { NULL, NULL, NULL } } },
    { "cr3.pocketbook.openwith.mobi", "MOBI, PRC, AZW, PDB",
      { { "mobi", NULL, NULL }, { "prc", NULL, NULL }, { "azw", NULL, NULL }, { "pdb", NULL, NULL },
        { NULL, NULL, NULL } } },
    { "cr3.pocketbook.openwith.doc", "DOC, DOCX, ODT",
      { { "doc", NULL, NULL }, { "docx", NULL, NULL }, { "odt", NULL, NULL }, { NULL, NULL, NULL } } },
    { "cr3.pocketbook.openwith.rtf", "RTF",
      { { "rtf", "@RTF_file", "ICON_RTF" }, { NULL, NULL, NULL } } },
    { "cr3.pocketbook.openwith.txt", "TXT",
      { { "txt", "@Text_file", "ICON_TXT" }, { "tcr", NULL, NULL }, { "pml", NULL, NULL }, { NULL, NULL, NULL } } },
    { "cr3.pocketbook.openwith.html", "HTML",
      { { "html", "@HTML_file", "ICON_HTML" }, { "htm", "@HTML_file", "ICON_HTM" }, { "xhtml", NULL, NULL },
        { "shtml", NULL, NULL }, { "mht", NULL, NULL }, { "asp", NULL, NULL }, { "cgi", NULL, NULL },
        { "jsp", NULL, NULL }, { "php", NULL, NULL }, { "pl", NULL, NULL }, { NULL, NULL, NULL } } },
    { "cr3.pocketbook.openwith.chm", "CHM",
      { { "chm", "@Z_HTML_file", "ICON_CHM" }, { NULL, NULL, NULL } } },
    { "cr3.pocketbook.pdf.openwith", "PDF",
      { { "pdf", "@PDF_file", "ICON_PDF" }, { NULL, NULL, NULL } } },
    { "cr3.pocketbook.djvu.openwith", "DjVu",
      { { "djvu", "@DJVU_file", "ICON_DJVU" }, { "djv", "@DJVU_file", "ICON_DJVU" }, { NULL, NULL, NULL } } },
};

struct List {
    std::vector<std::string> lines;
    bool read;          // the file was there
    bool crlf;          // its lines end with CR LF
    bool endsWithEol;   // its last line has an end
    List() : read(false), crlf(false), endsWithEol(true) { }
};

void readList(const char * path, List & list)
{
    list = List();
    FILE * f = path ? fopen(path, "rb") : NULL;
    if (f == NULL)
        return;
    std::string text;
    char block[4096];
    size_t got;
    while ((got = fread(block, 1, sizeof(block), f)) > 0)
        text.append(block, got);
    fclose(f);
    list.read = true;
    list.endsWithEol = text.empty() || text[text.size() - 1] == '\n';
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); i++) {
        if (i < text.size() && text[i] != '\n')
            continue;
        std::string line = text.substr(start, i - start);
        if (!line.empty() && line[line.size() - 1] == '\r') {
            list.crlf = true;
            line.erase(line.size() - 1);
        }
        if (i < text.size() || !line.empty())
            list.lines.push_back(line);
        start = i + 1;
    }
}

int lineOf(const List & list, const char * ext)
{
    const size_t n = strlen(ext);
    for (size_t i = 0; i < list.lines.size(); i++) {
        const std::string & line = list.lines[i];
        if (line.size() > n && line[n] == ':' && line.compare(0, n, ext) == 0)
            return (int)i;
    }
    return -1;
}

/// The five fields of a line (the name of a program holds no colon; an extension may hold a
/// point); false when the line is not of that form.
bool splitLine(const std::string & line, std::string fields[5])
{
    int n = 0;
    size_t start = 0;
    for (size_t i = 0; i <= line.size() && n < 5; i++)
        if (i == line.size() || (line[i] == ':' && n < 4)) {
            fields[n++] = line.substr(start, i - start);
            start = i + 1;
        }
    return n == 5;
}

void splitPrograms(const std::string & text, std::vector<std::string> & programs)
{
    programs.clear();
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); i++)
        if (i == text.size() || text[i] == ',') {
            if (i > start)
                programs.push_back(text.substr(start, i - start));
            start = i + 1;
        }
}

/// The two lists; the one of the firmware is read when a line is first looked for in it.
struct Lists {
    List user, firmware;
    const char * firmwarePath;
    bool firmwareRead;
    Lists(const char * userPath, const char * firmwarePath_) : firmwarePath(firmwarePath_), firmwareRead(false) {
        readList(userPath, user);
    }
    /// The fields of the line of the extension: of the list of the user, else of the firmware,
    /// else the usual ones with no program. False: no list knows it and nothing is known here.
    bool fieldsOf(const Ext & e, std::string fields[5]) {
        int at = lineOf(user, e.ext);
        if (at >= 0 && splitLine(user.lines[at], fields))
            return true;
        if (!firmwareRead) {
            readList(firmwarePath, firmware);
            firmwareRead = true;
        }
        at = lineOf(firmware, e.ext);
        if (at >= 0 && splitLine(firmware.lines[at], fields))
            return true;
        if (e.kind == NULL)
            return false;
        fields[0] = e.ext;
        fields[1] = e.kind;
        fields[2] = "1";
        fields[3].clear();
        fields[4] = e.icon;
        return true;
    }
};

int stateOfPrograms(const std::string & text)
{
    std::vector<std::string> programs;
    splitPrograms(text, programs);
    for (size_t i = 0; i < programs.size(); i++)
        if (programs[i] == THIS_PROGRAM)
            return i == 0 ? PB_OPEN_WITH_DEFAULT : PB_OPEN_WITH_LISTED;
    return PB_OPEN_WITH_NONE;
}

} // namespace

const char * pbOpenWithProp(int group)
{
    return group >= 0 && group < PB_OPEN_GROUP_COUNT ? GROUPS[group].prop : "";
}

const char * pbOpenWithName(int group)
{
    return group >= 0 && group < PB_OPEN_GROUP_COUNT ? GROUPS[group].name : "";
}

void pbOpenWithStatesOf(const char * userList, const char * firmwareList, int states[PB_OPEN_GROUP_COUNT])
{
    Lists lists(userList, firmwareList);
    std::string fields[5];
    for (int g = 0; g < PB_OPEN_GROUP_COUNT; g++) {
        states[g] = PB_OPEN_WITH_NONE;
        for (const Ext * e = GROUPS[g].exts; e->ext != NULL; e++)
            if (lists.fieldsOf(*e, fields)) {
                states[g] = stateOfPrograms(fields[3]);
                break;
            }
    }
}

bool pbOpenWithSetIn(const char * userList, const char * firmwareList, int group, int state)
{
    if (group < 0 || group >= PB_OPEN_GROUP_COUNT)
        return false;
    Lists lists(userList, firmwareList);
    List & user = lists.user;
    bool changed = false;
    for (const Ext * e = GROUPS[group].exts; e->ext != NULL; e++) {
        std::string fields[5];
        if (!lists.fieldsOf(*e, fields))
            continue;
        std::vector<std::string> programs;
        splitPrograms(fields[3], programs);
        for (size_t i = 0; i < programs.size(); )
            if (programs[i] == THIS_PROGRAM)
                programs.erase(programs.begin() + i);
            else
                i++;
        if (state == PB_OPEN_WITH_DEFAULT)
            programs.insert(programs.begin(), std::string(THIS_PROGRAM));
        else if (state == PB_OPEN_WITH_LISTED)
            programs.push_back(std::string(THIS_PROGRAM));
        const int at = lineOf(user, e->ext);
        if (programs.empty()) {
            // no program left: the line goes, the list of the firmware is in force again
            if (at >= 0) {
                user.lines.erase(user.lines.begin() + at);
                changed = true;
            }
            continue;
        }
        std::string line = fields[0] + ":" + fields[1] + ":" + fields[2] + ":";
        for (size_t i = 0; i < programs.size(); i++) {
            if (i > 0)
                line += ",";
            line += programs[i];
        }
        line += ":";
        line += fields[4];
        if (at < 0) {
            user.lines.push_back(line);
            changed = true;
        } else if (user.lines[at] != line) {
            user.lines[at] = line;
            changed = true;
        }
    }
    if (!changed)
        return true;
    // a new file first, then its name: a list cut short would leave the reader without its programs
    const std::string temp = std::string(userList) + ".cr3tmp";
    FILE * f = fopen(temp.c_str(), "wb");
    if (f == NULL) {
        OW_ERROR("open with: cannot write %s", temp.c_str());
        return false;
    }
    const char * end = (user.read ? user.crlf : true) ? "\r\n" : "\n";
    bool ok = true;
    for (size_t i = 0; i < user.lines.size() && ok; i++) {
        const bool last = i + 1 == user.lines.size();
        ok = fputs(user.lines[i].c_str(), f) >= 0 && ((last && user.read && !user.endsWithEol) || fputs(end, f) >= 0);
    }
    ok = fclose(f) == 0 && ok;
    if (!ok || rename(temp.c_str(), userList) != 0) {
        unlink(temp.c_str());
        OW_ERROR("open with: %s is left as it was", userList);
        return false;
    }
    OW_INFO("open with: %s files %s", GROUPS[group].name,
            state == PB_OPEN_WITH_DEFAULT ? "are opened by this program"
            : state == PB_OPEN_WITH_LISTED ? "have this program in the list" : "are not tied to this program");
    return true;
}

#ifndef PB_OPENWITH_PC_TEST
void pbOpenWithStates(int states[PB_OPEN_GROUP_COUNT])
{
    pbOpenWithStatesOf(CONFIGPATH "/extensions.cfg", SYSTEMDATA "/config/extensions.cfg", states);
}

bool pbOpenWithSet(int group, int state)
{
    return pbOpenWithSetIn(CONFIGPATH "/extensions.cfg", SYSTEMDATA "/config/extensions.cfg", group, state);
}
#endif
