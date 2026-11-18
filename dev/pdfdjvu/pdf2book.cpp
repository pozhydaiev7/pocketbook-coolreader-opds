/*
 * PC tool of the translator "a PDF with text as a book" (cr3gui/src/pb_pdftext.cpp).
 *
 *   pdf2book <file.pdf> <book.fb2>      writes the book and prints what it has made and the times
 *   pdf2book --kind <file.pdf>...       tells for every file whether it is a text or pictures
 *
 * Built by build_pdf2book.sh with the MuPDF of the PC (build_mupdf.sh host).
 */
#include "pb_pdftext.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static double now_ms()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

int main(int argc, char ** argv)
{
    if (argc >= 3 && strcmp(argv[1], "--kind") == 0) {
        for (int i = 2; i < argc; i++) {
            double t0 = now_ms();
            PbPdfKind kind = pbPdfKind(argv[i]);
            printf("%s: %s (%.1f ms)\n", argv[i],
                   kind == PB_PDF_TEXT ? "text" : kind == PB_PDF_PICTURES ? "pictures" : "unreadable", now_ms() - t0);
        }
        return 0;
    }
    if (argc < 3) {
        fprintf(stderr, "usage: pdf2book <file.pdf> <book.fb2> | pdf2book --kind <file.pdf>...\n");
        return 2;
    }
    PbPdfTextStats s;
    std::string error;
    double t0 = now_ms();
    if (!pbPdfToBook(argv[1], argv[2], NULL, NULL, &s, &error)) {
        fprintf(stderr, "%s: %s\n", argv[1], error.c_str());
        return 1;
    }
    double total = now_ms() - t0;
    printf("%s: %d pages -> %ld bytes in %.1f ms (%.2f ms a page): text %.1f (pages read %.1f, taken out by the "
           "library %.1f, the translator %.1f), pictures %.1f, writing %.1f\n",
           argv[1], s.pages, s.bytes, total, s.pages ? total / s.pages : 0, s.textMs, s.loadMs, s.extractMs,
           s.textMs - s.loadMs - s.extractMs, s.imageMs, s.writeMs);
    printf("  pages segmented by the library %d, JPEG pictures put in as they are %d\n", s.segmented, s.asIs);
    printf("  paragraphs %d, headings %d, sections %d, tables %d, pictures %d (drawings %d, formulas %d), "
           "footnotes %d; running titles left out %d, paragraphs joined over a page %d, put together %d\n",
           s.paragraphs, s.headings, s.sections, s.tables, s.images, s.drawings, s.formulas, s.footnotes, s.running,
           s.joined, s.mended);
    return 0;
}
