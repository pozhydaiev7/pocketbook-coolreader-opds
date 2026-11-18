// Test bench of the CoolReader engine, second edition: one source for both engines
// (CREngine 3.2 of the branch engine-3.2.59, and the old engine of master with -DBENCH_ENGINE_OLD).
// Opens a book, draws its pages the way the PocketBook build does (page image, then a copy to the
// screen canvas) and prints the CRC32 of every page: the same lines before and after an engine
// change mean the same pixels on the screen.
//
// cr3bench <fonts dir> <hyph dir> <css file> <book> <bpp> <font size> <kerning 0|1> <max pages>
// stdout: results to compare; stderr: timing.
// Environment:
//   BENCH_PROPS     "name=value;name=value": settings applied with LVDocView::propsApply(), as the
//                   program applies its settings file
//   BENCH_RELAYOUT  1: the time of a layout after a font size change (size + 2, then back)
//   BENCH_CACHE     folder of cache files; the book is saved to its cache file and opened from it
//   BENCH_PROF      parse | load | relayout | draw: only that stage, BENCH_REPEAT times, no output
//   BENCH_W, BENCH_H  size of the view in pixels (1404 x 1872 by default)
//   BENCH_DIALOG    1: the view is set up as a text window of the program (CRViewDialog: the key
//                   functions, the book information): the css file is the stylesheet of the window,
//                   no page header, margins of 4 px, one page
//   BENCH_DUMP      folder: every drawn page is written there as <book>-p<N>.pgm (8 bpp only)
//   BENCH_FACE      font face of the text (Georgia by default; Arial is registered too)
#include <crengine.h>
#include <lvrend.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <zlib.h>

// BENCH_CITE=<page>: the selection of the citation window (cr3gui/src/citecore.h, copied next to
// this file by the caller) is made on that page and moved by every action of the toolbar buttons;
// the selection after each action is printed.
#if __has_include("citecore.h")
#include "citecore.h"
#define BENCH_HAS_CITE 1
#endif

#ifdef BENCH_ENGINE_OLD
// globals the engine takes from the PocketBook GUI (cr3pocketbook.cpp)
lString16 currentCacheDir;
lString16 openedCacheFile;
typedef lString16 benchString;
#define BENCH_SET_KERNING(on) fontMan->setKerning(on)
#else
typedef lString32 benchString;
#define BENCH_SET_KERNING(on) fontMan->SetKerning(on)
#endif

static double nowMs()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// The clock of the page header reads time(): this definition stands for the one of libc in the
// whole program, so a test sets the minute the header shows (BENCH_PREPARE).
static time_t benchFakeTime = 0;
extern "C" time_t time(time_t * t)
{
    time_t now = benchFakeTime;
    if (now == 0) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        now = tv.tv_sec;
    }
    if (t != NULL)
        *t = now;
    return now;
}

static unsigned long bufCrc(LVDrawBuf * buf)
{
    return crc32(0, (const Bytef *)buf->GetScanLine(0), (uInt)(buf->GetRowSize() * buf->GetHeight()));
}

// BENCH_TIMEOUT: seconds a run may take. The stage it was in is printed, and the program ends
// with exit(), so a profiling build still writes its gmon.out.
static const char * benchStage = "start";
static const char * benchName = "";
static void onTimeout(int)
{
    fprintf(stderr, "%-26s TIMEOUT in the stage: %s\n", benchName, benchStage);
    exit(124);
}

// BENCH_SAMPLE=<file>: sampling profiler of the stage named by BENCH_PROF. A timer of the processor
// time interrupts the program every millisecond and the call stack is stored; at the exit the
// stacks are written to the file as addresses, and sample_report.py turns them into the time by
// function (own time and time with everything called). The build needs no -pg, so the program
// runs at its usual speed and nothing is lost, the functions of libc included.
#include <execinfo.h>
#include <link.h>
#define BENCH_SAMPLE_DEPTH 48
#define BENCH_SAMPLE_MAX 400000
static void ** benchSamples = NULL;
static unsigned char * benchSampleDepth = NULL;
static volatile int benchSampleCount = 0;
static volatile bool benchSampleOn = false;
static const char * benchSampleFile = NULL;
static void benchSampleHandler(int)
{
    int n = benchSampleCount;
    if (!benchSampleOn || n >= BENCH_SAMPLE_MAX)
        return;
    benchSampleDepth[n] = (unsigned char)backtrace(benchSamples + (size_t)n * BENCH_SAMPLE_DEPTH, BENCH_SAMPLE_DEPTH);
    benchSampleCount = n + 1;
}
static int benchSampleRange(struct dl_phdr_info * info, size_t, void * data)
{
    // the first object is the program itself
    unsigned long * range = (unsigned long *)data;
    range[0] = (unsigned long)info->dlpi_addr;
    unsigned long end = 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_LOAD) {
            unsigned long e = (unsigned long)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr + info->dlpi_phdr[i].p_memsz);
            if (e > end)
                end = e;
        }
    }
    range[1] = end;
    return 1;
}
static void benchSampleDump()
{
    benchSampleOn = false;
    struct itimerval off;
    memset(&off, 0, sizeof(off));
    setitimer(ITIMER_PROF, &off, NULL);
    FILE * f = fopen(benchSampleFile, "wb");
    if (f == NULL)
        return;
    unsigned long range[2] = { 0, 0 };
    dl_iterate_phdr(benchSampleRange, range);
    fprintf(f, "range %lx %lx\n", range[0], range[1]);
    for (int n = 0; n < benchSampleCount; n++) {
        void ** s = benchSamples + (size_t)n * BENCH_SAMPLE_DEPTH;
        for (int i = 0; i < benchSampleDepth[n]; i++)
            fprintf(f, i ? " %lx" : "%lx", (unsigned long)s[i]);
        fprintf(f, "\n");
    }
    fclose(f);
}
static void benchSampleStart()
{
    benchSampleFile = getenv("BENCH_SAMPLE");
    if (benchSampleFile == NULL || !*benchSampleFile)
        return;
    benchSamples = (void **)malloc(sizeof(void *) * (size_t)BENCH_SAMPLE_MAX * BENCH_SAMPLE_DEPTH);
    benchSampleDepth = (unsigned char *)malloc(BENCH_SAMPLE_MAX);
    // the first call loads the unwinder: not from the signal handler
    void * warm[4];
    backtrace(warm, 4);
    atexit(benchSampleDump);
    signal(SIGPROF, benchSampleHandler);
    struct itimerval tick;
    tick.it_interval.tv_sec = 0;
    tick.it_interval.tv_usec = 1000;
    tick.it_value = tick.it_interval;
    benchSampleOn = true;
    setitimer(ITIMER_PROF, &tick, NULL);
}

