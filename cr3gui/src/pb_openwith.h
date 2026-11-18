/*
 * "Open with CoolReader": which files the reader offers this program for, and which it opens
 * with it by a tap.
 *
 * The reader decides that by its list system/config/extensions.cfg; a line is
 *     fb2:@FB2_file:1:first.app,second.app:ICON_FB2
 * The programs are those of the menu "Open with" of a file; the first one opens the file by a
 * tap. The firmware remembers no choice made in "Open with". The list of the user holds the lines
 * that differ from the list the firmware came with; a package never carries it.
 *
 * The settings show what the list says (read at every start, whoever wrote it) and change the
 * lines of a group of extensions: this program is put first, last, or taken out; the other
 * programs of the lines and the other lines stay as they are.
 */
#ifndef PB_OPENWITH_H_INCLUDED
#define PB_OPENWITH_H_INCLUDED

enum PbOpenWith {
    PB_OPEN_WITH_NONE = 0,   ///< this program is not offered for the files
    PB_OPEN_WITH_LISTED,     ///< it is in "Open with", another program opens the files by a tap
    PB_OPEN_WITH_DEFAULT     ///< it opens the files by a tap
};

/// The kinds of files the program opens, each with its extensions (pb_openwith.cpp).
enum PbOpenWithGroup {
    PB_OPEN_GROUP_FB2 = 0,
    PB_OPEN_GROUP_EPUB,
    PB_OPEN_GROUP_MOBI,
    PB_OPEN_GROUP_DOC,
    PB_OPEN_GROUP_RTF,
    PB_OPEN_GROUP_TXT,
    PB_OPEN_GROUP_HTML,
    PB_OPEN_GROUP_CHM,
    PB_OPEN_GROUP_PDF,
    PB_OPEN_GROUP_DJVU,
    PB_OPEN_GROUP_COUNT
};

/// The property a group is shown by in the settings ("cr3.pocketbook.openwith.fb2" ...; PDF and
/// DjVu keep the names they had in their own folders).
const char * pbOpenWithProp(int group);
/// The name of a group for the settings and the log: "FB2", "MOBI, PRC, AZW" ...
const char * pbOpenWithName(int group);

/// What the lists say now for every group: PbOpenWith. The state of a group is that of its first
/// extension that has a line.
void pbOpenWithStatesOf(const char * userList, const char * firmwareList, int states[PB_OPEN_GROUP_COUNT]);
/// Changes the lines of the group in the list of the user. An extension neither list knows gets a
/// line only when the kind of the file and its icon are known here. The list is written as a new
/// file and renamed, and only when a line has changed. False when it could not be written.
bool pbOpenWithSetIn(const char * userList, const char * firmwareList, int group, int state);

/// The same for the lists of the reader.
void pbOpenWithStates(int states[PB_OPEN_GROUP_COUNT]);
bool pbOpenWithSet(int group, int state);

#endif // PB_OPENWITH_H_INCLUDED
