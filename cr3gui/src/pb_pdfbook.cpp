/*
 * A PDF with text read as a book: where the book is kept, when it is made, and the way between
 * the book and the pages as they are. See pb_pdfbook.h.
 */
#include "pb_pdfbook.h"

#include <inkview.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <utime.h>
#include <string>
#include <vector>

#include <crgui.h>
#include "cr3pocketbook.h"
#include "mainwnd.h"
#include "pb_toc.h"
#include "pb_pageview.h"
#include "pb_pdftext.h"
#include "pb_djvutext.h"
#include "pb_kosync.h"

namespace {

const char * const BOOKS_DIR = STATEPATH "/cr3/pages";
// books kept; a book of a text PDF is a megabyte or two, one with many pictures more
enum { BOOKS_KEPT = 12 };

double nowMs()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// the book in the book window, when it is one made of a PDF
lString32 openBook;     // its path
lString32 openSource;   // the file of the reader it is made of (the archive, when the PDF lies in one)
int openPages = 0;

/// The name of the kept book of the document file `file`, which the reader knows as `path`.
lString8 bookOf(const lString8 & file, const lString32 & path, bool djvu)
{
    struct stat st;
    memset(&st, 0, sizeof(st));
    stat(file.c_str(), &st);
    char key[96];
    snprintf(key, sizeof(key), "|%ld|%ld|%d", (long)st.st_size, (long)st.st_mtime,
             djvu ? 1000 + PB_DJVUTEXT_VERSION : PB_PDFTEXT_VERSION);
    const lString8 full = UnicodeToUtf8(path) + key;
    unsigned h = 2166136261u; // FNV-1a
    for (int i = 0; i < full.length(); i++)
        h = (h ^ (unsigned char)full[i]) * 16777619u;
    // the name of the file without its folder and extension, for the lists that show a file name
    lString8 name = UnicodeToUtf8(LVExtractFilenameWithoutExtension(LVExtractFilename(path)));
    lString8 safe;
    for (int i = 0; i < name.length() && safe.length() < 60; i++) {
        const unsigned char c = (unsigned char)name[i];
        // a character of several bytes is taken whole or not at all
        if (c >= 0xC0 && safe.length() > 54)
            break;
        safe << (char)((c < 0x80 && !(c == '.' || c == '-' || c == '_' || (c >= '0' && c <= '9')
                                      || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) ? '_' : c);
    }
    char tail[24];
    snprintf(tail, sizeof(tail), "-%08x.fb2", h);
    return lString8(BOOKS_DIR) + "/" + safe + tail;
}

bool readSource(const lString8 & book, lString32 & source, int & pages)
{
    FILE * f = fopen((book + ".src").c_str(), "r");
    if (f == NULL)
        return false;
    char line[2048];
    bool ok = fgets(line, sizeof(line), f) != NULL;
    if (ok) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        source = Utf8ToUnicode(lString8(line));
        ok = fscanf(f, "%d", &pages) == 1 && pages > 0 && !source.empty();
    }
    fclose(f);
    return ok;
}

/// The oldest books go when more than BOOKS_KEPT are kept.
void prune()
{
    struct Kept {
        std::string name;
        time_t time;
    };
    std::vector<Kept> books;
    DIR * dir = opendir(BOOKS_DIR);
    if (dir == NULL)
        return;
    struct dirent * entry;
    while ((entry = readdir(dir)) != NULL) {
        const size_t n = strlen(entry->d_name);
        if (n < 5 || strcmp(entry->d_name + n - 4, ".fb2") != 0)
            continue;
        struct stat st;
        Kept kept;
        kept.name = std::string(BOOKS_DIR) + "/" + entry->d_name;
        if (stat(kept.name.c_str(), &st) != 0)
            continue;
        kept.time = st.st_mtime;
        books.push_back(kept);
    }
    closedir(dir);
    while (books.size() > BOOKS_KEPT) {
        size_t oldest = 0;
        for (size_t i = 1; i < books.size(); i++)
            if (books[i].time < books[oldest].time)
                oldest = i;
        unlink(books[oldest].name.c_str());
        unlink((books[oldest].name + ".src").c_str());
        books.erase(books.begin() + oldest);
    }
}

struct Progress {
    double started, shown;
};

bool progress(void * context, int page, int pages)
{
    // a bar on the screen once the work has taken a second, then every second and a half: an
    // update of the screen takes a third of a second itself
    Progress * p = (Progress *)context;
    const double now = nowMs();
    if (now - p->started >= 1000 && now - p->shown >= 1500 && pages > 0) {
        p->shown = now;
        main_win->showProgress(lString32("cr3_wait_icon.png"), page * 100 / pages);
    }
    return true;
}

/// The kept book of the file, made now when it is not there. "" when it cannot be made.
lString8 ensureBook(const lString32 & path, const lString8 & file)
{
    mkdir(BOOKS_DIR, 0755);
    const bool djvu = PbPageDoc::kindOfFile(file.c_str()) == PB_PAGEDOC_DJVU;
    const lString8 book = bookOf(file, path, djvu);
    struct stat st;
    lString32 source;
    int pages = 0;
    if (stat(book.c_str(), &st) == 0 && st.st_size > 0 && readSource(book, source, pages)) {
        // read again: it is not the oldest now
        utime(book.c_str(), NULL);
        return book;
    }
    Progress p;
    p.started = nowMs();
    p.shown = 0;
    PbPdfTextStats stats;
    PbDjvuTextStats djvuStats;
    memset(&stats, 0, sizeof(stats));
    memset(&djvuStats, 0, sizeof(djvuStats));
    std::string error;
    const bool made = djvu ? pbDjvuToBook(file.c_str(), book.c_str(), progress, &p, &djvuStats, &error)
                           : pbPdfToBook(file.c_str(), book.c_str(), progress, &p, &stats, &error);
    if (!made) {
        CRLog::error("%s as a book: %s: %s", djvu ? "DjVu" : "PDF", file.c_str(), error.c_str());
        return lString8::empty_str;
    }
    FILE * f = fopen((book + ".src").c_str(), "w");
    if (f != NULL) {
        fprintf(f, "%s\n%d\n", UnicodeToUtf8(path).c_str(), djvu ? djvuStats.pages : stats.pages);
        fclose(f);
    }
    prune();
    if (djvu && CRLog::isInfoEnabled())
        CRLog::info("perf: DjVu as a book: %d pages (%d with text) made a book of %ld bytes in %.1f ms: text %.1f, "
                    "pictures %.1f (%d pages decoded), writing %.1f; paragraphs %d (%s), headings %d, contents %d, "
                    "tables %d, pictures %d (formulas %d), ornaments left out %d, running titles left out %d",
                    djvuStats.pages, djvuStats.textPages, djvuStats.bytes, nowMs() - p.started, djvuStats.textMs,
                    djvuStats.imageMs, djvuStats.decoded, djvuStats.writeMs, djvuStats.paragraphs,
                    djvuStats.zones ? "of the file" : "by indents and gaps", djvuStats.headings, djvuStats.sections,
                    djvuStats.tables, djvuStats.images, djvuStats.formulas, djvuStats.ornaments, djvuStats.running);
    else if (CRLog::isInfoEnabled())
        CRLog::info("perf: PDF as a book: %d pages made a book of %ld bytes in %.1f ms: text %.1f (pages read %.1f, "
                    "taken out by the library %.1f), pictures %.1f, writing %.1f; paragraphs %d, headings %d, "
                    "contents %d, tables %d, pictures %d (drawings %d, formulas %d), footnotes %d, running titles "
                    "left out %d",
                    stats.pages, stats.bytes, nowMs() - p.started, stats.textMs, stats.loadMs, stats.extractMs,
                    stats.imageMs, stats.writeMs,
                    stats.paragraphs, stats.headings, stats.sections, stats.tables, stats.images, stats.drawings,
                    stats.formulas, stats.footnotes, stats.running);
    return book;
}

void setOpen(const lString8 & book, const lString32 & path)
{
    openBook = Utf8ToUnicode(book);
    openSource = path;
    lString32 source;
    openPages = 0;
    readSource(book, source, openPages);
}

} // namespace

