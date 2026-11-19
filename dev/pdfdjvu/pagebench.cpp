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
 *
 *   pagebench level <file> <first page from 0> <pages> <max degrees> <out prefix>
 *
 * Levelling every page by itself: the tilt found for each page, its time, the frames before and after.
 *
 *   pagebench turns <without|with> <file> <first page from 0> <pages> <max degrees>
 *
 * What a page costs without the levelling and with it, by parts: run once each way.
 *
 *   pagebench cost <file> <page from 0> <max degrees>
 *
 * What levelling costs: ours, a turned frame against an upright one, and the amount of work of k2pdfopt.
 */
#include "pb_pagedoc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

// pagebench level <file> <first page from 0> <pages> <max degrees> <out prefix>
// Levelling every page by itself (PbPageDoc::skewOf, levelView): for each page the tilt found, how
// clear it was, the time, and the tilt left on the levelled frame. <prefix>-p<N>-before.pgm is the
// frame of the content box upright, <prefix>-p<N>-after.pgm the levelled one, 702 x 936 each.
static int levelMode(int argc, char ** argv)
{
    if (argc < 7) {
        fprintf(stderr, "usage: pagebench level <file> <first page> <pages> <max degrees> <out prefix>\n");
        return 2;
    }
    const int first = atoi(argv[3]), count = atoi(argv[4]);
    const float maxDegrees = (float)atof(argv[5]);
    const char * prefix = argv[6];
    const int w = 702, h = 936;
    std::string error;
    PbPageDoc * doc = PbPageDoc::open(argv[2], &error);
    if (!doc) {
        fprintf(stderr, "%s: %s\n", argv[2], error.c_str());
        return 1;
    }
    printf("%s: %s, %d pages; tilt looked for within %.1f degrees\n", argv[2],
           doc->kind() == PB_PAGEDOC_PDF ? "PDF" : "DjVu", doc->pageCount(), maxDegrees);
    double sumLevel = 0, sumAlone = 0, worstSkew = 0;
    int pages = 0;
    for (int p = first; p < first + count && p < doc->pageCount(); p++) {
        PbPageSize size;
        if (!doc->pageSize(p, size))
            continue;
        // as the window does it: the frames of "whole content" are laid out (the page is read and
        // its content box found), and the first frame is drawn
        std::vector<PbPageView> frames;
        std::vector<unsigned char> before((size_t)w * h), after((size_t)w * h);
        double t0 = now_ms();
        doc->framesOf(p, PB_PAGE_SCALE_CONTENT, PB_PAGE_LEAF_PAGE, 0, PbPageOwnFrame(), w * 2, h * 2, frames);
        PbPageBox box;
        doc->contentBox(p, box);
        doc->render(p, PbPageDoc::viewOfBox(box, 0, w, h), &before[0], w, h, w);
        const double tRead = now_ms() - t0;
        // the levelling, asked for after the page is read: the tilt and the content of the turned
        // page, from one small picture
        t0 = now_ms();
        PbPageView view;
        const bool content = doc->levelView(p, maxDegrees, w, h, view);
        const double tLevel = now_ms() - t0;
        float degrees = 0;
        const bool found = doc->skewOf(p, maxDegrees, degrees);   // kept by levelView
        doc->render(p, view, &after[0], w, h, w);
        // the arithmetic alone, on a picture as skewOf makes it: the best of five
        const int W = 512;
        int H = (int)(size.height * W / size.width + 0.5f);
        if (H < 64) H = 64;
        if (H > 1024) H = 1024;
        std::vector<unsigned char> small((size_t)W * H);
        PbPageView whole;
        whole.cx = size.width * 0.5f;
        whole.cy = size.height * 0.5f;
        whole.zoom = W / size.width < H / size.height ? W / size.width : H / size.height;
        doc->render(p, whole, &small[0], W, H, W);
        float clear = 0, d2 = 0;
        double alone = 1e9;
        for (int i = 0; i < 5; i++) {
            t0 = now_ms();
            PbPageDoc::skewOfPicture(&small[0], W, H, W, maxDegrees, d2, &clear);
            const double t = now_ms() - t0;
            if (t < alone)
                alone = t;
        }
        // what is left of the tilt on the levelled frame
        float left = 0;
        const bool leftFound = PbPageDoc::skewOfPicture(&after[0], w, h, w, 3.0f, left);
        printf("page %d: turn %+.2f %s, clear %.1f; levelling %.1f ms (the arithmetic of the tilt alone %.2f ms), "
               "content %s; left on the levelled frame %+.2f%s; frame centre %.0f,%.0f zoom %.3f; "
               "page read and first frame %.1f ms\n",
               p, degrees, found ? "found" : "none found", clear, tLevel, alone,
               content ? "found" : "NOT FOUND", left, leftFound ? "" : " (no lines)", view.cx, view.cy, view.zoom, tRead);
        char name[64];
        snprintf(name, sizeof(name), "p%d-before", p);
        save(prefix, name, before, w, h);
        snprintf(name, sizeof(name), "p%d-after", p);
        save(prefix, name, after, w, h);
        sumLevel += tLevel;
        sumAlone += alone;
        if (tLevel > worstSkew)
            worstSkew = tLevel;
        pages++;
    }
    if (pages > 0)
        printf("mean of %d pages: levelling %.1f ms (the arithmetic of the tilt alone %.2f ms), the slowest page %.1f ms\n",
               pages, sumLevel / pages, sumAlone / pages, worstSkew);
    delete doc;
    return 0;
}

