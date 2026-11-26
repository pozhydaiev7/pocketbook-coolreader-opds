/*
 * Scale and leafing of the picture mode: one for every PDF and DjVu file (pb_pageview.h).
 *
 * They were kept for each file, and the settings group "PDF and DjVu" held what a file opened for
 * the first time started with. A tester set "As a ribbon" in the settings while the list of the
 * file said "By frames", the pages went by frames, and it took long to see why. The owner: one
 * leafing and one scale for all files. The list of a file and the settings group show and change
 * the same two values (PROP_POCKETBOOK_PAGES_SCALE, PROP_POCKETBOOK_PAGES_LEAFING).
 *
 * What stays with a file is its own frame (a point of the page, a zoom and a turn set by the
 * reader): a file whose reader has chosen it is shown with it whatever the common scale is, until
 * a scale is chosen in the list of the file or in the settings.
 *
 * No window and no engine here: the rules are checked on a PC.
 */
#ifndef PB_PAGECOMMON_H_INCLUDED
#define PB_PAGECOMMON_H_INCLUDED

#include "pb_pagedoc.h"

/// The common scale as the settings may hold it: a scale of the list, never the own frame.
inline int pbPageCommonScale(int value)
{
    return value < 0 || value >= PB_PAGE_SCALE_COUNT || value == PB_PAGE_SCALE_OWN ? PB_PAGE_SCALE_AUTO : value;
}

/// The common leafing as the settings may hold it.
inline int pbPageCommonLeafing(int value)
{
    return value == PB_PAGE_LEAF_FRAMES || value == PB_PAGE_LEAF_RIBBON ? value : PB_PAGE_LEAF_PAGE;
}

/// The scale a file is opened with. kept: the scale its state names (-1 for none); ownSet: the
/// reader has set a frame for the file; common: the scale of the settings.
inline int pbPageScaleOfFile(int kept, bool ownSet, int common)
{
    return kept == PB_PAGE_SCALE_OWN && ownSet ? PB_PAGE_SCALE_OWN : pbPageCommonScale(common);
}

/// The scale of the open file when the settings may have been changed. shown: its scale now;
/// before: the scale of the settings as the window saw it last; now: as it is. The own frame of
/// the file stays until the scale of the settings is changed.
inline int pbPageScaleAfterSettings(int shown, int before, int now)
{
    now = pbPageCommonScale(now);
    return shown == PB_PAGE_SCALE_OWN && now == pbPageCommonScale(before) ? shown : now;
}

/// True when the place inside a page kept with a file (a frame, a row of the ribbon) is good for
/// the scale and the leafing the file is shown with: it was counted with the same ones.
inline bool pbPageFrameKept(int keptScale, int keptLeafing, int scale, int leafing)
{
    return keptScale == scale && keptLeafing == leafing;
}

#endif // PB_PAGECOMMON_H_INCLUDED