bool pbPdfBookFor(const lString32 & path, lString32 & book)
{
    if (!PbPageWindow::isPageFile(path))
        return false;
    const bool djvu = !PbPageWindow::isPdf(path);
    const int mode = PbPageWindow::modeOf(path);
    if (mode == PB_PAGE_MODE_PAGES)
        return false;
    if (mode != PB_PAGE_MODE_BOOK
            && main_win->getProps()->getIntDef(PROP_POCKETBOOK_PAGES_TEXT, 1) == 0)
        return false;
    const lString8 file = PbPageWindow::documentFile(path);
    if (file.empty())
        return false;
    if (mode != PB_PAGE_MODE_BOOK) {
        const double t0 = nowMs();
        // 0: not readable, 1: pictures, 2: a text (the two lists of kinds go the same way)
        const int kind = djvu ? (int)pbDjvuKind(file.c_str()) : (int)pbPdfKind(file.c_str());
        if (CRLog::isInfoEnabled())
            CRLog::info("perf: %s as a book: the file is %s, found in %.1f ms", djvu ? "DjVu" : "PDF",
                        kind == 2 ? "a text" : kind == 1 ? "pictures" : "not readable", nowMs() - t0);
        if (kind != 2)
            return false;
    }
    const lString8 made = ensureBook(path, file);
    if (made.empty())
        return false;
    setOpen(made, path);
    book = Utf8ToUnicode(made);
    return true;
}