// The font size the view takes for the next bigger size: the engine knows a list of sizes only
#ifndef BENCH_ENGINE_OLD
#include <map>
#include <string>
// BENCH_ELEMFACES: element name -> font face -> number of elements with text of their own
static void benchElementFacesWalk(ldomNode * n, std::map<std::string, std::map<std::string, int> > & faces, int depth)
{
    if (n == NULL || !n->isElement() || depth > 300)
        return;
    int count = n->getChildCount();
    bool text = false;
    for (int i = 0; i < count; i++) {
        ldomNode * c = n->getChildNode(i);
        if (c == NULL)
            continue;
        if (c->isText())
            text = true;
        else
            benchElementFacesWalk(c, faces, depth + 1);
    }
    if (text) {
        LVFontRef font = n->getFont();
        std::string name = UnicodeToUtf8(n->getNodeName()).c_str();
        std::string face = font.isNull() ? "(none)" : font->getTypeFace().c_str();
        faces[name][face]++;
    }
}

static void benchElementFaces(LVDocView & view, const char * book)
{
    std::map<std::string, std::map<std::string, int> > faces;
    benchElementFacesWalk(view.getDocument()->getRootNode(), faces, 0);
    fprintf(stderr, "%s faces:", book);
    for (std::map<std::string, std::map<std::string, int> >::iterator e = faces.begin(); e != faces.end(); ++e) {
        fprintf(stderr, " %s[", e->first.c_str());
        bool first = true;
        for (std::map<std::string, int>::iterator f = e->second.begin(); f != e->second.end(); ++f) {
            fprintf(stderr, "%s%s x%d", first ? "" : ", ", f->first.c_str(), f->second);
            first = false;
        }
        fprintf(stderr, "]");
    }
    fprintf(stderr, "\n");
}
#endif

static int biggerFontSize(LVDocView & view, int size)
{
    int cur = view.getFontSize();
    for (int add = 2; add <= 16; add += 2) {
        view.setFontSize(size + add);
        if (view.getFontSize() != cur)
            return size + add;
    }
    return size;
}

static int dx = 1404, dy = 1872;
static lString8 benchCss;
static bool benchHasCss = false;
static int benchFontSize = 44;

// The settings of a view, the same for every view of a run
static void setupView(LVDocView & view)
{
    if (benchHasCss)
        view.setStyleSheet(benchCss);
    view.setBackgroundColor(0xFFFFFF);
    view.setTextColor(0x000000);
    if (getenv("BENCH_DIALOG") != NULL) {
        // as CRViewDialog::CRViewDialog() and the callers of the text windows do
        view.setDefaultFontFace(lString8("Arial"));
        LVArray<int> all;
        for (int s = 12; s <= 120; s++)
            all.add(s);
        view.setFontSizes(all, false);
        view.setFontSize(benchFontSize);
        view.setPageMargins(lvRect(4, 4, 4, 4));
        view.setShowCover(false);
        view.setPageHeaderInfo(0);
        view.Resize(dx, dy);
        view.setVisiblePageCount(1);
        return;
    }
    view.setDefaultFontFace(lString8(getenv("BENCH_FACE") ? getenv("BENCH_FACE") : "Georgia"));
    view.setStatusFontFace(lString8("Arial"));
    // every size is allowed, as the list of the program allows its sizes: the engine takes the
    // nearest size of its list, and its own list ends below the sizes read with
    LVArray<int> sizes;
    for (int s = 12; s <= 120; s++)
        sizes.add(s);
    view.setFontSizes(sizes, false);
    view.setFontSize(benchFontSize);
    // no clock and no battery in the page header: they would change the page between runs
    view.setPageHeaderInfo(PGHDR_PAGE_NUMBER | PGHDR_PAGE_COUNT | PGHDR_AUTHOR | PGHDR_TITLE | PGHDR_CHAPTER_MARKS);
    view.Resize(dx, dy);
    view.setViewMode(DVM_PAGES, 1);
    const char * p = getenv("BENCH_PROPS");
    if (p != NULL && *p) {
        CRPropRef props = LVCreatePropsContainer();
        lString8 all(p);
        int start = 0;
        while (start < (int)all.length()) {
            int end = start;
            while (end < (int)all.length() && all[end] != ';')
                end++;
            lString8 item = all.substr(start, end - start);
            int eq = item.pos("=");
            if (eq > 0)
                props->setString(item.substr(0, eq).c_str(), Utf8ToUnicode(item.substr(eq + 1)));
            start = end + 1;
        }
        view.propsApply(props);
    }
}

