/* PC bench of the page documents (cr3gui/src/pb_pagedoc.cpp): PDF and DjVu through the interface
 * the reader uses.
 *
 *   pagebench <file> <page from 0> <width> <height> <out prefix> [angle]
 *
 * Prints the kind of the file, the pages, the contents, the size of the page and its content box,
 * and draws four frames into <prefix>-*.pgm, each timed (the first drawing, which reads the page,
 * and the best of three more):
 *   whole    the whole page in the screen
 *   content  the content box in the screen (margins cut off)
 *   turned   the content box turned by [angle] (default 1.5 degrees)
 *   zoom     the middle of the page at twice the zoom of "content"
 */
#include "pb_pagedoc.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static double now_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static void save(const char * prefix, const char * name, const std::vector<unsigned char> & buf, int w, int h)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s-%s.pgm", prefix, name);
    FILE * f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(&buf[0], 1, buf.size(), f);
    fclose(f);
}

static void frame(PbPageDoc * doc, int page, const PbPageView & view, int w, int h, const char * prefix, const char * name)
{
    std::vector<unsigned char> buf((size_t)w * h);
    double t0 = now_ms();
    bool ok = doc->render(page, view, &buf[0], w, h, w);
    double first = now_ms() - t0, best = 1e9;
    for (int i = 0; i < 3; i++) {
        t0 = now_ms();
        doc->render(page, view, &buf[0], w, h, w);
        double t = now_ms() - t0;
        if (t < best)
            best = t;
    }
    unsigned sum = 0;
    for (size_t i = 0; i < buf.size(); i++)
        sum = sum * 31 + buf[i];
    printf("  %-8s zoom %.4f angle %.2f centre %.1f,%.1f: first %.1f ms, again %.1f ms, %s, hash %08x\n",
           name, view.zoom, view.angle, view.cx, view.cy, first, best, ok ? "ok" : "FAILED", sum);
    save(prefix, name, buf, w, h);
}