bool pbPdfBookFromPages(CRGUIWindowManager * wm, const lString32 & path, int page)
{
    const lString8 file = PbPageWindow::documentFile(path);
    const lString8 made = file.empty() ? lString8::empty_str : ensureBook(path, file);
    if (made.empty() || !main_win->loadDocument(Utf8ToUnicode(made)))
        return false;
    PbPageWindow::setMode(path, PB_PAGE_MODE_BOOK);
    setOpen(made, path);
    pbPdfBookGoPage(page);
    wm->update(true);
    return true;
}

bool pbPdfBookGoPage(int page)
{
    if (!pbPdfBookIsOpen())
        return false;
    // the first element of the page carries the id "pgN"; a page with nothing on it has none,
    // and the next one that has is taken
    LVDocView * view = main_win->getDocView();
    ldomDocument * doc = view->getDocument();
    for (int n = page + 1; doc != NULL && n <= openPages; n++) {
        const lString32 id = lString32("pg") + lString32::itoa(n);
        if (doc->getElementById(id.c_str()) != NULL) {
            view->goLink(lString32("#") + id, false);
            return true;
        }
    }
    return false;
}

bool pbPdfBookPlace(lString32 & source, int & page, int & pages)
{
    if (!pbPdfBookIsOpen())
        return false;
    LVDocView * view = main_win->getDocView();
    ldomDocument * doc = view->getDocument();
    // the last page of the file whose first element stands at or above the top of the screen
    const int top = view->GetPos();
    page = 0;
    for (int n = 1; doc != NULL && n <= openPages; n++) {
        ldomNode * node = doc->getElementById((lString32("pg") + lString32::itoa(n)).c_str());
        if (node == NULL)
            continue;
        lvRect rc;
        node->getAbsRect(rc);
        if (rc.top > top + 2)
            break;
        page = n - 1;
    }
    source = openSource;
    pages = openPages;
    return pages > 0;
}

bool pbPdfBookIsMade(const lString32 & file)
{
    const lString32 dir = lString32(BOOKS_DIR) + "/";
    return file.startsWith(dir);
}

bool pbPdfBookIsOpen()
{
    if (openBook.empty() || main_win == NULL)
        return false;
    // another book may have been loaded into the window since
    const lString32 name = main_win->getDocView()->getDocProps()->getStringDef(DOC_PROP_FILE_NAME, "");
    return !name.empty() && LVExtractFilename(openBook) == name;
}

bool pbPdfBookToPages(CRGUIWindowManager * wm)
{
    if (!pbPdfBookIsOpen())
        return false;
    lString32 source;
    int page = 0, pages = 0;
    pbPdfBookPlace(source, page, pages);
    main_win->getDocView()->savePosition();
    PbPageWindow::setMode(openSource, PB_PAGE_MODE_PAGES);
    // the same file in another view: no place sent or asked for the change
    PbKosync::instance().hold(true);
    const bool opened = PbPageWindow::open(wm, openSource);
    PbKosync::instance().hold(false);
    if (!opened)
        return false;
    PbPageWindow::instance->showPage(page);
    wm->update(true);
    return true;
}
