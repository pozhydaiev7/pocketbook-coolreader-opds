/*
 * A DjVu with a text layer as a book: the recognised text of the file is written as an FB2 book,
 * which the program then opens as any book, with the fonts, styles and settings of the reader.
 *
 * The text layer of a DjVu is what a recognition program has read from the scan: lines with their
 * places on the page, in some files gathered into paragraphs. The book is as good as that reading.
 * Made of it: paragraphs (the ones of the file, or put together from the lines by their indents
 * and gaps), headings (larger lines), lines of a contents page, the contents of the file as the
 * sections of the book. Left out: running titles and page numbers. A paragraph that goes on over
 * a page break is one paragraph; a word cut at the end of a line is put together.
 * What stands on a page where no text was recognised (a picture, a drawing, a formula the
 * recognition skipped, a page with no text at all) is cut out of the page as a picture. The first
 * page is the cover. The first element of every page carries the id "pgN" (N from 1), as in a
 * book made of a PDF (pb_pdftext.h).
 *
 * No part of the engine is used: the PC tool dev/pdfdjvu/djvu2book.cpp builds this file with
 * DjVuLibre and MuPDF (which packs the pictures) alone.
 */
#ifndef PB_DJVUTEXT_H_INCLUDED
#define PB_DJVUTEXT_H_INCLUDED

#include <string>

struct PbDjvuTextStats {
    int pages;
    int textPages;      ///< pages with a text layer
    int paragraphs, headings, entries, images;
    int tables;         ///< runs of rows whose cells stand wide apart
    int formulas;       ///< of the pictures: formulas the recognition gave as a row of signs
    int ornaments;      ///< pictures left out: the same at the same place of three pages and more
    int stray;          ///< lines left out: clouds of marks where the recognition read a picture as text
    int running;        ///< running titles and page numbers left out
    int joined;         ///< paragraphs joined over a page break
    int sections;       ///< sections of the book
    int decoded;        ///< pages decoded to look for pictures
    bool zones;         ///< the paragraphs are the ones of the file
    double textMs;      ///< reading the text layer
    double imageMs;     ///< decoding pages, finding and packing the pictures; of it:
    double decodeMs;    ///<   the pages decoded
    double cutMs;       ///<   the pictures drawn at their size and packed
    double writeMs;     ///< the analysis of the whole and the writing
    long bytes;         ///< the size of the book
};

/// Called as the work goes on, done of total; false stops the work (the book is not written).
typedef bool (*PbDjvuTextProgress)(void * context, int done, int total);

/// What a DjVu is, by a few of its pages spread over the file.
enum PbDjvuKind {
    PB_DJVU_UNREADABLE = 0, ///< cannot be opened
    PB_DJVU_PICTURES,       ///< scans without a text layer, or with one on a few pages only
    PB_DJVU_TEXT            ///< scans with a text layer: can be read as a book
};
PbDjvuKind pbDjvuKind(const char * path);

/// Writes the book. False and the reason in error when the file cannot be read or written, or
/// when the progress function stopped the work.
bool pbDjvuToBook(const char * djvuPath, const char * bookPath, PbDjvuTextProgress progress, void * context,
                  PbDjvuTextStats * stats, std::string * error);

/// Changes with what the translator writes: part of the name of a kept book, so that a new
/// version of the program makes the books anew. (1: alpha3. 2: alpha4, the clouds of stray marks.)
#define PB_DJVUTEXT_VERSION 2

#endif // PB_DJVUTEXT_H_INCLUDED
