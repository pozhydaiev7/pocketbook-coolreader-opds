/*
 * The name of a bookmark of a PDF or a DjVu in the state file of the file (pb_pageview.cpp):
 *
 *     markname <page> <name>
 *
 * one line for a bookmark that has a name, after the lines "mark <page>". A version of the program
 * that does not know the key leaves the line out, as it does with every key it does not know, so
 * the bookmarks themselves stay readable for it.
 *
 * No engine here, so that a PC test covers it.
 */
#ifndef PB_MARKNAMES_H_INCLUDED
#define PB_MARKNAMES_H_INCLUDED

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

/// the longest name kept, in bytes of UTF-8
#define PB_MARK_NAME_MAX 200

/// "Set bookmark" of the list of the bookmarks of a PDF or a DjVu: the page shown into the place
/// index of the list (the pages of the bookmarks, in their order), in place of the bookmark there
/// or, with a place past the bookmarks, as a new one. A page has one bookmark: when the page
/// shown has its bookmark already, nothing is changed and false is returned. (The bookmark of
/// the place was taken off before that was looked at, so setting a bookmark in a taken place on a
/// page that had its own lost the bookmark of that place and set none.)
inline bool pbMarkSet(std::vector<int> & marks, int index, int page)
{
    for (size_t i = 0; i < marks.size(); i++)
        if (marks[i] == page)
            return false;
    if (index >= 0 && index < (int)marks.size())
        marks.erase(marks.begin() + index);
    size_t at = 0;
    while (at < marks.size() && marks[at] < page)
        at++;
    marks.insert(marks.begin() + at, page);
    return true;
}

/// A name as it is kept: one line (a control character becomes a space), no spaces at its ends,
/// PB_MARK_NAME_MAX bytes at most, cut between two characters. Empty: no name.
inline std::string pbMarkNameClean(const std::string & text)
{
    std::string s;
    for (size_t i = 0; i < text.size(); i++) {
        unsigned char c = (unsigned char)text[i];
        s += (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
    }
    size_t from = 0, to = s.size();
    while (from < to && s[from] == ' ')
        from++;
    while (to > from && s[to - 1] == ' ')
        to--;
    s = s.substr(from, to - from);
    if (s.size() > PB_MARK_NAME_MAX) {
        size_t cut = PB_MARK_NAME_MAX;
        // not inside a character of several bytes
        while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80)
            cut--;
        s = s.substr(0, cut);
        while (!s.empty() && s[s.size() - 1] == ' ')
            s.erase(s.size() - 1);
    }
    return s;
}

/// The line of the state file for the name of the bookmark of a page; empty when there is no name.
inline std::string pbMarkNameLine(int page, const std::string & name)
{
    std::string clean = pbMarkNameClean(name);
    if (clean.empty() || page < 0)
        return std::string();
    char number[32];
    snprintf(number, sizeof(number), "%d", page);
    return std::string("markname ") + number + " " + clean + "\n";
}

/// What follows the key "markname" on its line: the page and the name. False when it is not that.
inline bool pbMarkNameParse(const char * rest, int & page, std::string & name)
{
    if (rest == NULL)
        return false;
    while (*rest == ' ' || *rest == '\t')
        rest++;
    if (*rest < '0' || *rest > '9')
        return false;
    char * end = NULL;
    long value = strtol(rest, &end, 10);
    if (end == rest || value < 0 || value > 100000000 || (*end != ' ' && *end != '\t'))
        return false;
    name = pbMarkNameClean(end);
    if (name.empty())
        return false;
    page = (int)value;
    return true;
}

#endif // PB_MARKNAMES_H_INCLUDED