// pagebench cost <file> <page from 0> <max degrees>
// What levelling costs, three numbers for one page and a frame of 1404 x 1872:
//   ours      PbPageDoc::levelView, the page read before it
//   frames    the frame of the levelled page against the same frame upright (a turned frame costs more)
//   k2pdfopt  the amount of work of bmp_autostraighten of k2pdfopt (willuslib/bmp.c), written anew
//             here by its description: every angle of the range in steps of 0.05 degrees, for each
//             the dark pixels of the whole frame summed along tilted lines, up to 400 points a line;
//             then the whole frame turned (pbRotateGray). Not its code: the same loops over the
//             same number of pixels, to have a measured number beside ours.
static int costMode(int argc, char ** argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: pagebench cost <file> <page> <max degrees>\n");
        return 2;
    }
    const int p = atoi(argv[3]);
    const float maxDegrees = (float)atof(argv[4]);
    const int w = 1404, h = 1872;
    std::string error;
    PbPageDoc * doc = PbPageDoc::open(argv[2], &error);
    if (!doc) {
        fprintf(stderr, "%s: %s\n", argv[2], error.c_str());
        return 1;
    }
    std::vector<PbPageView> frames;
    std::vector<unsigned char> upright((size_t)w * h), turned((size_t)w * h), out((size_t)w * h);
    double t0 = now_ms();
    doc->framesOf(p, PB_PAGE_SCALE_CONTENT, PB_PAGE_LEAF_PAGE, 0, PbPageOwnFrame(), w, h, frames);
    doc->render(p, frames[0], &upright[0], w, h, w);
    const double tRead = now_ms() - t0;
    t0 = now_ms();
    PbPageView view;
    doc->levelView(p, maxDegrees, w, h, view);
    const double tOurs = now_ms() - t0;
    double tUp = 1e9, tTurned = 1e9;
    PbPageView same = view;
    same.angle = 0;
    for (int i = 0; i < 3; i++) {
        t0 = now_ms();
        doc->render(p, same, &upright[0], w, h, w);
        double t = now_ms() - t0;
        if (t < tUp) tUp = t;
        t0 = now_ms();
        doc->render(p, view, &turned[0], w, h, w);
        t = now_ms() - t0;
        if (t < tTurned) tTurned = t;
    }
    // the work of k2pdfopt on the frame as it is shown
    t0 = now_ms();
    const float step = 0.05f;
    const int na = (int)(maxDegrees / step + 0.5f);
    const int c1 = w / 15, c2 = w - c1;
    int dw = (c2 - c1) / 400;
    if (dw < 1) dw = 1;
    std::vector<int> off;
    double bestDev = -1;
    int bestI = 0;
    for (int i = -na; i <= na; i++) {
        const float t = tanf(i * step * 3.14159265f / 180.0f);
        off.clear();
        for (int c = c1; c < c2; c += dw)
            off.push_back((int)(t * c) * w + c);
        const int lost = (int)(fabsf(t) * w) + 1 + h / 15;
        double sum = 0, sumSq = 0;
        int n = 0;
        for (int r = lost; r < h - lost; r++) {
            const unsigned char * row = &upright[(size_t)r * w];
            int count = 0;
            for (size_t k = 0; k < off.size(); k++)
                if (row[off[k]] < 128)
                    count++;
            const double d = 100.0 * count / off.size();
            sum += d;
            sumSq += d * d;
            n++;
        }
        const double dev = n > 0 ? sqrt(fabs((sum / n) * (sum / n) - sumSq / n)) : 0;
        if (dev > bestDev) {
            bestDev = dev;
            bestI = i;
        }
    }
    const double tAngles = now_ms() - t0;
    t0 = now_ms();
    pbRotateGray(&upright[0], w, h, w, w * 0.5f, h * 0.5f, &out[0], w, h, w, w * 0.5f, h * 0.5f, -bestI * step);
    const double tTurn = now_ms() - t0;
    printf("%s page %d, within %.0f degrees: ours %.1f ms (turn %+.2f); the frame upright %.1f ms, turned %.1f ms; "
           "the work of k2pdfopt %.0f ms (%d angles %.0f ms, the frame turned %.1f ms; tilt %+.2f); "
           "page read and first frame %.1f ms\n",
           argv[2], p, maxDegrees, tOurs, view.angle, tUp, tTurned, tAngles + tTurn, na * 2 + 1, tAngles, tTurn,
           -bestI * step, tRead);
    delete doc;
    return 0;
}

