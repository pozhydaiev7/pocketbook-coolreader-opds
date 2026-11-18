/*
 * A PDF with text as a book: the text of the file is taken out and written as an FB2 book, which
 * the program then opens as any book, with the fonts, styles and settings of the reader.
 *
 * The layout of the original is dropped (the owner reads on a screen of 5 to 8 inches): fonts,
 * sizes, columns and the place on the page. Kept: paragraphs, headings, bold, italic, monospace,
 * superscript, tables, pictures, footnotes (as text, after the paragraph their page ends in),
 * the contents of the file. Left out: running titles and page numbers (what repeats at the top
 * and the bottom of the pages). A paragraph that goes on over a page break is one paragraph.
 * The first element of every page carries the id "pgN" (N from 1), so that the program can go
 * from a place of the book to the same page of the file as it is, and back.
 *
 * No part of the engine is used: the PC tool dev/pdfdjvu/pdf2book.cpp builds this file with
 * MuPDF alone.
 */
#ifndef PB_PDFTEXT_H_INCLUDED
#define PB_PDFTEXT_H_INCLUDED

#include <string>

struct PbPdfTextStats {
    int pages;
    int paragraphs, headings, tables, images, footnotes;
    int asIs;           ///< of the pictures: JPEG data of the file put into the book as it is
    int segmented;      ///< pages given to the page segmentation of the library (columns drawn mixed)
    int drawings;       ///< parts of pages drawn with lines and shapes, taken as pictures
    int formulas;       ///< formulas taken as pictures
    int mended;         ///< paragraphs the library had cut at a short line, put together
    int running;        ///< running titles and page numbers left out
    int joined;         ///< paragraphs joined over a page break
    int sections;       ///< entries of the contents
    double textMs;      ///< reading the pages and taking their text out; of it:
    double loadMs;      ///<   the pages read from the file
    double extractMs;   ///<   their text taken out by the library (segmentation, paragraphs, tables)
    double imageMs;     ///< decoding and packing the pictures
    double writeMs;     ///< the analysis of the whole and the writing
    long bytes;         ///< the size of the book
};

/// Called before every page; false stops the work (the book is not written).
typedef bool (*PbPdfTextProgress)(void * context, int page, int pages);

/// What a PDF is, by a few of its pages spread over the file.
enum PbPdfKind {
    PB_PDF_UNREADABLE = 0,  ///< cannot be opened
    PB_PDF_PICTURES,        ///< scans: pages are pictures, with a recognised text under them or without
    PB_PDF_TEXT             ///< a text to read as a book
};
PbPdfKind pbPdfKind(const char * path);

/// Writes the book. False and the reason in error when the file cannot be read or written, or
/// when the progress function stopped the work.
bool pbPdfToBook(const char * pdfPath, const char * bookPath, PbPdfTextProgress progress, void * context,
                 PbPdfTextStats * stats, std::string * error);

/// Changes with what the translator writes: part of the name of a kept book, so that a new
/// version of the program makes the books anew.
#define PB_PDFTEXT_VERSION 2

#endif // PB_PDFTEXT_H_INCLUDED
