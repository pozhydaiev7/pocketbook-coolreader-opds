/*
 * A PDF with text, or a DjVu with a text layer, read as a book: the glue between the translators
 * (pb_pdftext.h, pb_djvutext.h), the picture mode (pb_pageview.h) and the book window. The names
 * here say "PDF" since the PDF came first; all of it serves both kinds of files.
 *
 * The book made of a file is kept in system/state/cr3/pages as "<name of the file>-<hash>.fb2";
 * the hash covers the path, the size and the date of the file and the version of the translator,
 * so a changed file or a new translator makes the book anew. A file beside it, ".src", names the
 * PDF and its number of pages. A few books are kept; the oldest go.
 *
 * Whether a file is read as a book: what the reader chose for the file (remembered in the state
 * file of the picture mode), else the setting "A new file with text" of "PDF view settings" or "DjVu
 * view settings" when the file is a text: a PDF that is not scans (pbPdfKind), a DjVu with a text
 * layer on half of its pages and more (pbDjvuKind).
 */
#ifndef PB_PDFBOOK_H_INCLUDED
#define PB_PDFBOOK_H_INCLUDED

#include <crengine.h>

class CRGUIWindowManager;

/// True, and the book to load, when the file (a PDF, or an archive with one) is to be read as a
/// book; the book is made now when it is not kept, with the progress shown. False: the file goes
/// to the picture mode.
bool pbPdfBookFor(const lString32 & path, lString32 & book);

/// From the picture mode to the book, at the page (from 0) that was shown: the choice is
/// remembered for the file, the book is made when it is not kept, loaded and turned to the page.
/// The window of the picture mode is closed by the caller before. False when no book could be
/// made: the caller opens the picture mode again.
bool pbPdfBookFromPages(CRGUIWindowManager * wm, const lString32 & path, int page);

/// From the book to the pages as they are, at the page the place of the book comes from. False
/// when the book in the book window is not one made of a PDF.
bool pbPdfBookToPages(CRGUIWindowManager * wm);

/// True when the book in the book window is one made of a PDF or a DjVu (the main menu of the
/// book then begins with "View as the original").
bool pbPdfBookIsOpen();

#endif // PB_PDFBOOK_H_INCLUDED
