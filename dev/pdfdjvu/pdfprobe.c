/* Stage 0 of PDF and DjVu: a probe of MuPDF.
 *
 *   pdfprobe <file> <first page> <pages> <width> <out prefix>
 *
 * For each page: the page drawn in gray, fitted to <width> pixels (written as <prefix>-<n>.pgm),
 * and its structured text with the page segmentation, the paragraph breaks, the table hunt and
 * the pictures (written as <prefix>-<n>.xhtml). One line with the times of both per page.
 */
#include <mupdf/fitz.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef NEED_GETENTROPY
/* glibc before 2.25 (the SDK of the reader has 2.9) has no getentropy(); MuPDF asks for 44 bytes
 * once, as the seed of its random numbers (object identifiers, encryption of a written file) */
#include <fcntl.h>
#include <unistd.h>
int getentropy(void *buffer, size_t length)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t got = read(fd, buffer, length);
    close(fd);
    return got == (ssize_t)length ? 0 : -1;
}
#endif

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: pdfprobe <file> <first page> <pages> <width> <out prefix>\n");
        return 2;
    }
    const char *path = argv[1];
    int first = atoi(argv[2]);
    int count = atoi(argv[3]);
    int width = atoi(argv[4]);
    const char *prefix = argv[5];
    int result = 0;

    fz_context *ctx = fz_new_context(NULL, NULL, 32 << 20);
    if (!ctx) {
        fprintf(stderr, "no context\n");
        return 1;
    }
    fz_document *doc = NULL;
    fz_var(doc);
    fz_try(ctx) {
        double t0 = now_ms();
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, path);
        int pages = fz_count_pages(ctx, doc);
        printf("%s: %d pages, opened in %.1f ms\n", path, pages, now_ms() - t0);
        for (int n = first; n < first + count && n < pages; n++) {
            char name[1024];
            double t1 = now_ms();
            fz_page *page = fz_load_page(ctx, doc, n);
            fz_rect box = fz_bound_page(ctx, page);
            float zoom = width / (box.x1 - box.x0);
            double t2 = now_ms();
            fz_pixmap *pix = fz_new_pixmap_from_page(ctx, page, fz_scale(zoom, zoom), fz_device_gray(ctx), 0);
            double t3 = now_ms();
            snprintf(name, sizeof(name), "%s-%d.pgm", prefix, n);
            fz_save_pixmap_as_pnm(ctx, pix, name);
            int w = fz_pixmap_width(ctx, pix), h = fz_pixmap_height(ctx, pix);
            fz_drop_pixmap(ctx, pix);

            fz_stext_options opts;
            memset(&opts, 0, sizeof(opts));
            opts.flags = FZ_STEXT_PRESERVE_IMAGES | FZ_STEXT_DEHYPHENATE | FZ_STEXT_COLLECT_STRUCTURE
                | FZ_STEXT_COLLECT_VECTORS | FZ_STEXT_SEGMENT | FZ_STEXT_PARAGRAPH_BREAK | FZ_STEXT_TABLE_HUNT;
            double t4 = now_ms();
            fz_stext_page *text = fz_new_stext_page_from_page(ctx, page, &opts);
            double t5 = now_ms();
            int blocks[5] = { 0, 0, 0, 0, 0 };
            for (fz_stext_block *b = text->first_block; b; b = b->next)
                if (b->type >= 0 && b->type < 5)
                    blocks[b->type]++;
            snprintf(name, sizeof(name), "%s-%d.xhtml", prefix, n);
            fz_output *out = fz_new_output_with_path(ctx, name, 0);
            fz_print_stext_page_as_xhtml(ctx, out, text, n);
            fz_close_output(ctx, out);
            fz_drop_output(ctx, out);
            fz_drop_stext_page(ctx, text);
            fz_drop_page(ctx, page);
            printf("page %d: %.0fx%.0f pt -> %dx%d, load %.1f ms, draw %.1f ms, text %.1f ms;"
                " blocks: text %d, image %d, struct %d, vector %d, grid %d\n",
                n, box.x1 - box.x0, box.y1 - box.y0, w, h, t2 - t1, t3 - t2, t5 - t4,
                blocks[0], blocks[1], blocks[2], blocks[3], blocks[4]);
        }
    }
    fz_always(ctx) {
        fz_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        fprintf(stderr, "error: %s\n", fz_caught_message(ctx));
        result = 1;
    }
    fz_drop_context(ctx);
    return result;
}