// pagebench turns <without|with> <file> <first page from 0> <pages> <max degrees>
// What a page costs with the levelling and without it: run once each way on the same pages and the
// two means are compared. One way a run, because the library of DjVu keeps the pages it has
// decoded for the whole program: a second document of the same run got them for nothing, and the
// first cut of this bench showed the levelling as the faster way. Nothing is prepared ahead, so
// this is the whole work of a page (on a reader it is done in the pause, while the page before is
// read). Frame of 1404 x 1872.
//   without   the frames of "whole content" are laid out (the page is read, its content box found)
//             and the first one is drawn upright
//   with      the same laying out, which tells the document the zoom of the frames to come so
//             that it decodes the page once at that size; then levelView (the tilt of the page
//             and its content as it lies turned) and that frame is drawn, turned when the page is
//             tilted. So "with" has the content box of the upright page in it too, which a turned
//             page does not need: the number is the upper bound.
// The time of a page is given whole and by parts: the page read, the search, the frame.
static int turnsMode(int argc, char ** argv)
{
    if (argc < 7 || (strcmp(argv[2], "with") != 0 && strcmp(argv[2], "without") != 0)) {
        fprintf(stderr, "usage: pagebench turns <without|with> <file> <first page> <pages> <max degrees>\n");
        return 2;
    }
    const bool levelling = strcmp(argv[2], "with") == 0;
    const int first = atoi(argv[4]), count = atoi(argv[5]);
    const float maxDegrees = (float)atof(argv[6]);
    const int w = 1404, h = 1872;
    std::string error;
    PbPageDoc * doc = PbPageDoc::open(argv[3], &error);
    if (!doc) {
        fprintf(stderr, "%s: %s\n", argv[3], error.c_str());
        return 1;
    }
    std::vector<unsigned char> buffer((size_t)w * h);
    double sum = 0, sumRead = 0, sumSearch = 0, sumFrame = 0, worst = 0;
    int pages = 0, turnedPages = 0;
    for (int p = first; p < first + count && p < doc->pageCount(); p++) {
        std::vector<PbPageView> frames;
        doc->readMs = doc->boxMs = doc->levelMs = 0;
        const double t0 = now_ms();
        doc->framesOf(p, PB_PAGE_SCALE_CONTENT, PB_PAGE_LEAF_PAGE, 0, PbPageOwnFrame(), w, h, frames);
        PbPageView view = frames[0];
        if (levelling)
            doc->levelView(p, maxDegrees, w, h, view);
        doc->render(p, view, &buffer[0], w, h, w);
        const double all = now_ms() - t0, search = doc->boxMs + doc->levelMs, frame = all - doc->readMs - search;
        if (getenv("PB_BY_PAGE"))
            printf("page %d %s: %.1f ms (read %.1f, %s %.1f, frame %.1f), turn %+.2f\n", p, argv[2], all, doc->readMs,
                   levelling ? "content box and levelling" : "content box", search, frame, view.angle);
        sum += all;
        sumRead += doc->readMs;
        sumSearch += search;
        sumFrame += frame;
        if (all > worst)
            worst = all;
        pages++;
        if (view.angle != 0)
            turnedPages++;
    }
    if (pages > 0)
        printf("%-7s mean of %d pages (%d turned): %.1f ms (read %.1f, %s %.1f, frame %.1f), the slowest page %.1f ms\n",
               argv[2], pages, turnedPages, sum / pages, sumRead / pages,
               levelling ? "content box and levelling" : "content box", sumSearch / pages, sumFrame / pages, worst);
    delete doc;
    return 0;
}