int main(int argc, char ** argv)
{
    if (argc < 9) {
        fprintf(stderr, "usage: cr3bench <fonts dir> <hyph dir> <css file> <book> <bpp> <font size> "
                        "<kerning 0|1> <max pages>\n");
        return 2;
    }
    const char * fontDir = argv[1];
    const char * hyphDir = argv[2];
    const char * cssFile = argv[3];
    const char * book = argv[4];
    int bpp = atoi(argv[5]);
    benchFontSize = atoi(argv[6]);
    bool kerning = atoi(argv[7]) != 0;
    int maxPages = atoi(argv[8]);
    const char * name = strrchr(book, '/');
    name = name ? name + 1 : book;
    benchName = name;
    // BENCH_LOG=1: the log of the engine goes to stderr (level INFO; BENCH_LOG=2: DEBUG)
    if (getenv("BENCH_LOG") != NULL) {
        CRLog::setStderrLogger();
        CRLog::setLogLevel(atoi(getenv("BENCH_LOG")) > 1 ? CRLog::LL_DEBUG : CRLog::LL_INFO);
    }
    if (getenv("BENCH_TIMEOUT") != NULL) {
        signal(SIGALRM, onTimeout);
        alarm(atoi(getenv("BENCH_TIMEOUT")));
    }
    if (getenv("BENCH_W") != NULL && atoi(getenv("BENCH_W")) > 0)
        dx = atoi(getenv("BENCH_W"));
    if (getenv("BENCH_H") != NULL && atoi(getenv("BENCH_H")) > 0)
        dy = atoi(getenv("BENCH_H"));

    InitFontManager(lString8());
    int fonts = 0;
    // a fixed list: the registration order must not depend on the directory order
    static const char * const fontFiles[] = { "georgia.ttf", "georgiab.ttf", "georgiai.ttf", "georgiaz.ttf",
                                              "arial.ttf", "arialbd.ttf", NULL };
#ifndef BENCH_ENGINE_OLD
    // BENCH_FONTFACES=<file>: the kept list of the fonts (RegisterFontFaces, as cr3main.cpp of the
    // program uses it). No such file: the fonts are registered by their files and the faces are
    // written there. The file exists: the fonts are registered from it, no font file is opened
    // for that. The pages of both runs must be the same.
    const char * facesFile = getenv("BENCH_FONTFACES");
    FILE * facesIn = facesFile != NULL && *facesFile ? fopen(facesFile, "rb") : NULL;
    if (facesIn != NULL) {
        static char line[2048];
        lString8 path, faces;
        while (true) {
            bool got = fgets(line, sizeof(line), facesIn) != NULL;
            int n = got ? (int)strlen(line) : 0;
            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
                line[--n] = 0;
            if (!got || line[0] == 'F') {
                if (!path.empty() && fontMan->RegisterFontFaces(path, faces))
                    fonts++;
                if (!got)
                    break;
                path = lString8(line + 2);
                faces.clear();
            } else if (line[0] == 'D') {
                faces << (line + 2) << "\n";
            }
        }
        fclose(facesIn);
        fprintf(stderr, "fonts: %d files registered from the list %s\n", fonts, facesFile);
    } else {
        FILE * facesOut = facesFile != NULL && *facesFile ? fopen(facesFile, "wb") : NULL;
        for (int i = 0; fontFiles[i] != NULL; i++) {
            lString8 path = lString8(fontDir) + "/" + fontFiles[i];
            if (fontMan->RegisterFont(path))
                fonts++;
            if (facesOut != NULL) {
                fprintf(facesOut, "F %s\n", path.c_str());
                lString8 faces = fontMan->GetLastRegisteredFaces();
                int start = 0;
                while (start < (int)faces.length()) {
                    int end = start;
                    while (end < (int)faces.length() && faces[end] != '\n')
                        end++;
                    fprintf(facesOut, "D %s\n", faces.substr(start, end - start).c_str());
                    start = end + 1;
                }
            }
        }
        if (facesOut != NULL)
            fclose(facesOut);
    }
#else
    for (int i = 0; fontFiles[i] != NULL; i++) {
        lString8 path = lString8(fontDir) + "/" + fontFiles[i];
        if (fontMan->RegisterFont(path))
            fonts++;
    }
#endif
    if (fonts == 0) {
        fprintf(stderr, "no fonts in %s\n", fontDir);
        return 3;
    }
    BENCH_SET_KERNING(kerning);
    // The folder must end with "/": without it the engines take the name for an archive, find no
    // dictionaries and hyphenate by their built-in algorithm (the runs before 2026-11-12 night
    // did so). BENCH_NOHYPHDICT=1 keeps that for comparisons with those runs.
    lString8 hyphFolder(hyphDir);
    if (getenv("BENCH_NOHYPHDICT") == NULL && !hyphFolder.empty() && hyphFolder[hyphFolder.length() - 1] != '/')
        hyphFolder << "/";
#ifndef BENCH_ENGINE_OLD
    // BENCH_HYPHCACHE=<file>: the headers of the pattern files are kept there between the runs
    // (HyphSetDictListCacheFile): the list of the dictionaries must be the same with and without it
    if (getenv("BENCH_HYPHCACHE") != NULL)
        HyphSetDictListCacheFile(getenv("BENCH_HYPHCACHE"));
#endif
    // BENCH_PROF=hyphlist: only the list of the hyphenation dictionaries, made BENCH_REPEAT times
    // (what a start of the program does once); the time of one pass is printed
    if (getenv("BENCH_PROF") != NULL && strcmp(getenv("BENCH_PROF"), "hyphlist") == 0) {
        int times = getenv("BENCH_REPEAT") ? atoi(getenv("BENCH_REPEAT")) : 20;
        double a = nowMs();
        for (int r = 0; r < times; r++)
            HyphMan::initDictionaries(Utf8ToUnicode(hyphFolder));
        HyphDictionaryList * list = HyphMan::getDictList();
        fprintf(stderr, "dictionary list: %.2f ms a pass (%d passes), %d dictionaries\n", (nowMs() - a) / times, times,
                list ? list->length() : 0);
        return 0;
    }
    HyphMan::initDictionaries(Utf8ToUnicode(hyphFolder));
    bool hyphDict = HyphMan::activateDictionary(benchString("Russian_EnUS_hyphen_(Alan).pdb"));
    if (!hyphDict && getenv("BENCH_NOHYPHDICT") == NULL)
        fprintf(stderr, "hyphenation dictionary Russian_EnUS_hyphen_(Alan).pdb is NOT active (folder %s)\n",
                hyphFolder.c_str());
    benchHasCss = LVLoadStylesheetFile(benchString(cssFile), benchCss);

    const char * prof = getenv("BENCH_PROF");
    int repeat = getenv("BENCH_REPEAT") ? atoi(getenv("BENCH_REPEAT")) : 20;
#ifndef BENCH_ENGINE_OLD
    // BENCH_IMAGE=<picture file>: the picture is decoded and drawn over the whole canvas, as the
    // program draws its start logo and the backgrounds of its windows, BENCH_REPEAT times; the
    // time of one drawing and the CRC of the canvas are printed (the book argument is not used)
    const char * imageFile = getenv("BENCH_IMAGE");
    if (imageFile != NULL && *imageFile) {
        LVGrayDrawBuf canvas(dx, dy, bpp);
        benchSampleStart();
        double a = nowMs();
        for (int r = 0; r < repeat; r++) {
            LVStreamRef stream = LVOpenFileStream(imageFile, LVOM_READ);
            LVImageSourceRef img = stream.isNull() ? LVImageSourceRef() : LVCreateStreamCopyImageSource(stream);
            if (img.isNull()) {
                fprintf(stderr, "cannot read the picture %s\n", imageFile);
                return 6;
            }
            canvas.Clear(0xFFFFFF);
            canvas.Draw(img, 0, 0, dx, dy, true);
        }
        fprintf(stderr, "picture %s on %d x %d, %d bpp: %.2f ms a drawing (%d drawings), crc %08lx\n", imageFile, dx, dy,
                bpp, (nowMs() - a) / repeat, repeat,
                (unsigned long)crc32(0, (const Bytef *)canvas.GetScanLine(0), (uInt)(canvas.GetRowSize() * canvas.GetHeight())));
        return 0;
    }
#endif
    if (prof != NULL && (strcmp(prof, "load") == 0 || strcmp(prof, "parse") == 0)) {
        bool layout = strcmp(prof, "load") == 0;
        benchSampleStart();
        for (int r = 0; r < repeat; r++) {
            LVDocView v(bpp);
            setupView(v);
            benchStage = "parse";
            if (!v.LoadDocument(book))
                return 4;
            benchStage = "layout";
            if (layout)
                v.checkRender();
        }
        return 0;
    }

    // BENCH_HYPH=<file>: the hyphenation dictionaries of the hyph folder. Every line of the file
    // is "<dictionary id> <word> <word> ..."; the dictionary is activated and every word is
    // printed with its hyphenation points. Before that, the list of the dictionaries the engine
    // has accepted. No book is opened (the book argument is not used).
    const char * hyphFile = getenv("BENCH_HYPH");
    if (hyphFile != NULL && *hyphFile) {
        HyphDictionaryList * list = HyphMan::getDictList();
        printf("dictionaries: %d\n", list ? list->length() : 0);
        for (int i = 0; list && i < list->length(); i++) {
#ifdef BENCH_ENGINE_OLD
            printf("  %s | %s\n", UnicodeToUtf8(list->get(i)->getId()).c_str(),
                   UnicodeToUtf8(list->get(i)->getTitle()).c_str());
#else
            printf("  %s | %s | %s\n", UnicodeToUtf8(list->get(i)->getId()).c_str(),
                   UnicodeToUtf8(list->get(i)->getTitle()).c_str(),
                   UnicodeToUtf8(list->get(i)->getLangTag()).c_str());
#endif
        }
        FILE * f = fopen(hyphFile, "rb");
        if (f == NULL) {
            fprintf(stderr, "cannot open %s\n", hyphFile);
            return 5;
        }
        static char line[4096];
        while (fgets(line, sizeof(line), f) != NULL) {
            lString8 s(line);
            while (s.length() > 0 && (s[s.length() - 1] == '\n' || s[s.length() - 1] == '\r'))
                s.erase(s.length() - 1, 1);
            if (s.empty())
                continue;
            int sp = s.pos(" ");
            lString8 id = sp > 0 ? s.substr(0, sp) : s;
            bool ok = HyphMan::activateDictionary(Utf8ToUnicode(id));
#ifndef BENCH_ENGINE_OLD
            // CREngine 3.2 chooses the dictionary by the language tag of the chosen one: which
            // dictionary really hyphenates, and how many patterns the chosen file gave
            HyphMethod * used = TextLangMan::getMainLangHyphMethod();
            HyphMethod * own = HyphMan::getHyphMethodForDictionary(Utf8ToUnicode(id));
            lString8 usedId = used ? UnicodeToUtf8(used->getId()) : lString8("-");
            if (own == NULL || own->getPatternsCount() == 0 || usedId != id)
                ok = false;
            printf("%s: %s [used %s, %d patterns, limits %d/%d]", id.c_str(), ok ? "ok" : "NOT LOADED",
                   usedId.c_str(), own ? (int)own->getPatternsCount() : 0, own ? own->getLeftHyphenMin() : 0,
                   own ? own->getRightHyphenMin() : 0);
#else
            printf("%s: %s", id.c_str(), ok ? "ok" : "NOT LOADED");
#endif
            int start = sp > 0 ? sp + 1 : s.length();
            while (ok && start < (int)s.length()) {
                int end = start;
                while (end < (int)s.length() && s[end] != ' ')
                    end++;
                benchString word = Utf8ToUnicode(s.substr(start, end - start));
                start = end + 1;
                int len = word.length();
                if (len == 0 || len > 100)
                    continue;
                lUInt16 widths[128];
                lUInt8 flags[128];
                for (int i = 0; i < len; i++) {
                    widths[i] = (lUInt16)((i + 1) * 10);
                    flags[i] = 0;
                }
                HyphMan::hyphenate(word.c_str(), len, widths, flags, 5, 30000);
                benchString res;
                for (int i = 0; i < len; i++) {
                    res << word[i];
                    if ((flags[i] & LCHAR_ALLOW_HYPH_WRAP_AFTER) && i < len - 1)
                        res << '-';
                }
                printf(" %s", UnicodeToUtf8(res).c_str());
            }
            printf("\n");
        }
        fclose(f);
        return 0;
    }

    // BENCH_HIST=<history file>, BENCH_HIST_MODE=save|load: the reading position between the engines.
    //   save  the book is opened, a page in the middle (BENCH_HIST_PAGE, a share of the book in
    //         1/1000) becomes the position, the history is written to the file
    //   load  the history is read from the file, the book is opened as the program opens it, the
    //         position is restored; the history is written to <history file>.out
    // Printed: the page, the page count and the beginning of the text of the page.
    const char * histFile = getenv("BENCH_HIST");
    if (histFile != NULL && *histFile) {
        const char * mode = getenv("BENCH_HIST_MODE");
        bool save = mode != NULL && strcmp(mode, "save") == 0;
        LVDocView v(bpp);
        setupView(v);
        if (!save) {
            LVStreamRef in = LVOpenFileStream(histFile, LVOM_READ);
            bool ok = !in.isNull() && v.getHistory()->loadFromStream(in);
            fprintf(stderr, "history read: %s, %d records\n", ok ? "ok" : "FAILED",
                    v.getHistory()->getRecords().length());
        }
        if (!v.LoadDocument(book)) {
            fprintf(stderr, "%-26s LoadDocument FAILED\n", name);
            return 4;
        }
        if (save) {
            v.checkRender();
            int share = getenv("BENCH_HIST_PAGE") ? atoi(getenv("BENCH_HIST_PAGE")) : 500;
            // the first request of the position after a layout puts the view to its start
            v.getCurPage();
            v.goToPage(v.getPageCount() * share / 1000);
        } else {
            v.restorePosition();
            v.checkRender();
        }
        int page = v.getCurPage();
        LVRef<ldomXRange> range = v.getPageDocumentRange(page);
        lString8 text = range.isNull() ? lString8("-") : UnicodeToUtf8(range->getRangeText().substr(0, 70));
        for (int i = 0; i < (int)text.length(); i++)
            if (text[i] == '\n' || text[i] == '\r')
                text[i] = ' ';
        // the pointer of the position: the top of the page when saved, the one of the history
        // record when restored; and the text it points to, which must be the same in both engines
        ldomXPointer bm = v.getBookmark();
        lString8 posName = bm.isNull() ? lString8("-") : UnicodeToUtf8(bm.toString());
        if (!save && v.getHistory()->getRecords().length() > 0) {
            posName = UnicodeToUtf8(v.getHistory()->getRecords()[0]->getLastPos()->getStartPos());
            bm = v.getDocument()->createXPointer(Utf8ToUnicode(posName));
        }
        lString8 at("-");
        if (!bm.isNull() && bm.getNode() != NULL) {
            benchString all = bm.getNode()->getText();
            int from = bm.getNode()->isText() ? bm.getOffset() : 0;
            if (from > (int)all.length())
                from = all.length();
            at = UnicodeToUtf8(all.substr(from, 60));
        }
        fprintf(stderr, "%-26s %s: page %d of %d, records %d, position %s\n    text at the position: %s\n", name,
                save ? "saved" : "restored", page + 1, v.getPageCount(), v.getHistory()->getRecords().length(),
                posName.c_str(), at.c_str());
        v.savePosition();
        lString8 outName(histFile);
        if (!save)
            outName += ".out";
        LVStreamRef out = LVOpenFileStream(outName.c_str(), LVOM_WRITE);
        if (out.isNull() || !v.getHistory()->saveToStream(out.get()))
            fprintf(stderr, "history write FAILED: %s\n", outName.c_str());
        return 0;
    }

    const char * cacheDir = getenv("BENCH_CACHE");
    if (cacheDir != NULL && *cacheDir) {
        // The book is opened, laid out and saved to its cache file; then it is opened by a new view
        ldomDocCache::init(Utf8ToUnicode(lString8(cacheDir)), (lvsize_t)512 * 1024 * 1024);
        int pages1 = 0, pages2 = 0;
        double tSave = 0, tOpen = 0, tOpenLayout = 0;
        {
            LVDocView v(bpp);
            setupView(v);
            if (!v.LoadDocument(book))
                return 4;
            v.checkRender();
            pages1 = v.getPageCount();
            double a = nowMs();
            CRTimerUtil infinite;
            v.swapToCache(infinite);
            tSave = nowMs() - a;
        }
        {
            LVDocView v(bpp);
            setupView(v);
            double a = nowMs();
            if (!v.LoadDocument(book))
                return 4;
            double b = nowMs();
            v.checkRender();
            pages2 = v.getPageCount();
            tOpen = b - a;
            tOpenLayout = nowMs() - b;
        }
        fprintf(stderr, "%-26s cache: save %6.0f ms, open %5.0f ms + layout %5.0f ms, pages %d / %d\n", name, tSave,
                tOpen, tOpenLayout, pages1, pages2);
        return 0;
    }

    // the first view: the fonts of the text and of the page header are loaded here
    double tSetup0 = nowMs();
    LVDocView view(bpp);
    setupView(view);
    double tSetup = nowMs() - tSetup0;

    double t0 = nowMs();
    benchStage = "parse";
    if (!view.LoadDocument(book)) {
        fprintf(stderr, "cannot open %s\n", book);
        return 4;
    }
    double tParse = nowMs() - t0;
    // the layout of the whole book is made on the first request, as on the device
    benchStage = "layout";
    view.checkRender();
    int pages = view.getPageCount();
    double tLoad = nowMs() - t0;

#ifdef BENCH_HAS_CITE
    if (getenv("BENCH_CITE") != NULL) {
        benchStage = "citation selection";
        view.goToPage(atoi(getenv("BENCH_CITE")));
        view.getPageImage(0);
        CiteSelection sel(view);
        static const struct {
            const char * name;
            void (CiteSelection::*action)();
        } steps[] = {
            { "A down", &CiteSelection::moveDown },
            { "A down", &CiteSelection::moveDown },
            { "A up", &CiteSelection::moveUp },
            { "A+ down", &CiteSelection::growDown },
            { "A+ up", &CiteSelection::growUp },
            { "A- up", &CiteSelection::shrinkUp },
            { "A- down", &CiteSelection::shrinkDown },
            { "C+ down", &CiteSelection::growDownPhrase },
            { "C+ up", &CiteSelection::growUpPhrase },
            { "C- up", &CiteSelection::shrinkUpPhrase },
            { "C- down", &CiteSelection::shrinkDownPhrase },
            { NULL, NULL }
        };
        ldomXRange r = sel.select();
        printf("%-8s [%s : %s]\n", "start", UnicodeToUtf8(r.getStart().toString()).c_str(),
               UnicodeToUtf8(r.getEnd().toString()).c_str());
        for (int i = 0; steps[i].name != NULL; i++) {
            ldomXRange before = sel.select();
            (sel.*steps[i].action)();
            ldomXRange after = sel.select();
            bool same = before.getStart().compare(after.getStart()) == 0 && before.getEnd().compare(after.getEnd()) == 0;
            printf("%-8s [%s : %s]%s\n", steps[i].name, UnicodeToUtf8(after.getStart().toString()).c_str(),
                   UnicodeToUtf8(after.getEnd().toString()).c_str(), same ? "   NOTHING CHANGED" : "");
        }
#ifndef BENCH_ENGINE_OLD
        // Every button of the toolbar from three selections: a whole paragraph given by text
        // positions (what the window opens with), two paragraphs, and one word inside a
        // paragraph (what a long tap selects). Each line: the text length before and after,
        // and what the step did.
        {
            // the paragraph after the one the window opened with: it has a paragraph on both sides
            sel.setRange(r.getStart(), r.getEnd());
            sel.moveDown();
            ldomXRange para = sel.select();
            ldomXPointerEx pe = para.getEnd(); // a text position: the end of the paragraph
            ldomXPointerEx ps = para.getStart(); // the element of the paragraph
            if (!ps.isText())
                ps.nextVisibleText();
            ps.setOffset(0);
            printf("btn: paragraph [%s : %s]\n", UnicodeToUtf8(ps.toString()).c_str(), UnicodeToUtf8(pe.toString()).c_str());
            // a word in the last text node of the paragraph
            ldomXPointerEx ws = pe, we = pe;
            ws.setOffset(22);
            we.setOffset(27);
            sel.setRange(ps, pe);
            sel.growDown();
            ldomXRange two = sel.select();
            static const struct {
                const char * name;
                int kind; // 0 a step, 1 the tap on A
                void (CiteSelection::*action)();
            } buttons[] = {
                { "A tap", 1, NULL },
                { "A+ tap/down", 0, &CiteSelection::growDown },
                { "A+ up", 0, &CiteSelection::growUp },
                { "A- tap/up", 0, &CiteSelection::shrinkUp },
                { "A- down", 0, &CiteSelection::shrinkDown },
                { "C+ tap/down", 0, &CiteSelection::growDownPhrase },
                { "C+ up", 0, &CiteSelection::growUpPhrase },
                { "C- tap/up", 0, &CiteSelection::shrinkUpPhrase },
                { "C- down", 0, &CiteSelection::shrinkDownPhrase },
                { NULL, 0, NULL }
            };
            for (int state = 0; state < 3; state++) {
                const char * stateName = state == 0 ? "paragraph" : state == 1 ? "two paragraphs" : "word";
                ldomXPointerEx s0 = state == 0 ? ps : state == 1 ? two.getStart() : ws;
                ldomXPointerEx e0 = state == 0 ? pe : state == 1 ? two.getEnd() : we;
                for (int i = 0; buttons[i].name != NULL; i++) {
                    sel.setRange(s0, e0);
                    int before = ldomXRange(s0, e0).getRangeText().length();
                    if (buttons[i].kind == 1) {
                        if (!sel.selectWholeParagraphs())
                            sel.moveDown();
                    } else {
                        (sel.*buttons[i].action)();
                    }
                    ldomXRange r2 = sel.select();
                    int cs = r2.getStart().compare(s0);
                    int ce = r2.getEnd().compare(e0);
                    bool valid = r2.getStart().compare(r2.getEnd()) < 0;
                    int after = valid ? r2.getRangeText().length() : -1;
                    printf("btn: %-14s %-12s %4d -> %4d  start %s, end %s%s%s\n", stateName, buttons[i].name,
                           before, after, cs < 0 ? "earlier" : cs > 0 ? "later" : "same",
                           ce < 0 ? "earlier" : ce > 0 ? "later" : "same",
                           valid ? "" : "   NO TEXT LEFT",
                           (valid && before == after && (cs != 0 || ce != 0)) ? "   SAME LENGTH" : "");
                }
            }
            sel.setRange(ps, pe);
        }
        // Editing of a saved citation, as CiteWindow::startEditing and its "save" do it: the
        // selection is saved as a citation, the selection goes elsewhere, the citation is found
        // again by its saved positions, selected, grown by a paragraph and saved in place.
        {
            ldomXRange saved = sel.select();
            CRBookmark * bmk = view.saveRangeBookmark(saved, bmkt_comment, lString32("note"));
            if (bmk == NULL) {
                printf("edit: NO HISTORY RECORD\n");
                return 1;
            }
            LVPtrVector<CRBookmark> & list = view.getCurrentFileHistRecord()->getBookmarks();
            int count = list.length();
            int place = list.indexOf(bmk);
            lString32 textBefore = bmk->getPosText();
            sel.moveDown();
            sel.moveDown();
            ldomXPointerEx s = view.getDocument()->createXPointer(bmk->getStartPos());
            ldomXPointerEx e = view.getDocument()->createXPointer(bmk->getEndPos());
            bool found = !s.isNull() && !e.isNull() && s.compare(saved.getStart()) == 0
                         && e.compare(saved.getEnd()) == 0;
            view.setHiddenBookmark(bmk);
            sel.setRange(s, e);
            ldomXRange opened = sel.select();
            bool openedSame = opened.getStart().compare(saved.getStart()) == 0
                              && opened.getEnd().compare(saved.getEnd()) == 0;
            sel.growDown();
            ldomXRange edited = sel.select();
            bool updated = view.updateRangeBookmark(bmk, edited);
            view.setHiddenBookmark(NULL);
            lString32 textAfter = bmk->getPosText();
            ldomXPointerEx s2 = view.getDocument()->createXPointer(bmk->getStartPos());
            ldomXPointerEx e2 = view.getDocument()->createXPointer(bmk->getEndPos());
            bool savedNew = !s2.isNull() && !e2.isNull() && s2.compare(edited.getStart()) == 0
                            && e2.compare(edited.getEnd()) == 0;
            printf("edit: positions found %d, opened with the saved text %d, updated %d, new positions saved %d\n",
                   found, openedSame, updated, savedNew);
            printf("edit: citations %d -> %d, place %d -> %d, type kept %d, comment kept %d\n",
                   count, list.length(), place, list.indexOf(bmk), bmk->getType() == bmkt_comment,
                   bmk->getCommentText() == lString32("note"));
            printf("edit: text %d -> %d characters, starts with the old text %d\n",
                   textBefore.length(), textAfter.length(), textAfter.startsWith(textBefore));
            ldomXRange other = sel.select();
            ldomXPointerEx far = other.getEnd();
            CRBookmark stray;
            printf("edit: a citation that is not in the book is refused %d\n",
                   !view.updateRangeBookmark(&stray, other) && !far.isNull());
        }
#endif
        return 0;
    }
#endif
    benchStage = "layout after a font size change";
    if (prof != NULL && strcmp(prof, "relayout") == 0) {
        int bigger = biggerFontSize(view, benchFontSize);
        benchSampleStart();
        for (int r = 0; r < repeat; r++) {
            view.setFontSize((r & 1) ? benchFontSize : bigger);
            view.checkRender();
        }
        return 0;
    }
    double tRelayout = -1, tRelayoutBack = -1;
    if (getenv("BENCH_RELAYOUT") != NULL) {
        double a = nowMs();
        int bigger = biggerFontSize(view, benchFontSize);
        view.checkRender();
        double b = nowMs();
        if (getenv("BENCH_VERBOSE") != NULL)
            fprintf(stderr, "%s: font size %d -> %d (asked %d), %d -> %d pages\n", name, benchFontSize,
                    view.getFontSize(), bigger, pages, view.getPageCount());
        view.setFontSize(benchFontSize);
        view.checkRender();
        tRelayout = b - a;
        tRelayoutBack = nowMs() - b;
        if (view.getPageCount() != pages)
            fprintf(stderr, "%s: %d pages after the font size was put back, %d before\n", name, view.getPageCount(), pages);
    }
    printf("book %s bpp %d size %d kerning %d pages %d\n", name, bpp, benchFontSize, kerning ? 1 : 0, pages);
#ifndef BENCH_ENGINE_OLD
    // BENCH_ELEMFACES=1: the font faces the elements with text of their own are drawn with,
    // by element name: "h1: Georgia x12, FreeSans x3"
    if (getenv("BENCH_ELEMFACES") != NULL) {
        benchElementFaces(view, name);
        // BENCH_TOGGLE_FONTS=1: then the fonts of the book are turned off in the open book, as
        // the settings menu does it, and the faces are listed again
        if (getenv("BENCH_TOGGLE_FONTS") != NULL) {
            CRPropRef p = LVCreatePropsContainer();
            p->setBool(PROP_EMBEDDED_FONTS, false);
            view.propsApply(p);
            view.checkRender();
            benchElementFaces(view, "  turned off in the open book");
        }
        // BENCH_TOGGLE_STYLES=1: the same for the styles of the book
        if (getenv("BENCH_TOGGLE_STYLES") != NULL) {
            CRPropRef p = LVCreatePropsContainer();
            p->setBool(PROP_EMBEDDED_STYLES, false);
            view.propsApply(p);
            view.checkRender();
            benchElementFaces(view, "  styles turned off in the open book");
        }
    }
#endif

    LVGrayDrawBuf canvas(dx, dy, bpp);
    int count = pages < maxPages ? pages : maxPages;
    // pages spread over the whole book, not only its beginning
    int step = pages > count ? pages / count : 1;
    benchStage = "page drawing";
    if (prof != NULL && strcmp(prof, "draw") == 0) {
        for (int r = 0; r < repeat; r++) {
            for (int i = 0; i < count; i++) {
                view.goToPage(i * step);
                view.clearImageCache();
                LVDocImageRef img = view.getPageImage(0);
                img->getDrawBuf()->DrawTo(&canvas, 0, 0, 0, NULL);
            }
        }
        return 0;
    }
#ifndef BENCH_ENGINE_OLD
    // BENCH_PREPARE=1: the image of the next page prepared ahead (LVDocView::preparePageImage).
    // Runs of consecutive pages at several places of the book are read forward and then back,
    // twice: every page drawn when it is shown, and every page prepared while the one before it
    // is shown. The clock of the page header goes on by a minute at every page turn (BENCH_PROPS
    // turns the clock on), the same in both passes. The images of the two passes must be the same.
    if (getenv("BENCH_PREPARE") != NULL) {
        benchStage = "pages prepared ahead";
        const int runLen = 12;
        int starts[3] = { pages / 6, pages / 2, pages * 5 / 6 };
        LVArray<int> seq;      // pages in the order they are shown
        LVArray<int> seqDir;   // direction of the turn that shows the page: 1, -1; 0 a jump
        for (int s = 0; s < 3; s++) {
            if (starts[s] + runLen >= pages)
                continue;
            for (int i = 0; i < runLen; i++) {
                seq.add(starts[s] + i);
                seqDir.add(i == 0 ? 0 : 1);
            }
            for (int i = runLen - 2; i >= 0; i--) {
                seq.add(starts[s] + i);
                seqDir.add(-1);
            }
        }
        const time_t clockBase = 1795000000;
        // The position asked for before the first draw of a book is not kept: goToPage() asks for
        // the bookmark of the new position, that sets the position not set yet, to the first page.
        // The first page is drawn here, the passes start after it.
        view.getPageImage(0);
        int wrongPage = 0;
        LVArray<unsigned long> shown[2];
        double tTurn[2] = { 0, 0 }, tPrepare = 0;
        int turns = 0, prepared = 0, refused = 0, ready = 0;
        for (int pass = 0; pass < 2; pass++) {
            view.clearImageCache();
            for (int i = 0; i < seq.length(); i++) {
                benchFakeTime = clockBase + 60 * i;
                if (pass == 0) {
                    view.clearImageCache();
                } else if (seqDir[i] != 0) {
                    // the idle time of the page before: the page of this turn is prepared
                    double a = nowMs();
                    bool done = view.preparePageImage(seqDir[i]);
                    tPrepare += nowMs() - a;
                    if (done)
                        prepared++;
                    else if (!view.isPageImageReady(seqDir[i]))
                        refused++;
                }
                double a = nowMs();
                view.goToPage(seq[i]);
                if (pass == 1 && seqDir[i] != 0 && view.isPageImageReady(0))
                    ready++;
                LVDocImageRef img = view.getPageImage(0);
                img->getDrawBuf()->DrawTo(&canvas, 0, 0, 0, NULL);
                double b = nowMs();
                if (view.getCurPage() != seq[i])
                    wrongPage++;
                if (seqDir[i] != 0) {
                    tTurn[pass] += b - a;
                    if (pass == 0)
                        turns++;
                }
                shown[pass].add(bufCrc(img->getDrawBuf()));
                // BENCH_VERBOSE: where an image of the second pass differs from the page drawn anew
                if (pass == 1 && getenv("BENCH_VERBOSE") != NULL && shown[0][i] != shown[1][i]) {
                    LVGrayDrawBuf was(dx, dy, bpp);
                    img->getDrawBuf()->DrawTo(&was, 0, 0, 0, NULL);
                    img = LVDocImageRef();
                    view.clearImageCache();
                    LVDocImageRef fresh = view.getPageImage(0);
                    int first = -1, last = -1, rows = 0;
                    for (int y = 0; y < dy; y++)
                        if (memcmp(was.GetScanLine(y), fresh->getDrawBuf()->GetScanLine(y), was.GetRowSize()) != 0) {
                            if (first < 0)
                                first = y;
                            last = y;
                            rows++;
                        }
                    printf("prepare: page %d: first pass %08lx, second pass %08lx, drawn anew now %08lx; rows %d..%d differ (%d) from the one drawn anew\n",
                           seq[i], shown[0][i], shown[1][i], bufCrc(fresh->getDrawBuf()), first, last, rows);
                }
            }
        }
        benchFakeTime = 0;
        // the pages of a run are different pages: the same image twice in a row would mean a
        // page turn that showed nothing new
        int repeated = 0;
        for (int i = 1; i < seq.length(); i++)
            if (shown[0][i] == shown[0][i - 1])
                repeated++;
        if (wrongPage > 0 || repeated > 0)
            printf("prepare: %d times another page than the one asked for, %d page turns with the same image\n", wrongPage, repeated);
        int differ = 0;
        for (int i = 0; i < seq.length(); i++)
            if (shown[0][i] != shown[1][i]) {
                differ++;
                if (differ <= 5)
                    printf("prepare: page %d differs (%s turn)\n", seq[i], seqDir[i] > 0 ? "forward" : (seqDir[i] < 0 ? "back" : "no"));
            }
        printf("prepare %s: %d page turns, %d images ready at the turn (%d prepared ahead, %d kept from before), %d refused, %d images differ\n",
               name, turns, ready, prepared, ready - prepared, refused, differ);
        fprintf(stderr, "%-26s size %d: page turn %5.2f ms drawn when shown, %5.2f ms prepared ahead; prepared in %5.2f ms/page; "
                        "%d turns, %d ready, %d refused, %d differ\n",
                name, benchFontSize, turns ? tTurn[0] / turns : 0, turns ? tTurn[1] / turns : 0,
                prepared ? tPrepare / prepared : 0, turns, ready, refused, differ);
        return (differ || wrongPage || repeated) ? 2 : 0;
    }
#endif
    double tDraw = 0, tCopy = 0, tPoint = 0;
    for (int i = 0; i < count; i++) {
        int p = i * step;
        view.goToPage(p);
        double a = nowMs();
        LVDocImageRef img = view.getPageImage(0);
        double b = nowMs();
        LVDrawBuf * drawbuf = img->getDrawBuf();
        drawbuf->DrawTo(&canvas, 0, 0, 0, NULL);
        double c = nowMs();
        const char * dumpDir = getenv("BENCH_DUMP");
        if (dumpDir != NULL && *dumpDir && bpp == 8) {
            char file[1024];
            snprintf(file, sizeof(file), "%s/%s-p%d.pgm", dumpDir, name, p);
            FILE * f = fopen(file, "wb");
            if (f != NULL) {
                fprintf(f, "P5\n%d %d\n255\n", canvas.GetWidth(), canvas.GetHeight());
                for (int y = 0; y < canvas.GetHeight(); y++)
                    fwrite(canvas.GetScanLine(y), 1, canvas.GetWidth(), f);
                fclose(f);
            }
        }
        // what a tap and the reading statistics ask the engine for
        ldomXPointer ptr = view.getNodeByPoint(lvPoint(dx / 2, dy / 2));
        LVRef<ldomXRange> range = view.getPageDocumentRange(p);
        int textLen = range.isNull() ? -1 : range->getRangeText().length();
        double d = nowMs();
        tDraw += b - a;
        tCopy += c - b;
        tPoint += d - c;
        printf("page %d image %08lx canvas %08lx point %s text %d\n", p, bufCrc(drawbuf), bufCrc(&canvas),
               ptr.isNull() ? "-" : UnicodeToUtf8(ptr.toString()).c_str(), textLen);
    }
    fprintf(stderr, "%-26s size %d: view %4.1f, parse %5.0f, layout %5.0f, relayout %5.0f / %5.0f ms, %4d pages; "
                    "draw %5.2f, copy %4.2f, point+text %5.2f ms/page\n",
            name, benchFontSize, tSetup, tParse, tLoad - tParse, tRelayout, tRelayoutBack, pages,
            count ? tDraw / count : 0, count ? tCopy / count : 0, count ? tPoint / count : 0);
    return 0;
}
