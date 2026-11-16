#ifndef __MOD_CITE_H
#define __MOD_CITE_H 1

#include <cstdlib>
#include <tinydict.h>
#include "lvdocview.h"
#include "crgui.h"

class V3DocViewWin;

// The parameter of the command MCMD_CITE, the mode of the citation window:
//   any other value              a new citation; the window closes to the book
//   CITE_PARAM_FROM_LIST         a new citation, opened from the list of citations: when the
//                                window closes, the page that was open is restored and the list
//                                is shown again
//   CITE_PARAM_EDIT_BASE + n     the saved citation n (index in the bookmarks of the book) is
//                                edited: the window opens with its text selected, "save" changes
//                                that citation instead of adding one, then as CITE_PARAM_FROM_LIST
#define CITE_PARAM_FROM_LIST 1000
#define CITE_PARAM_EDIT_BASE 1001

extern void
activate_cite(CRGUIWindowManager *wm, V3DocViewWin * mainwin, int mode = 0);

#endif
