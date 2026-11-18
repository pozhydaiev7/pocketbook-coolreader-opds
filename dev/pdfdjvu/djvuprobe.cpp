/* Stage 0 of PDF and DjVu: a probe of DjVuLibre.
 *
 *   djvuprobe <file> <first page> <pages> <width> <out prefix>
 *
 * For each page: the page drawn in gray, fitted to <width> pixels (written as <prefix>-<n>.pgm),
 * and its hidden text layer (the number of words, written as <prefix>-<n>.txt with the rectangle
 * of each word). One line with the times per page.
 */
#include "libdjvu/ddjvuapi.h"
#include "libdjvu/miniexp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static void handle(ddjvu_context_t *ctx, bool wait)
{
    if (wait)
        ddjvu_message_wait(ctx);
    const ddjvu_message_t *msg;
    while ((msg = ddjvu_message_peek(ctx)) != NULL) {
        if (msg->m_any.tag == DDJVU_ERROR)
            fprintf(stderr, "error: %s\n", msg->m_error.message);
        ddjvu_message_pop(ctx);
    }
}

/* the words of a zone and of the zones inside it: (type xmin ymin xmax ymax children-or-string) */
static int walk(miniexp_t zone, FILE *out)
{
    if (!miniexp_consp(zone))
        return 0;
    miniexp_t type = miniexp_car(zone);
    miniexp_t rest = miniexp_cdr(zone);
    int box[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < 4 && miniexp_consp(rest); i++, rest = miniexp_cdr(rest))
        if (miniexp_numberp(miniexp_car(rest)))
            box[i] = miniexp_to_int(miniexp_car(rest));
    int words = 0;
    for (; miniexp_consp(rest); rest = miniexp_cdr(rest)) {
        miniexp_t item = miniexp_car(rest);
        if (miniexp_stringp(item)) {
            if (out)
                fprintf(out, "%s %d %d %d %d %s\n", miniexp_to_name(type), box[0], box[1], box[2], box[3],
                        miniexp_to_str(item));
            words++;
        } else {
            words += walk(item, out);
        }
    }
    return words;
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: djvuprobe <file> <first page> <pages> <width> <out prefix>\n");
        return 2;
    }
    const char *path = argv[1];
    int first = atoi(argv[2]);
    int count = atoi(argv[3]);
    int width = atoi(argv[4]);
    const char *prefix = argv[5];

    double t0 = now_ms();
    ddjvu_context_t *ctx = ddjvu_context_create("djvuprobe");
    ddjvu_document_t *doc = ddjvu_document_create_by_filename_utf8(ctx, path, TRUE);
    if (!doc) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    while (!ddjvu_document_decoding_done(doc))
        handle(ctx, true);
    if (ddjvu_document_decoding_error(doc)) {
        fprintf(stderr, "cannot decode %s\n", path);
        return 1;
    }
    int pages = ddjvu_document_get_pagenum(doc);
    printf("%s: %d pages, opened in %.1f ms\n", path, pages, now_ms() - t0);

    ddjvu_format_t *format = ddjvu_format_create(DDJVU_FORMAT_GREY8, 0, NULL);
    ddjvu_format_set_row_order(format, 1);
    ddjvu_format_set_y_direction(format, 1);

    for (int n = first; n < first + count && n < pages; n++) {
        char name[1024];
        double t1 = now_ms();
        ddjvu_page_t *page = ddjvu_page_create_by_pageno(doc, n);
        while (!ddjvu_page_decoding_done(page))
            handle(ctx, true);
        double t2 = now_ms();
        int pw = ddjvu_page_get_width(page), ph = ddjvu_page_get_height(page);
        int dpi = ddjvu_page_get_resolution(page);
        int w = width, h = (int)((double)ph * width / pw + 0.5);
        ddjvu_rect_t prect = { 0, 0, (unsigned)w, (unsigned)h };
        ddjvu_rect_t rrect = prect;
        unsigned char *buffer = (unsigned char *)malloc((size_t)w * h);
        memset(buffer, 0xFF, (size_t)w * h);
        int drawn = ddjvu_page_render(page, DDJVU_RENDER_COLOR, &prect, &rrect, format, w, (char *)buffer);
        double t3 = now_ms();
        snprintf(name, sizeof(name), "%s-%d.pgm", prefix, n);
        FILE *f = fopen(name, "wb");
        if (f) {
            fprintf(f, "P5\n%d %d\n255\n", w, h);
            fwrite(buffer, 1, (size_t)w * h, f);
            fclose(f);
        }
        free(buffer);
        const char *kind = "?";
        switch (ddjvu_page_get_type(page)) {
        case DDJVU_PAGETYPE_BITONAL: kind = "bitonal"; break;
        case DDJVU_PAGETYPE_PHOTO: kind = "photo"; break;
        case DDJVU_PAGETYPE_COMPOUND: kind = "compound"; break;
        default: break;
        }
        ddjvu_page_release(page);

        double t4 = now_ms();
        miniexp_t text;
        while ((text = ddjvu_document_get_pagetext(doc, n, "word")) == miniexp_dummy)
            handle(ctx, true);
        double t5 = now_ms();
        snprintf(name, sizeof(name), "%s-%d.txt", prefix, n);
        FILE *words_file = fopen(name, "wb");
        int words = walk(text, words_file);
        if (words_file)
            fclose(words_file);
        ddjvu_miniexp_release(doc, text);
        printf("page %d: %dx%d at %d dpi (%s) -> %dx%d, decode %.1f ms, draw %.1f ms%s, text %.1f ms, %d words\n",
               n, pw, ph, dpi, kind, w, h, t2 - t1, t3 - t2, drawn ? "" : " (nothing drawn)", t5 - t4, words);
    }
    ddjvu_format_release(format);
    ddjvu_document_release(doc);
    ddjvu_context_release(ctx);
    return 0;
}