int main(int argc, char ** argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: pagebench <file> <page> <width> <height> <out prefix> [angle]\n");
        return 2;
    }
    int page = atoi(argv[2]), w = atoi(argv[3]), h = atoi(argv[4]);
    const char * prefix = argv[5];
    float angle = argc > 6 ? (float)atof(argv[6]) : 1.5f;
    std::string error;
    double t0 = now_ms();
    PbPageDoc * doc = PbPageDoc::open(argv[1], &error);
    if (!doc) {
        fprintf(stderr, "%s: %s\n", argv[1], error.c_str());
        return 1;
    }
    printf("%s: %s, %d pages, opened in %.1f ms\n", argv[1], doc->kind() == PB_PAGEDOC_PDF ? "PDF" : "DjVu",
           doc->pageCount(), now_ms() - t0);
    std::vector<PbPageTocEntry> toc;
    t0 = now_ms();
    doc->contents(toc);
    printf("contents: %d entries in %.1f ms\n", (int)toc.size(), now_ms() - t0);
    for (size_t i = 0; i < toc.size() && i < 6; i++)
        printf("  %*s%s -> page %d\n", toc[i].level * 2, "", toc[i].title.c_str(), toc[i].page + 1);
    PbPageSize size;
    if (!doc->pageSize(page, size)) {
        fprintf(stderr, "no page %d\n", page);
        return 1;
    }
    // an experiment: PB_FIRST_ZOOM=<pixels per page unit> draws the middle of the page at this
    // zoom before anything else, to see what the later, smaller frames cost after it
    if (getenv("PB_FIRST_ZOOM")) {
        PbPageView first;
        first.cx = size.width * 0.5f;
        first.cy = size.height * 0.5f;
        first.zoom = (float)atof(getenv("PB_FIRST_ZOOM"));
        frame(doc, page, first, w, h, prefix, "first");
    }
    t0 = now_ms();
    bool text = doc->hasText(page);
    double tText = now_ms() - t0;
    PbPageBox box;
    t0 = now_ms();
    // as the window does it: the frames of the default scale are laid out, which looks for the
    // content box with the zoom of the frames to come known to the document
    {
        std::vector<PbPageView> first;
        doc->framesOf(page, PB_PAGE_SCALE_CONTENT, PB_PAGE_LEAF_PAGE, 0, PbPageOwnFrame(), w, h, first);
    }
    bool found = doc->contentBox(page, box);
    // the page after this one starts being read beside the drawing (PB_NO_HINT=1: it does not)
    if (!getenv("PB_NO_HINT"))
        doc->hintNext(page + 1);
    printf("page %d: %.0f x %.0f at %.0f dpi, text %s (%.1f ms), content box %.0f,%.0f - %.0f,%.0f %s (%.1f ms)\n",
           page, size.width, size.height, size.dpi, text ? "yes" : "no", tText,
           box.x0, box.y0, box.x1, box.y1, found ? "found" : "not found", now_ms() - t0);

    PbPageView whole = PbPageDoc::viewOfBox(PbPageBox(0, 0, size.width, size.height), 0, w, h);
    PbPageView content = PbPageDoc::viewOfBox(box, 0, w, h);
    PbPageView turned = PbPageDoc::viewOfBox(box, angle, w, h);
    PbPageView zoom = content;
    zoom.zoom *= 2;
    frame(doc, page, whole, w, h, prefix, "whole");
    frame(doc, page, content, w, h, prefix, "content");
    frame(doc, page, turned, w, h, prefix, "turned");
    frame(doc, page, zoom, w, h, prefix, "zoom");
    // the next page: what a page turn costs when nothing is prepared
    if (page + 1 < doc->pageCount()) {
        PbPageSize next;
        doc->pageSize(page + 1, next);
        frame(doc, page + 1, PbPageDoc::viewOfBox(PbPageBox(0, 0, next.width, next.height), 0, w, h), w, h, prefix, "next");
    }
    // a page turn with nothing prepared, as the window makes it: the frames of the next page are
    // laid out (the content box) and the first one is drawn. The frame of this page was drawn
    // just before, as on a reader.
    if (page + 2 < doc->pageCount()) {
        std::vector<PbPageView> frames;
        std::vector<unsigned char> buffer((size_t)w * h);
        doc->framesOf(page, PB_PAGE_SCALE_CONTENT, PB_PAGE_LEAF_PAGE, 0, PbPageOwnFrame(), w, h, frames);
        doc->render(page, frames[0], &buffer[0], w, h, w);
        for (int turn = 1; turn <= 2; turn++) {
            if (!getenv("PB_NO_HINT"))
                doc->hintNext(page + turn);
            // the time the page is read on a reader, in which the next one is decoded beside
            usleep(getenv("PB_NO_HINT") ? 0 : 300000);
            doc->readMs = 0;
            doc->boxMs = 0;
            t0 = now_ms();
            doc->framesOf(page + turn, PB_PAGE_SCALE_CONTENT, PB_PAGE_LEAF_PAGE, 0, PbPageOwnFrame(), w, h, frames);
            doc->render(page + turn, frames[0], &buffer[0], w, h, w);
            double total = now_ms() - t0;
            printf("  turn%d    total %.1f ms: page read %.1f, content box %.1f, frame %.1f\n", turn, total,
                   doc->readMs, doc->boxMs, total - doc->readMs - doc->boxMs);
        }
    }
    // the frames of the page for every scale and both ways of leafing, as the window of the
    // picture mode asks for them; the frames of "content width, by frames" are drawn
    static const char * const scales[] = { "whole page", "page width", "whole content", "content width", "own frame" };
    PbPageOwnFrame own;
    own.cx = box.x0 + (box.x1 - box.x0) * 0.3f;
    own.cy = box.y0 + (box.y1 - box.y0) * 0.2f;
    own.zoom = content.zoom * 1.8f;
    own.set = true;
    for (int scale = 0; scale < PB_PAGE_SCALE_COUNT; scale++)
        for (int leafing = 0; leafing < 2; leafing++) {
            std::vector<PbPageView> frames;
            doc->framesOf(page, scale, leafing, scale == PB_PAGE_SCALE_OWN ? angle : 0, own, w, h, frames);
            printf("  frames, %-13s %-17s %d:", scales[scale], leafing ? "by frames" : "next page at once", (int)frames.size());
            for (size_t i = 0; i < frames.size() && i < 6; i++)
                printf(" (%.0f,%.0f x%.3f)", frames[i].cx, frames[i].cy, frames[i].zoom);
            printf("\n");
            if (scale == PB_PAGE_SCALE_CONTENT_WIDTH && leafing == 1)
                for (size_t i = 0; i < frames.size() && i < 4; i++) {
                    char name[32];
                    snprintf(name, sizeof(name), "f%d", (int)i);
                    frame(doc, page, frames[i], w, h, prefix, name);
                }
        }
    delete doc;
    return 0;
}