// pagebench ribbon <file> <first page> <screens> <scale>
// The leafing "as a ribbon" as the window makes it (PbPageWindow::drawRibbon): the strips of the
// pages from the first one are drawn whole, then in parts of a screen, and the two are compared
// pixel by pixel; then the time of the screens of a walk down the ribbon.
static int ribbonMode(int argc, char ** argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: pagebench ribbon <file> <first page> <screens> <scale 0..5>\n");
        return 2;
    }
    const int W = 1404, H = 1872, gap = H / 60, step = H - H / 20;
    int page = atoi(argv[3]);
    const int screens = atoi(argv[4]), scale = atoi(argv[5]);
    std::string error;
    PbPageDoc * doc = PbPageDoc::open(argv[2], &error);
    if (!doc) {
        fprintf(stderr, "%s: %s\n", argv[2], error.c_str());
        return 1;
    }
    PbPageOwnFrame own;
    // 1. a strip drawn whole and in parts
    for (int p = page; p < page + 2 && p < doc->pageCount(); p++) {
        PbPageStrip strip;
        doc->stripOf(p, scale, 0, own, W, H, strip);
        std::vector<unsigned char> whole((size_t)W * strip.height), parts((size_t)W * strip.height);
        doc->renderStrip(p, strip, 0, strip.height, &whole[0], W, W);
        // parts of uneven sizes, as the screens cut a page
        static const int sizes[] = { 701, 333, 1872, 517 };
        int from = 0, n = 0, pieces = 0;
        while (from < strip.height) {
            int to = from + sizes[n++ % 4];
            if (to > strip.height)
                to = strip.height;
            doc->renderStrip(p, strip, from, to, &parts[(size_t)from * W], W, W);
            from = to;
            pieces++;
        }
        size_t differ = 0;
        int worst = 0;
        for (size_t i = 0; i < whole.size(); i++) {
            const int d = whole[i] > parts[i] ? whole[i] - parts[i] : parts[i] - whole[i];
            if (d)
                differ++;
            if (d > worst)
                worst = d;
        }
        printf("page %d: strip %d rows at zoom %.4f, angle %.2f; %d parts against the whole: %lu pixels of %lu "
               "differ, the largest difference %d of 255\n", p + 1, strip.height, strip.zoom, strip.angle, pieces,
               (unsigned long)differ, (unsigned long)whole.size(), worst);
    }
    // 2. a walk down the ribbon
    delete doc;
    doc = PbPageDoc::open(argv[2], &error);
    std::vector<unsigned char> screen((size_t)W * H);
    int row = 0;
    double total = 0, worstMs = 0;
    int done = 0, lastPage = page;
    long shownRows = 0;
    for (int n = 0; n < screens && page < doc->pageCount(); n++) {
        const double t0 = now_ms();
        memset(&screen[0], 0xFF, screen.size());
        int y = 0;
        for (int p = page; y < H && p < doc->pageCount(); p++) {
            PbPageStrip strip;
            doc->stripOf(p, scale, 0, own, W, H, strip);
            const int from = p == page ? row : 0;
            int rows = strip.height - from;
            if (rows > H - y)
                rows = H - y;
            doc->renderStrip(p, strip, from, from + rows, &screen[(size_t)y * W], W, W);
            y += rows + gap;
            shownRows += rows;
            lastPage = p;
        }
        const double ms = now_ms() - t0;
        total += ms;
        if (ms > worstMs)
            worstMs = ms;
        done++;
        // the next screen, as PbPageWindow::ribbonMove goes
        int delta = step;
        bool end = false;
        for (;;) {
            PbPageStrip strip;
            doc->stripOf(page, scale, 0, own, W, H, strip);
            const int rest = strip.height - row;
            if (delta < rest) {
                row += delta;
                break;
            }
            if (page + 1 >= doc->pageCount()) {
                end = true;
                break;
            }
            delta -= rest + gap;
            page++;
            row = 0;
            if (delta <= 0)
                break;
        }
        if (end)
            break;
    }
    printf("ribbon, scale %d: %d screens down to page %d, %.1f ms a screen (the slowest %.1f), %ld rows of pages shown\n",
           scale, done, lastPage + 1, done ? total / done : 0, worstMs, shownRows);
    delete doc;
    return 0;
}

int main(int argc, char ** argv)
{
    if (argc > 1 && strcmp(argv[1], "ribbon") == 0)
        return ribbonMode(argc, argv);
    if (argc > 1 && strcmp(argv[1], "level") == 0)
        return levelMode(argc, argv);
    if (argc > 1 && strcmp(argv[1], "turns") == 0)
        return turnsMode(argc, argv);
    if (argc > 1 && strcmp(argv[1], "cost") == 0)
        return costMode(argc, argv);
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
    static const char * const scales[] = { "whole page", "page width", "whole content", "content width", "own frame",
                                           "automatic" };
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
