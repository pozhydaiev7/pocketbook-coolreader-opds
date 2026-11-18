/*
 * PC tool of the translator "a DjVu with a text layer as a book" (cr3gui/src/pb_djvutext.cpp).
 *
 *   djvu2book <file.djvu> <book.fb2>    writes the book and prints what it has made and the times
 *   djvu2book --kind <file.djvu>...     tells for every file whether it has a text to read
 *
 * PB_DJVUTEXT_DUMP=1 prints the items of the book to stderr.
 * Built by build_djvu2book.sh with the libraries of the PC (build_djvu.sh host, build_mupdf.sh host).
 */
#include "pb_djvutext.h"
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
            PbDjvuKind kind = pbDjvuKind(argv[i]);
            printf("%s: %s (%.1f ms)\n", argv[i],
                   kind == PB_DJVU_TEXT ? "text" : kind == PB_DJVU_PICTURES ? "pictures" : "unreadable", now_ms() - t0);
        }
        return 0;
    }
    if (argc < 3) {
        fprintf(stderr, "usage: djvu2book <file.djvu> <book.fb2> | djvu2book --kind <file.djvu>...\n");
        return 2;
    }
    PbDjvuTextStats s;
    std::string error;
    double t0 = now_ms();
    if (!pbDjvuToBook(argv[1], argv[2], NULL, NULL, &s, &error)) {
        fprintf(stderr, "%s: %s\n", argv[1], error.c_str());
        return 1;
    }
    double total = now_ms() - t0;
    printf("%s: %d pages (%d with text) -> %ld bytes in %.1f ms: text %.1f, pictures %.1f (%d pages decoded %.1f, "
           "looked at %.1f, pictures made %.1f), writing %.1f\n",
           argv[1], s.pages, s.textPages, s.bytes, total, s.textMs, s.imageMs, s.decoded, s.decodeMs,
           s.imageMs - s.decodeMs - s.cutMs, s.cutMs, s.writeMs);
    printf("  paragraphs %d (%s), headings %d, contents lines %d, sections %d, tables %d, pictures %d (formulas %d), "
           "ornaments left out %d; running titles left out %d, stray lines left out %d, paragraphs joined over a page %d\n",
           s.paragraphs, s.zones ? "of the file" : "by indents and gaps", s.headings, s.entries, s.sections, s.tables,
           s.images, s.formulas, s.ornaments, s.running, s.stray, s.joined);
    return 0;
}
