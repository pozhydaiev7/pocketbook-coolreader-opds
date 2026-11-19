/*
 * The picture mode: a PDF or DjVu file shown page by page as it is. See pb_pageview.h.
 *
 * Speed: a frame is drawn by the library into a gray buffer of the size of the window and copied
 * to the canvas; while it is read, the next frame in the direction of the last page turn is drawn
 * into a second buffer, so a page turn only swaps the two. The list screens are the touch lists
 * of the OPDS catalog (opds/opdsmenu.h).
 */
#include "pb_pageview.h"
#include <inkview.h>
#include <crengine.h>
#include <cri18n.h>
#include <algorithm>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include "cr3pocketbook.h"
#include "mainwnd.h"
#include "settings.h"
#include "pb_toc.h"
#include "pb_toclist.h"
#include "pb_statsview.h"
#include "opdsmenu.h"
#include "pb_pdfbook.h"
#include "viewdlg.h"

extern V3DocViewWin * main_win;
// mainwnd.cpp: a row of the table of "Book info"
void addPropLine( lString8 & buf, const char * description, const lString32 & value );
using namespace opdsui;

PbPageWindow * PbPageWindow::instance = NULL;

namespace {

const float PI_F = 3.14159265358979f;
// where the state of the files and the documents taken out of archives are kept
const char * const PAGES_DIR = STATEPATH "/cr3/pages";

lString32 tr(const char * s)
{
    return lString32(_(s));
}

double nowMs()
{
    // gettimeofday: clock_gettime is in librt in the C library of the SDK, which the program does not link
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// A page is rendered in 8 bits of gray; the canvas of the screen holds 2, 3, 4 or 8 (main() in
// cr3pocketbook.cpp), and LVGrayDrawBuf::DrawTo() copies nothing between two different depths.
// 3 and 4 bits are a byte per pixel with the low bits clear, as the engine keeps them: the
// picture is brought to that here, four pixels at a step. 2 bits are packed when drawn.
void toCanvasDepth(lUInt8 * p, size_t n, int bpp)
{
    const lUInt32 keep = bpp == 4 ? 0xF0F0F0F0u : bpp == 3 ? 0xE0E0E0E0u : 0xFFFFFFFFu;
#if (GRAY_INVERSE == 1)
    const lUInt32 flip = 0xFFFFFFFFu;
#else
    const lUInt32 flip = 0;
    if (keep == 0xFFFFFFFFu)
        return;
#endif
    size_t i = 0;
    for (; i < n && ((size_t)(p + i) & 3) != 0; i++)
        p[i] = (lUInt8)((p[i] ^ flip) & keep);
    lUInt32 * w = (lUInt32 *)(p + i);
    for (size_t k = (n - i) >> 2; k != 0; k--, w++)
        *w = (*w ^ flip) & keep;
    for (i += (n - i) & ~(size_t)3; i < n; i++)
        p[i] = (lUInt8)((p[i] ^ flip) & keep);
}

unsigned hashOf(const lString8 & text)
{
    unsigned h = 2166136261u; // FNV-1a
    for (int i = 0; i < text.length(); i++)
        h = (h ^ (unsigned char)text[i]) * 16777619u;
    return h;
}

bool hasExtension(const lString32 & name, const char * ext)
{
    lString32 lower = name;
    lower.lowercase();
    return lower.endsWith(lString32(ext));
}

bool isDocumentName(const lString32 & name)
{
    return hasExtension(name, ".pdf") || hasExtension(name, ".djvu") || hasExtension(name, ".djv");
}

/// The document of an archive: its first PDF or DjVu entry, "" when there is none.
lString32 documentOfArchive(const lString32 & path, LVContainerRef & archive, lvsize_t & size)
{
    LVStreamRef stream = LVOpenFileStream(path.c_str(), LVOM_READ);
    if (stream.isNull())
        return lString32::empty_str;
    archive = LVOpenArchieve(stream);
    if (archive.isNull())
        return lString32::empty_str;
    for (int i = 0; i < archive->GetObjectCount(); i++) {
        const LVContainerItemInfo * item = archive->GetObjectInfo(i);
        if (item != NULL && !item->IsContainer() && isDocumentName(lString32(item->GetName()))) {
            size = item->GetSize();
            return lString32(item->GetName());
        }
    }
    return lString32::empty_str;
}

/// Takes the document out of the archive into a kept file and returns its path ("" on a failure).
/// The file is named by the archive, so the next opening finds it; the documents of other
/// archives are removed: one is kept.
lString8 extractFromArchive(const lString32 & path)
{
    LVContainerRef archive;
    lvsize_t size = 0;
    lString32 name = documentOfArchive(path, archive, size);
    if (name.empty())
        return lString8::empty_str;
    struct stat st;
    memset(&st, 0, sizeof(st));
    stat(UnicodeToUtf8(path).c_str(), &st);
    char key[64];
    snprintf(key, sizeof(key), "arc-%08x-%lx%s", hashOf(UnicodeToUtf8(path + name)), (long)st.st_mtime,
             hasExtension(name, ".pdf") ? ".pdf" : ".djvu");
    lString8 target = lString8(PAGES_DIR) + "/" + key;
    if (stat(target.c_str(), &st) == 0 && (lvsize_t)st.st_size == size)
        return target;
    DIR * dir = opendir(PAGES_DIR);
    if (dir != NULL) {
        struct dirent * entry;
        while ((entry = readdir(dir)) != NULL)
            if (strncmp(entry->d_name, "arc-", 4) == 0)
                unlink((lString8(PAGES_DIR) + "/" + entry->d_name).c_str());
        closedir(dir);
    }
    LVStreamRef in = archive->OpenStream(name.c_str(), LVOM_READ);
    FILE * out = in.isNull() ? NULL : fopen(target.c_str(), "wb");
    if (out == NULL)
        return lString8::empty_str;
    static char block[1 << 16];
    lvsize_t done = 0, got = 0;
    bool ok = true;
    while (done < size && in->Read(block, sizeof(block), &got) == LVERR_OK && got > 0) {
        if (fwrite(block, 1, got, out) != got) {
            ok = false;
            break;
        }
        done += got;
    }
    fclose(out);
    if (!ok || done != size) {
        unlink(target.c_str());
        return lString8::empty_str;
    }
    return target;
}

void pausePassed()
{
    if (PbPageWindow::instance != NULL)
        PbPageWindow::instance->prepareAhead();
}

void pageChosen(int page)
{
    // the page selector of the firmware gives a page from 1
    if (PbPageWindow::instance != NULL && page > 0)
        PbPageWindow::instance->getWindowManager()->postCommand(PB_PAGEVIEW_CMD_GOTO_APPLY, page);
}

/// The search and the citations work on the text of a book. A file with text is read as one
/// ("View as a book"); the pages shown as they are, are pictures.
void notInOriginal(int command)
{
    (void)command;
    lString8 text = UnicodeToUtf8(lString32(_("Not in the view of the original. A file with text has it when viewed as a book.")));
    Message(ICON_INFORMATION, const_cast<char *>("CoolReader"), const_cast<char *>(text.c_str()), 3000);
}

const char * scaleName(int scale)
{
    switch (scale) {
    case PB_PAGE_SCALE_PAGE: return _("Whole page");
    case PB_PAGE_SCALE_WIDTH: return _("Page width");
    case PB_PAGE_SCALE_CONTENT: return _("Whole content");
    case PB_PAGE_SCALE_CONTENT_WIDTH: return _("Content width");
    case PB_PAGE_SCALE_AUTO: return _("Automatic, every page levelled");
    default: return _("Own frame");
    }
}

} // namespace

/// The state file of a file of the reader.
static lString8 stateFileOf(const lString32 & path)
{
    char name[48];
    snprintf(name, sizeof(name), "/%08x.state", hashOf(UnicodeToUtf8(path)));
    return lString8(PAGES_DIR) + name;
}

// ------------------------------------------------------------------------------------ opening
bool PbPageWindow::isPdf(const lString32 & path)
{
    if (hasExtension(path, ".pdf"))
        return true;
    if (!hasExtension(path, ".zip"))
        return false;
    LVContainerRef archive;
    lvsize_t size = 0;
    return hasExtension(documentOfArchive(path, archive, size), ".pdf");
}

lString8 PbPageWindow::documentFile(const lString32 & path)
{
    mkdir(PAGES_DIR, 0755);
    return isDocumentName(path) ? UnicodeToUtf8(path) : extractFromArchive(path);
}

int PbPageWindow::modeOf(const lString32 & path)
{
    FILE * f = fopen(stateFileOf(path).c_str(), "r");
    if (f == NULL)
        return PB_PAGE_MODE_AUTO;
    int mode = PB_PAGE_MODE_AUTO;
    char line[1024];
    while (fgets(line, sizeof(line), f) != NULL)
        if (strncmp(line, "mode ", 5) == 0)
            mode = atoi(line + 5);
    fclose(f);
    return mode == PB_PAGE_MODE_PAGES || mode == PB_PAGE_MODE_BOOK ? mode : PB_PAGE_MODE_AUTO;
}

void PbPageWindow::setMode(const lString32 & path, int mode)
{
    // the other lines of the state stay as they are
    const lString8 name = stateFileOf(path);
    std::vector<std::string> lines;
    FILE * f = fopen(name.c_str(), "r");
    if (f != NULL) {
        char line[1024];
        while (fgets(line, sizeof(line), f) != NULL)
            if (strncmp(line, "mode ", 5) != 0)
                lines.push_back(line);
        fclose(f);
    }
    mkdir(PAGES_DIR, 0755);
    const lString8 temp = name + ".tmp";
    f = fopen(temp.c_str(), "w");
    if (f == NULL)
        return;
    fprintf(f, "mode %d\n", mode);
    for (size_t i = 0; i < lines.size(); i++)
        fputs(lines[i].c_str(), f);
    if (lines.empty())
        fprintf(f, "file %s\n", UnicodeToUtf8(path).c_str());
    fclose(f);
    rename(temp.c_str(), name.c_str());
    if (instance != NULL && instance->_path == path)
        instance->_mode = mode;
}

void PbPageWindow::showPage(int page)
{
    goToPage(page);
    saveState();
}

bool PbPageWindow::isPageFile(const lString32 & path)
{
    if (isDocumentName(path))
        return true;
    if (!hasExtension(path, ".zip"))
        return false;
    LVContainerRef archive;
    lvsize_t size = 0;
    return !documentOfArchive(path, archive, size).empty();
}

bool PbPageWindow::open(CRGUIWindowManager * wm, const lString32 & path)
{
    double t0 = nowMs();
    mkdir(PAGES_DIR, 0755);
    lString8 file = UnicodeToUtf8(path);
    if (!isDocumentName(path)) {
        file = extractFromArchive(path);
        if (file.empty()) {
            CRLog::error("picture mode: no document taken out of %s", UnicodeToUtf8(path).c_str());
            return false;
        }
    }
    double t1 = nowMs();
    std::string error;
    PbPageDoc * doc = PbPageDoc::open(file.c_str(), &error);
    if (doc == NULL) {
        CRLog::error("picture mode: %s: %s", file.c_str(), error.c_str());
        return false;
    }
    PbPageWindow * window = new PbPageWindow(wm, doc, path, stateFileOf(path));
    instance = window;
    wm->activateWindow(window);
    CRLog::info("perf: picture mode: %s, %d pages; archive %.1f ms, opened in %.1f ms; page %d, scale %d, leafing %d",
                doc->kind() == PB_PAGEDOC_PDF ? "PDF" : "DjVu", doc->pageCount(), t1 - t0, nowMs() - t1,
                window->_page + 1, window->_scale, window->_leafing);
    return true;
}

PbPageWindow::PbPageWindow(CRGUIWindowManager * wm, PbPageDoc * doc, const lString32 & path, const lString8 & stateFile)
    : CRGUIWindowBase(wm), _doc(doc), _path(path), _stateFile(stateFile), _page(0), _frame(0),
      _scale(PB_PAGE_SCALE_PAGE), _leafing(PB_PAGE_LEAF_PAGE), _showNumber(true), _mode(PB_PAGE_MODE_AUTO),
      _direction(1), _stamp(1),
      _adjust(false), _adjAngle(0), _touchStarted(false), _head(NULL), _tocRead(false)
{
    _angle[0] = _angle[1] = 0;
    _fullscreen = true;
    setAccelerators(main_win->getAccelerators());
    readDefaults();
    loadState();
}

PbPageWindow::~PbPageWindow()
{
    ClearTimer(pausePassed);
    if (instance == this)
        instance = NULL;
    delete _head;
    delete _doc;
}

void PbPageWindow::closing()
{
    saveState();
}

/// The settings of the group "PDF and DjVu": what a file opened for the first time starts with,
/// and whether the page number is shown.
void PbPageWindow::readDefaults()
{
    CRPropRef props = main_win->getProps();
    // the default is every page levelled by itself (the owner's word): a scan is crooked more often than not
    _scale = props->getIntDef(PROP_POCKETBOOK_PAGES_SCALE, PB_PAGE_SCALE_AUTO);
    _leafing = props->getIntDef(PROP_POCKETBOOK_PAGES_LEAFING, PB_PAGE_LEAF_PAGE);
    _showNumber = props->getIntDef(PROP_POCKETBOOK_PAGES_PAGE_NUMBER, 1) != 0;
    // the own frame belongs to a file, it is no value of the settings
    if (_scale < 0 || _scale >= PB_PAGE_SCALE_COUNT || _scale == PB_PAGE_SCALE_OWN)
        _scale = PB_PAGE_SCALE_AUTO;
}

void PbPageWindow::loadState()
{
    FILE * f = fopen(_stateFile.c_str(), "r");
    if (f == NULL)
        return;
    char key[32];
    while (fscanf(f, "%31s", key) == 1) {
        if (strcmp(key, "page") == 0) {
            if (fscanf(f, "%d %d", &_page, &_frame) != 2) break;
        } else if (strcmp(key, "scale") == 0) {
            if (fscanf(f, "%d %d", &_scale, &_leafing) != 2) break;
        } else if (strcmp(key, "angle") == 0) {
            if (fscanf(f, "%f %f", &_angle[0], &_angle[1]) != 2) break;
        } else if (strcmp(key, "frame0") == 0 || strcmp(key, "frame1") == 0) {
            OwnFrame & own = _own[key[5] - '0'];
            int set = 0;
            if (fscanf(f, "%f %f %f %d", &own.cx, &own.cy, &own.zoom, &set) != 4) break;
            own.set = set != 0 && own.zoom > 0;
        } else if (strcmp(key, "mode") == 0) {
            if (fscanf(f, "%d", &_mode) != 1) break;
        } else if (strcmp(key, "mark") == 0) {
            int page = 0;
            if (fscanf(f, "%d", &page) != 1) break;
            if (page >= 0 && page < _doc->pageCount())
                _marks.push_back(page);
        } else {
            // the rest of a line this version does not know
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') { }
        }
    }
    fclose(f);
    if (_page < 0 || _page >= _doc->pageCount())
        _page = 0;
    if (_scale < 0 || _scale >= PB_PAGE_SCALE_COUNT)
        _scale = PB_PAGE_SCALE_AUTO;
    if (_leafing != PB_PAGE_LEAF_FRAMES && _leafing != PB_PAGE_LEAF_RIBBON)
        _leafing = PB_PAGE_LEAF_PAGE;
    if (_frame < 0)
        _frame = 0;
}

void PbPageWindow::saveState()
{
    lString8 temp = _stateFile + ".tmp";
    FILE * f = fopen(temp.c_str(), "w");
    if (f == NULL)
        return;
    fprintf(f, "mode %d\npage %d %d\nscale %d %d\nangle %.2f %.2f\n", _mode, _page, _frame, _scale, _leafing,
            _angle[0], _angle[1]);
    for (int i = 0; i < 2; i++)
        fprintf(f, "frame%d %.2f %.2f %.5f %d\n", i, _own[i].cx, _own[i].cy, _own[i].zoom, _own[i].set ? 1 : 0);
    for (size_t i = 0; i < _marks.size(); i++)
        fprintf(f, "mark %d\n", _marks[i]);
    fprintf(f, "file %s\n", UnicodeToUtf8(_path).c_str());
    fclose(f);
    rename(temp.c_str(), _stateFile.c_str());
}

void PbPageWindow::changed()
{
    _stamp++;
    _shown.ready = false;
    _ahead.ready = false;
    setDirty();
}

void PbPageWindow::setRect(const lvRect & rc)
{
    CRGUIWindowBase::setRect(rc);
    // the page part is the window without the status line; a new size of it starts the frames anew
    setupHead();
}

void PbPageWindow::reactivated()
{
    // back from a list or from the settings menu: the page number may be turned on or off
    CRPropRef props = main_win->getProps();
    _showNumber = props->getIntDef(PROP_POCKETBOOK_PAGES_PAGE_NUMBER, 1) != 0;
    // and the status line may be another one
    setupHead();
    setDirty();
}

// ------------------------------------------------------------------------------------ frames
void PbPageWindow::framesOfPage(int page, std::vector<PbPageView> & frames)
{
    const int par = page & 1;
    _doc->framesOf(page, _scale, _leafing, _angle[par], _own[par], _pageRc.width(), _pageRc.height(), frames);
}

void PbPageWindow::drawShot(Shot & shot, int page, int frame)
{
    const int W = _pageRc.width(), H = _pageRc.height();
    std::vector<PbPageView> frames;
    // the parts of the time, for the log of the callers
    _doc->readMs = 0;
    _doc->boxMs = 0;
    _doc->levelMs = 0;
    if (_leafing == PB_PAGE_LEAF_RIBBON) {
        drawRibbon(shot, page, frame);
        return;
    }
    framesOfPage(page, frames);
    if (frame >= (int)frames.size())
        frame = (int)frames.size() - 1;
    if (frame < 0)
        frame = 0;
    shot.pixels.resize((size_t)W * H);
    _doc->render(page, frames[frame], &shot.pixels[0], W, H, W);
    toCanvasDepth(&shot.pixels[0], shot.pixels.size(), _wm->getScreen()->getCanvas()->GetBitsPerPixel());
    shot.page = page;
    shot.frame = frame;
    shot.stamp = _stamp;
    shot.angle = frames[frame].angle;
    shot.ready = true;
}

// ------------------------------------------------------------------------------------ the ribbon
// Asked by a tester: "a continuous mode of leafing; a small gap between the pages is enough". The
// pages stand one under another as wide as the screen. The screen of a reader does not scroll:
// a page turn moves the ribbon by a screen at once.
static const int RIBBON_OVERLAP = 20;   // a screen repeats this part of the one before, 1/N of its height
static const int RIBBON_GAP = 60;       // the gap between two pages, 1/N of the height of the page part

bool PbPageWindow::stripOfPage(int page, PbPageStrip & strip)
{
    const int par = page & 1;
    return _doc->stripOf(page, _scale, _angle[par], _own[par], _pageRc.width(), _pageRc.height(), strip);
}

int PbPageWindow::ribbonGap() const
{
    const int gap = _pageRc.height() / RIBBON_GAP;
    return gap > 6 ? gap : 6;
}

bool PbPageWindow::ribbonMove(int & page, int & row, int delta)
{
    const int count = _doc->pageCount(), gap = ribbonGap();
    const int page0 = page, row0 = row;
    PbPageStrip strip;
    if (delta > 0) {
        for (;;) {
            stripOfPage(page, strip);
            const int rest = strip.height - row;
            if (delta < rest) {
                row += delta;
                break;
            }
            if (page + 1 >= count) {
                // the step is shorter than a screen: the end of the file is on the screen already
                page = page0;
                row = row0;
                return false;
            }
            // over the rest of the page and the gap: the next page from its top, or further
            delta -= rest + gap;
            page++;
            row = 0;
            if (delta <= 0)
                break;
        }
    } else {
        delta = -delta;
        for (;;) {
            if (delta <= row) {
                row -= delta;
                break;
            }
            if (page == 0) {
                row = 0;
                break;
            }
            delta -= row + gap;
            page--;
            stripOfPage(page, strip);
            row = strip.height;
            if (delta <= 0) {
                // the step ends in the gap: the page under it from its top
                page++;
                row = 0;
                break;
            }
        }
    }
    return page != page0 || row != row0;
}

void PbPageWindow::drawRibbon(Shot & shot, int page, int row)
{
    const int W = _pageRc.width(), H = _pageRc.height();
    const int count = _doc->pageCount(), gap = ribbonGap();
    shot.pixels.resize((size_t)W * H);
    memset(&shot.pixels[0], 0xFF, shot.pixels.size());
    shot.angle = 0;
    int y = 0, first = row;
    for (int p = page; y < H && p < count; p++) {
        PbPageStrip strip;
        stripOfPage(p, strip);
        int from = p == page ? row : 0;
        if (from >= strip.height)
            // a row kept for another size of the page part
            from = strip.height - 1;
        if (from < 0)
            from = 0;
        if (p == page) {
            first = from;
            shot.angle = strip.angle;
        }
        int rows = strip.height - from;
        if (rows > H - y)
            rows = H - y;
        _doc->renderStrip(p, strip, from, from + rows, &shot.pixels[(size_t)y * W], W, W);
        y += rows;
        // the gap: white, with a thin gray line in its middle that tells two pages apart
        if (p + 1 < count && y + gap / 2 < H)
            memset(&shot.pixels[(size_t)(y + gap / 2) * W], 0x80, W);
        y += gap;
    }
    toCanvasDepth(&shot.pixels[0], shot.pixels.size(), _wm->getScreen()->getCanvas()->GetBitsPerPixel());
    shot.page = page;
    shot.frame = first;
    shot.stamp = _stamp;
    shot.ready = true;
}

// ------------------------------------------------------------------------------------ frames
void PbPageWindow::target(int direction, int & page, int & frame)
{
    page = _page;
    frame = _frame;
    if (_leafing == PB_PAGE_LEAF_RIBBON) {
        // by a screen, less a twentieth of it: a line cut by the edge is whole on the next screen
        const int H = _pageRc.height();
        const int step = H - H / RIBBON_OVERLAP;
        if (!ribbonMove(page, frame, direction > 0 ? step : -step))
            page = -1;
        return;
    }
    if (_leafing == PB_PAGE_LEAF_FRAMES) {
        std::vector<PbPageView> frames;
        framesOfPage(_page, frames);
        if (direction > 0 && _frame + 1 < (int)frames.size()) {
            frame = _frame + 1;
            return;
        }
        if (direction < 0 && _frame > 0) {
            frame = _frame - 1;
            return;
        }
        page += direction;
        frame = 0;
        if (direction < 0 && page >= 0) {
            framesOfPage(page, frames);
            frame = (int)frames.size() - 1;
        }
    } else {
        page += direction;
        frame = 0;
    }
    if (page < 0 || page >= _doc->pageCount())
        page = -1;
}

void PbPageWindow::turn(int direction)
{
    int page, frame;
    target(direction, page, frame);
    if (page < 0)
        return;
    _page = page;
    _frame = frame;
    _direction = direction;
    setDirty();
}

void PbPageWindow::goToPage(int page)
{
    if (page < 0 || page >= _doc->pageCount())
        return;
    _direction = page >= _page ? 1 : -1;
    _page = page;
    _frame = 0;
    setDirty();
}

void PbPageWindow::armPause()
{
    if (main_win->getProps()->getIntDef(PROP_POCKETBOOK_PAGE_PREPARE, 1) != 0)
        SetWeakTimer("PbPagePrepare", pausePassed, 400);
}

void PbPageWindow::prepareAhead()
{
    if (_adjust || !_shown.ready || _wm->getTopVisibleWindow() != this)
        return;
    int page, frame;
    target(_direction, page, frame);
    if (page < 0 || (_ahead.ready && _ahead.page == page && _ahead.frame == frame && _ahead.stamp == _stamp))
        return;
    double t0 = nowMs();
    drawShot(_ahead, page, frame);
    // the page after the prepared one is read beside, while this one is on the screen
    _doc->hintNext(page + _direction);
    if (CRLog::isInfoEnabled())
        CRLog::info("perf: picture mode: page %d frame %d prepared ahead in %.1f ms: page read %.1f, content box %.1f, "
                    "levelling %.1f (turn %+.2f)", page + 1, frame, nowMs() - t0, _doc->readMs, _doc->boxMs,
                    _doc->levelMs, _ahead.angle);
}

// ------------------------------------------------------------------------------------ drawing
void PbPageWindow::draw()
{
    const int W = _pageRc.width(), H = _pageRc.height();
    if (W < 1 || H < 1)
        return;
    double t0 = nowMs();
    const char * how = "kept";
    bool drawnNow = false;
    if (_adjust) {
        // the frame being set: drawn anew at every step
        PbPageView view;
        view.cx = _adjFrame.cx;
        view.cy = _adjFrame.cy;
        view.zoom = _adjFrame.zoom;
        view.angle = _adjAngle;
        _shown.pixels.resize((size_t)W * H);
        _doc->render(_page, view, &_shown.pixels[0], W, H, W);
        toCanvasDepth(&_shown.pixels[0], _shown.pixels.size(), _wm->getScreen()->getCanvas()->GetBitsPerPixel());
        _shown.ready = false;
        how = "frame being set";
    } else if (!(_shown.ready && _shown.page == _page && _shown.frame == _frame && _shown.stamp == _stamp)) {
        if (_ahead.ready && _ahead.page == _page && _ahead.frame == _frame && _ahead.stamp == _stamp) {
            // prepared while the frame before was read: the two buffers change places
            _shown.pixels.swap(_ahead.pixels);
            _shown.page = _page;
            _shown.frame = _frame;
            _shown.stamp = _stamp;
            _shown.ready = true;
            _ahead.ready = false;
            how = "prepared";
        } else {
            drawShot(_shown, _page, _frame);
            _frame = _shown.frame;
            drawnNow = true;
            // the next page is read beside while the screen shows this one
            _doc->hintNext(_page + _direction);
        }
    }
    double t1 = nowMs();
    LVDrawBuf * canvas = _wm->getScreen()->getCanvas().get();
    const int bpp = canvas->GetBitsPerPixel();
    if (bpp == 2) {
        // four pixels to a byte, the first one in the high bits
        const int row = (W + 3) >> 2;
        std::vector<lUInt8> packed((size_t)row * H, 0);
        const lUInt8 * src = &_shown.pixels[0];
        for (int y = 0; y < H; y++) {
            lUInt8 * dst = &packed[(size_t)y * row];
            for (int x = 0; x < W; x++)
                dst[x >> 2] |= (lUInt8)((*src++ >> 6) << (6 - ((x & 3) << 1)));
        }
        LVGrayDrawBuf image(W, H, 2, &packed[0]);
        image.DrawTo(canvas, _pageRc.left, _pageRc.top, 0, NULL);
    } else {
        // the pixels are in the depth of the canvas already (toCanvasDepth)
        LVGrayDrawBuf image(W, H, bpp, &_shown.pixels[0]);
        image.DrawTo(canvas, _pageRc.left, _pageRc.top, 0, NULL);
    }
    // the status line: drawn when the page is shown, so its clock is the one of this moment;
    // the frame prepared ahead holds the page alone
    drawHead(canvas);
    if (_adjust)
        drawAdjustPanel(canvas);
    else if (_showNumber)
        drawNumber(canvas);
    if (CRLog::isInfoEnabled()) {
        if (drawnNow)
            CRLog::info("perf: picture mode: page %d frame %d drawn now in %.1f ms: page read %.1f, content box %.1f, "
                        "levelling %.1f (turn %+.2f); to the canvas %.1f ms", _page + 1, _frame, t1 - t0, _doc->readMs,
                        _doc->boxMs, _doc->levelMs, _shown.angle, nowMs() - t1);
        else
            CRLog::info("perf: picture mode: page %d frame %d %s in %.1f ms, to the canvas %.1f ms", _page + 1, _frame,
                        how, t1 - t0, nowMs() - t1);
    }
    if (!_adjust) {
        // the reading statistics time the page as they time a page of a book
        PbStatsView::pageOfFileShown(_path, _page, _doc->pageCount());
        armPause();
    }
}

static LVFontRef pageWindowFont(int size)
{
    lString8 face = UnicodeToUtf8(main_win->getProps()->getStringDef(PROP_FONT_FACE, "Liberation Sans"));
    return fontMan->GetFont(size, 400, false, css_ff_sans_serif, face);
}

void PbPageWindow::drawNumber(LVDrawBuf * canvas)
{
    // "12 / 161" in the lower right corner, small, on a white patch
    const int size = _pageRc.width() / 45 > 14 ? _pageRc.width() / 45 : 14;
    LVFontRef font = pageWindowFont(size);
    if (font.isNull())
        return;
    lString32 text = lString32::itoa(_page + 1) + U" / " + lString32::itoa(_doc->pageCount());
    const int tw = font->getTextWidth(text.c_str(), text.length());
    const int th = font->getHeight();
    const int pad = size / 3;
    const int x1 = _pageRc.right - pad, y1 = _pageRc.bottom - pad;
    const int x0 = x1 - tw - pad * 2, y0 = y1 - th;
    canvas->FillRect(x0, y0, x1, y1, 0xFFFFFF);
    canvas->SetTextColor(0x000000);
    font->DrawTextString(canvas, x0 + pad, y0, text.c_str(), text.length(), '?', NULL, false);
}

// ------------------------------------------------------------------------------------ the frame being set
// The buttons of the panel, row by row. The first row shifts the frame: reported from InkPad 4,
// "there is no way to move the frame"; a drag moved it, and nothing on the screen told of that.
enum {
    ADJ_LEFT = 0, ADJ_UP, ADJ_DOWN, ADJ_RIGHT,
    ADJ_ZOOM_OUT, ADJ_ZOOM_IN, ADJ_TURN_BACK, ADJ_TURN,
    ADJ_QUARTER, ADJ_RESET, ADJ_CANCEL, ADJ_DONE,
    ADJ_COUNT
};

static const char * const ADJUST_LABELS[ADJ_COUNT] = {
    NULL, NULL, NULL, NULL,          // arrows, drawn
    "\xE2\x88\x92", "+", "\xE2\x88\x92" "0.5\xC2\xB0", "+0.5\xC2\xB0",
    "90\xC2\xB0", NULL, NULL, NULL   // the last three are words: Reset, Cancel, Done
};

/// The frame goes over the page by (sx, sy) pixels of the screen; the picture is turned by angle.
static void shiftFrame(PbPageOwnFrame & frame, float angle, float sx, float sy)
{
    const float a = angle * PI_F / 180.0f;
    const float c = cosf(a), s = sinf(a);
    frame.cx += (sx * c + sy * s) / frame.zoom;
    frame.cy += (-sx * s + sy * c) / frame.zoom;
}

/// The arrow of a shift button: a filled triangle in the middle of the button. Drawn, so that it
/// does not depend on the font having the arrow signs.
static void drawArrow(LVDrawBuf * canvas, const lvRect & rc, int button)
{
    const int half = rc.height() / 5 > 4 ? rc.height() / 5 : 4;   // half of the base
    const int len = half * 3 / 2;                                  // from the base to the point
    const int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
    for (int d = -half; d <= half; d++) {
        const int l = len * (half + 1 - (d < 0 ? -d : d)) / (half + 1);
        if (l <= 0)
            continue;
        switch (button) {
        case ADJ_LEFT:  canvas->FillRect(cx + len / 2 - l, cy + d, cx + len / 2, cy + d + 1, 0x000000); break;
        case ADJ_RIGHT: canvas->FillRect(cx - len / 2, cy + d, cx - len / 2 + l, cy + d + 1, 0x000000); break;
        case ADJ_UP:    canvas->FillRect(cx + d, cy + len / 2 - l, cx + d + 1, cy + len / 2, 0x000000); break;
        default:        canvas->FillRect(cx + d, cy - len / 2, cx + d + 1, cy - len / 2 + l, 0x000000); break;
        }
    }
}

void PbPageWindow::adjustButtons(std::vector<lvRect> & rects)
{
    // three rows of four buttons at the bottom of the window
    rects.clear();
    const int W = _rect.width();
    const int bh = _rect.height() / 14;
    const int top = _rect.bottom - bh * (ADJ_COUNT / 4);
    for (int i = 0; i < ADJ_COUNT; i++) {
        lvRect rc;
        rc.left = _rect.left + W * (i % 4) / 4;
        rc.right = _rect.left + W * (i % 4 + 1) / 4;
        rc.top = top + bh * (i / 4);
        rc.bottom = rc.top + bh;
        rects.push_back(rc);
    }
}

void PbPageWindow::drawAdjustPanel(LVDrawBuf * canvas)
{
    std::vector<lvRect> rects;
    adjustButtons(rects);
    LVFontRef font = pageWindowFont(rects[0].height() * 2 / 5);
    if (font.isNull())
        return;
    canvas->SetTextColor(0x000000);
    for (int i = 0; i < ADJ_COUNT; i++) {
        const lvRect & rc = rects[i];
        canvas->FillRect(rc.left, rc.top, rc.right, rc.bottom, 0x000000);
        canvas->FillRect(rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2, 0xFFFFFF);
        if (i <= ADJ_RIGHT) {
            drawArrow(canvas, rc, i);
            continue;
        }
        lString32 text = ADJUST_LABELS[i] != NULL ? Utf8ToUnicode(lString8(ADJUST_LABELS[i]))
                       : i == ADJ_RESET ? tr("Reset") : i == ADJ_CANCEL ? tr("Cancel") : tr("Done");
        const int tw = font->getTextWidth(text.c_str(), text.length());
        font->DrawTextString(canvas, rc.left + (rc.width() - tw) / 2, rc.top + (rc.height() - font->getHeight()) / 2,
                             text.c_str(), text.length(), '?', NULL, false);
    }
}

void PbPageWindow::adjustBegin()
{
    // starts from what is on the screen
    std::vector<PbPageView> frames;
    framesOfPage(_page, frames);
    const PbPageView & view = frames[_frame < (int)frames.size() ? _frame : 0];
    _adjFrame.cx = view.cx;
    _adjFrame.cy = view.cy;
    _adjFrame.zoom = view.zoom;
    _adjFrame.set = true;
    _adjAngle = view.angle;
    _adjust = true;
    ClearTimer(pausePassed);
    setDirty();
}

void PbPageWindow::adjustEnd(bool keep)
{
    _adjust = false;
    if (keep) {
        const int par = _page & 1;
        _own[par] = _adjFrame;
        _angle[par] = _adjAngle;
        // the pages of the other parity start with the same frame until theirs is set
        if (!_own[par ^ 1].set) {
            _own[par ^ 1] = _adjFrame;
            _own[par ^ 1].set = false;
        }
        _scale = PB_PAGE_SCALE_OWN;
        _frame = 0;
        saveState();
    }
    changed();
}

bool PbPageWindow::adjustTouch(int x, int y, CRGUITouchEventType evType)
{
    if (evType == CRTOUCH_DOWN) {
        _touchStart = lvPoint(x, y);
        _touchStarted = true;
        return true;
    }
    if (evType != CRTOUCH_UP || !_touchStarted)
        return true;
    _touchStarted = false;
    std::vector<lvRect> rects;
    adjustButtons(rects);
    const int dx = x - _touchStart.x, dy = y - _touchStart.y;
    const int moved = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    if (moved < _rect.width() / 40) {
        for (int i = 0; i < ADJ_COUNT; i++) {
            if (!rects[i].isPointInside(lvPoint(x, y)))
                continue;
            // The frame goes over the page to the side the arrow shows, so what lies there comes
            // into view: a twentieth of the width of the window a step.
            const float step = _rect.width() / 20.0f;
            switch (i) {
            case ADJ_LEFT: shiftFrame(_adjFrame, _adjAngle, -step, 0); break;
            case ADJ_RIGHT: shiftFrame(_adjFrame, _adjAngle, step, 0); break;
            case ADJ_UP: shiftFrame(_adjFrame, _adjAngle, 0, -step); break;
            case ADJ_DOWN: shiftFrame(_adjFrame, _adjAngle, 0, step); break;
            // (a twentieth a step: a tenth was too coarse to fit a column to the screen)
            case ADJ_ZOOM_OUT: _adjFrame.zoom /= 1.05f; break;
            case ADJ_ZOOM_IN: _adjFrame.zoom *= 1.05f; break;
            case ADJ_TURN_BACK: _adjAngle -= 0.5f; break;
            case ADJ_TURN: _adjAngle += 0.5f; break;
            case ADJ_QUARTER: _adjAngle += 90.0f; break;
            case ADJ_RESET: {
                // the whole content, upright
                PbPageSize size;
                PbPageBox box;
                if (_doc->pageSize(_page, size)) {
                    box = PbPageBox(0, 0, size.width, size.height);
                    _doc->contentBox(_page, box);
                    PbPageView view = PbPageDoc::viewOfBox(box, 0, _pageRc.width(), _pageRc.height());
                    _adjFrame.cx = view.cx;
                    _adjFrame.cy = view.cy;
                    _adjFrame.zoom = view.zoom;
                    _adjAngle = 0;
                }
                break;
            }
            case ADJ_CANCEL: adjustEnd(false); return redrawAfterTouch();
            default: adjustEnd(true); return redrawAfterTouch();
            }
            if (_adjAngle > 180.0f)
                _adjAngle -= 360.0f;
            if (_adjAngle <= -180.0f)
                _adjAngle += 360.0f;
            return redrawAfterTouch();
        }
        return true;
    }
    // a drag moves the page under the finger: the frame goes the other way
    shiftFrame(_adjFrame, _adjAngle, (float)-dx, (float)-dy);
    return redrawAfterTouch();
}

bool PbPageWindow::redrawAfterTouch()
{
    // A touch posts no event by itself, and the window manager draws only after an event it has
    // handled (a command, a key): without this one the frame changed and the screen did not.
    setDirty();
    _wm->postEvent(new CRGUIUpdateEvent(false));
    return true;
}

// ------------------------------------------------------------------------------------ input
bool PbPageWindow::onTouch(int x, int y, CRGUITouchEventType evType)
{
    if (_adjust)
        return adjustTouch(x, y, evType);
    if (evType == CRTOUCH_DOWN) {
        _touchStart = lvPoint(x, y);
        _touchStarted = true;
        return true;
    }
    bool longTap = evType == CRTOUCH_DOWN_LONG;
    if ((evType != CRTOUCH_UP && !longTap) || !_touchStarted)
        return true;
    _touchStarted = false;
    CRPropRef props = main_win->getProps();
    const int dx = x - _touchStart.x;
    if (!longTap && (dx < 0 ? -dx : dx) >= _rect.width() * MIN_PAGE_TURN_SWIPE_WIDTH) {
        if (props->getIntDef(PROP_CTRL_PAGE_TURN_SWIPES, 1) == 1)
            _wm->postCommand(dx < 0 ? DCMD_PAGEDOWN : DCMD_PAGEUP, 1);
        return true;
    }
    int zone = getTapZone(x, y, props);
    if (zone != getTapZone(_touchStart.x, _touchStart.y, props))
        return true;
    int command = 0, param = 0;
    getCommandForTapZone(zone, props, longTap, command, param);
    // The zones do what they do in a book (a tester: "the tap zones could work as in the usual
    // mode"; every zone but the page turns opened the list of this window). The command comes
    // back to onCommand, which does it, hands it to the book window or leaves it out.
    if (command != 0)
        _wm->postCommand(command, param);
    return true;
}

bool PbPageWindow::onCommand(int command, int params)
{
    if (_adjust) {
        // the keys of the page turn and Back leave the frame as it was
        if (command == MCMD_CANCEL || command == MCMD_QUIT || command == PB_CMD_MAIN_MENU || command == PB_QUICK_MENU)
            adjustEnd(false);
        return true;
    }
    if (command >= PB_TOC_SAFE_CMD_RANGE && command < PB_TOC_SAFE_CMD_RANGE + _doc->pageCount() + 1) {
        goToPage(command - PB_TOC_SAFE_CMD_RANGE - 1);
        return true;
    }
    if (PbTocList::isTocListCommand(command)) {
        PbTocList::instance().onCommand(_wm, command, params);
        return true;
    }
    if (command >= PB_PAGEVIEW_CMD_SCALE && command < PB_PAGEVIEW_CMD_SCALE + PB_PAGE_SCALE_COUNT) {
        _scale = command - PB_PAGEVIEW_CMD_SCALE;
        _frame = 0;
        saveState();
        changed();
        return true;
    }
    if (command >= PB_PAGEVIEW_CMD_PAGE && command <= PB_PAGEVIEW_CMD_LAST) {
        size_t index = (size_t)(command - PB_PAGEVIEW_CMD_PAGE);
        if (index < _marks.size())
            goToPage(_marks[index]);
        return true;
    }
    switch (command) {
    case DCMD_PAGEDOWN:
        turn(1);
        return true;
    case DCMD_PAGEUP:
        turn(-1);
        return true;
    case DCMD_BEGIN:
        goToPage(0);
        return true;
    case DCMD_END:
        goToPage(_doc->pageCount() - 1);
        return true;
    case PB_QUICK_MENU:
        // the menu of a book: the touch menu with the icons and the page bar where the skin has
        // it, the menu of nine cells elsewhere. It asks this window for the pages, the chapters
        // and the bookmark (cr3pocketbook.cpp, tmPages); what is chosen in it comes here as the
        // command of the action.
        return main_win->onCommand(command, params);
    case PB_PAGEVIEW_CMD_MENU:
    case PB_CMD_MAIN_MENU:
    case MCMD_MAIN_MENU:
        showMenu();
        return true;
    case PB_PAGEVIEW_CMD_CLOSE:
        setDirty();
        return true;
    case PB_PAGEVIEW_CMD_CONTENTS:
    case PB_CMD_CONTENTS:
        showContents();
        return true;
    case PB_PAGEVIEW_CMD_GOTO:
    case MCMD_GO_PAGE:
        OpenPageSelector(pageChosen);
        return true;
    case PB_PAGEVIEW_CMD_GOTO_APPLY:
        goToPage(params - 1);
        return true;
    case PB_PAGEVIEW_CMD_SCALE_LIST:
        showScaleList();
        return true;
    case PB_PAGEVIEW_CMD_LEAFING:
        // next page at once, by frames, as a ribbon, and round again
        _leafing = _leafing == PB_PAGE_LEAF_PAGE ? PB_PAGE_LEAF_FRAMES
                   : _leafing == PB_PAGE_LEAF_FRAMES ? PB_PAGE_LEAF_RIBBON : PB_PAGE_LEAF_PAGE;
        _frame = 0;
        saveState();
        changed();
        showMenu();
        return true;
    case PB_PAGEVIEW_CMD_ADJUST:
        adjustBegin();
        return true;
    case PB_PAGEVIEW_CMD_MARK:
        toggleMark();
        showMarks();
        return true;
    case PB_CMD_SET_BOOKMARK:
    case PB_CMD_UNSET_BOOKMARK:
        // a long tap on the bookmark icon of the touch menu, or the action of a key
        toggleMark();
        setDirty();
        return true;
    case PB_PAGEVIEW_CMD_MARKS:
    case MCMD_BOOKMARK_LIST:
    case MCMD_BOOKMARK_LIST_GO_MODE:
        showMarks();
        return true;
    case PB_PAGEVIEW_CMD_INFO:
    case MCMD_ABOUT:
        showInfo();
        return true;
    case MCMD_GO_PAGE_APPLY:
        // the page bar of the touch menu and its number box: a page from 1
        goToPage(params < 1 ? 0 : params > _doc->pageCount() ? _doc->pageCount() - 1 : params - 1);
        return true;
    case MCMD_DICT:
    case MCMD_SEARCH:
    case MCMD_CITE:
        notInOriginal(command);
        return true;
    case PB_PAGEVIEW_CMD_AS_BOOK: {
        // the window goes; the book window takes the book made of the file
        saveState();
        CRGUIWindowManager * wm = _wm;
        const lString32 path = _path;
        const int page = _page;
        wm->closeWindow(this);
        if (!pbPdfBookFromPages(wm, path, page)) {
            // no book could be made: the pages again
            setMode(path, PB_PAGE_MODE_PAGES);
            if (open(wm, path))
                wm->update(true);
        }
        return true;
    }
    case PB_PAGEVIEW_CMD_SETTINGS:
        return main_win->onCommand(MCMD_SETTINGS, 0);
    case PB_PAGEVIEW_CMD_EXIT:
    case MCMD_CANCEL:
    case MCMD_QUIT:
        saveState();
        _wm->closeAllWindows();
        return true;
    default:
        break;
    }
    // What belongs to a book and its text has nothing to act on here. The rest (the settings
    // menu and what it posts, the front light, the rotation of the screen, the full update, the
    // standby...) is the business of the book window, as in the book mode.
    // the commands of the book view (lvdocview.h) end where those of the main menu start
    if (command >= 100 && command < MAIN_MENU_COMMANDS_START)
        return true;
    switch (command) {
    case MCMD_LONG_FORWARD:
        goToPage(_page + 10 < _doc->pageCount() ? _page + 10 : _doc->pageCount() - 1);
        return true;
    case MCMD_LONG_BACK:
        goToPage(_page > 10 ? _page - 10 : 0);
        return true;
    case MCMD_GO_LINK:
    case MCMD_GO_LINK_APPLY:
    case MCMD_OPEN_RECENT_BOOK:
    case MCMD_SWITCH_TO_RECENT_BOOK:
    case MCMD_SEARCH_FINDFIRST:
    case MCMD_DICT_VKEYBOARD:
    case MCMD_DICT_FIND:
    case MCMD_NEXT_MODE:
    case MCMD_PREV_MODE:
    case MCMD_RECENT_BOOK_LIST:
    case MCMD_KBD_NEXTLAYOUT:
    case MCMD_KBD_PREVLAYOUT:
    case MCMD_HELP:
    case MCMD_HELP_KEYS:
    case PB_CMD_SELECT_DICT:
    case PB_CMD_TRANSLATE:
    case PB_CMD_BOOKMARK_REMOVE:
    case PB_CMD_STATUS_LINE:
    case PB_CMD_CLEAR_CACHE:
        return true;
    default:
        return main_win->onCommand(command, params);
    }
}

// ------------------------------------------------------------------------------------ as a book
const std::vector<PbPageTocEntry> & PbPageWindow::toc()
{
    if (!_tocRead) {
        _doc->contents(_toc);
        _tocRead = true;
    }
    return _toc;
}

bool PbPageWindow::isMarked() const
{
    for (size_t i = 0; i < _marks.size(); i++)
        if (_marks[i] == _page)
            return true;
    return false;
}

void PbPageWindow::toggleMark()
{
    bool found = false;
    for (size_t i = 0; i < _marks.size(); i++)
        if (_marks[i] == _page) {
            _marks.erase(_marks.begin() + i);
            found = true;
            break;
        }
    if (!found) {
        size_t at = 0;
        while (at < _marks.size() && _marks[at] < _page)
            at++;
        _marks.insert(_marks.begin() + at, _page);
    }
    saveState();
}

lString32 PbPageWindow::chapterName(int page)
{
    // the last entry of the contents that starts at this page or before it
    const std::vector<PbPageTocEntry> & t = toc();
    const PbPageTocEntry * found = NULL;
    for (size_t i = 0; i < t.size(); i++)
        if (t[i].page >= 0 && t[i].page <= page && (found == NULL || t[i].page >= found->page))
            found = &t[i];
    return found ? Utf8ToUnicode(found->title.c_str()) : lString32::empty_str;
}

void PbPageWindow::sectionBounds(LVArray<int> & bounds)
{
    bounds.clear();
    const std::vector<PbPageTocEntry> & t = toc();
    const int count = _doc->pageCount();
    if (count < 1)
        return;
    // The chapters are the entries of the top level; a file that hangs everything under one top
    // entry (the title of the book) gives the level under it.
    int level = 0, tops = 0;
    for (size_t i = 0; i < t.size(); i++)
        if (t[i].level == 0)
            tops++;
    if (tops < 2)
        level = 1;
    std::vector<int> pages;
    for (size_t i = 0; i < t.size(); i++)
        if (t[i].level <= level && t[i].page > 0 && t[i].page < count)
            pages.push_back(t[i].page);
    std::sort(pages.begin(), pages.end());
    int last = -1;
    for (size_t i = 0; i < pages.size(); i++) {
        // rounded up, so that the bound taken back to a page is the page of the entry
        const int b = (int)(((lInt64)pages[i] * 10000 + count - 1) / count);
        if (b > last)
            bounds.add(b);
        last = b;
    }
}

lString32 PbPageWindow::progressText(int page, bool tillChapterEnd)
{
    const int count = _doc->pageCount();
    lString32 text = lString32::itoa(page + 1) + U" / ";
    if (tillChapterEnd) {
        LVArray<int> bounds;
        sectionBounds(bounds);
        int left = 0;
        for (int i = 0; i < bounds.length(); i++) {
            const int start = (int)(((lInt64)bounds[i] * count) / 10000);
            if (start >= page) {
                left = start - page;
                if (i != bounds.length() - 1)
                    left++;
                break;
            }
        }
        if (left + page + 1 != count)
            text << lString32::itoa(left) << U" / ";
    }
    return text + lString32::itoa(count);
}

// The status line: the settings of the one of a book are given to a view of the engine made for
// it, which draws the line as it draws it over a book.
void PbPageWindow::setupHead()
{
    if (_head == NULL) {
        _head = new LVDocView(_wm->getScreen()->getCanvas()->GetBitsPerPixel());
        _head->setVisiblePageCount(1);
        _head->setBatteryIcons(_wm->getBatteryIcons());
        // the title the file names, or the name of the file
        std::string title, author;
        _doc->info(title, author);
        lString32 name = Utf8ToUnicode(title.c_str());
        name.trim();
        if (name.empty())
            name = LVExtractFilenameWithoutExtension(_path);
        _head->getDocProps()->setString(DOC_PROP_TITLE, name);
        _head->getDocProps()->setString(DOC_PROP_AUTHORS, Utf8ToUnicode(author.c_str()));
        // the marks of the chapters come from the contents of the file
        sectionBounds(_head->getSectionBounds(10000, 32, true));
    }
    static const char * const names[] = {
        PROP_STATUS_LINE, PROP_STATUS_FONT_COLOR, PROP_STATUS_FONT_FACE, PROP_STATUS_FONT_SIZE,
        PROP_STATUS_FONT_EMBOLDEN, PROP_SHOW_TIME, PROP_SHOW_TIME_12HOURS, PROP_TIME_FORMAT, PROP_SHOW_TITLE,
        PROP_STATUS_CHAPTER_MARKS, PROP_SHOW_BATTERY, PROP_SHOW_BATTERY_PERCENT, PROP_SHOW_PAGE_COUNT,
        PROP_SHOW_PAGE_NUMBER, PROP_SHOW_CHAPTER_PAGES_REMAIN, NULL
    };
    CRPropRef all = main_win->getProps();
    CRPropRef part = LVCreatePropsContainer();
    for (int i = 0; names[i]; i++) {
        lString32 value;
        if (all->getString(names[i], value))
            part->setString(names[i], value);
    }
    _head->propsApply(part);
    _head->Resize(_rect.width(), _rect.height());
    layout();
}

void PbPageWindow::layout()
{
    lvRect rc = _rect;
    if (_head != NULL) {
        const int h = _head->getPageHeaderHeight();
        // 1: at the top, 2: at the bottom (PAGE_HEADER_POS_* of lvdocview.cpp)
        const int pos = _head->getPageheaderPosition();
        if (h > 0 && h < rc.height() / 4) {
            if (pos == 1)
                rc.top += h;
            else if (pos == 2)
                rc.bottom -= h;
        }
    }
    const bool resized = rc.width() != _pageRc.width() || rc.height() != _pageRc.height();
    _pageRc = rc;
    if (resized) {
        _frame = 0;
        changed();
    }
}

void PbPageWindow::drawHead(LVDrawBuf * canvas)
{
    if (_head == NULL || _pageRc.height() >= _rect.height())
        return;
    lvRect band = _rect;
    if (_pageRc.top > _rect.top)
        band.bottom = _pageRc.top;
    else
        band.top = _pageRc.bottom;
    canvas->FillRect(band.left, band.top, band.right, band.bottom, 0xFFFFFF);
    int charge = 0;
    bool charging = false;
    if (_wm->getBatteryStatus(charge, charging))
        _head->setBatteryState(charging ? CR_BATTERY_STATE_CHARGING : CR_BATTERY_STATE_DISCHARGING,
                               charging ? CR_BATTERY_CHARGER_USB : CR_BATTERY_CHARGER_NO, charge);
    lvRect rc;
    _head->getPageHeaderRectangle(1, rc);
    rc.left += _rect.left;
    rc.right += _rect.left;
    rc.top += _rect.top;
    rc.bottom += _rect.top;
    // the place in percent is that of the empty book of the view: left out
    _head->drawPageHeader(canvas, rc, _page, _head->getPageHeaderInfo() & ~PGHDR_PERCENT, _doc->pageCount());
}

/// What the file tells of itself, as "Book info" of a book does.
void PbPageWindow::showInfo()
{
    std::string title, author;
    _doc->info(title, author);
    lString8 rows;
    addPropLine(rows, C_("Book", "Title"), Utf8ToUnicode(title.c_str()));
    addPropLine(rows, _("Author(s)"), Utf8ToUnicode(author.c_str()));
    addPropLine(rows, _("File name"), LVExtractFilename(_path));
    addPropLine(rows, _("File path"), LVExtractPath(_path));
    struct stat st;
    if (stat(UnicodeToUtf8(_path).c_str(), &st) == 0)
        addPropLine(rows, _("File size"), lString32::itoa((unsigned int)st.st_size));
    addPropLine(rows, _("File format"), lString32(_doc->kind() == PB_PAGEDOC_PDF ? "PDF" : "DjVu"));
    addPropLine(rows, _("Current page"), lString32::itoa(_page + 1));
    addPropLine(rows, _("Total pages"), lString32::itoa(_doc->pageCount()));
    PbPageSize size;
    if (_doc->pageSize(_page, size) && size.dpi > 0) {
        // in millimetres, as paper is named
        lString32 mm;
        mm << lString32::itoa((int)(size.width * 25.4f / size.dpi + 0.5f)) << U" x "
           << lString32::itoa((int)(size.height * 25.4f / size.dpi + 0.5f)) << U" mm";
        addPropLine(rows, _("Page size"), mm);
    }
    lString8 txt;
    txt << "<table><col width=\"34%\"/><col width=\"66%\"/>\n" << rows << "</table>\n";
    txt = CRViewDialog::makeFb2Xml(txt);
    CRViewDialog * dlg = new CRViewDialog(_wm, tr("Book info"), txt, lvRect(), true, true);
    dlg->getDocView()->setVisiblePageCount(1);
    dlg->getDocView()->setFontSize(main_win->getProps()->getIntDef(PROP_FILE_PROPS_FONT_SIZE, 22));
    _wm->activateWindow(dlg);
}

// ------------------------------------------------------------------------------------ lists
void PbPageWindow::showMenu()
{
    // What belongs to the view of a page. The contents, the bookmarks, the settings, the rotation
    // and the way home are where a book has them, in the menu of the reader; they were rows here.
    lvRect rc = _wm->getScreen()->getRect();
    lString32 caption = tr(_doc->kind() == PB_PAGEDOC_PDF ? "PDF" : "DjVu");
    OpdsMenu * menu = new OpdsMenu(_wm, caption, rc, false);
    menu->setupRows(true);
    menu->setCloseCommand(PB_PAGEVIEW_CMD_CLOSE);
    menu->add(PB_PAGEVIEW_CMD_SCALE_LIST, tr("Scale") + U": " + tr(scaleName(_scale)));
    menu->add(PB_PAGEVIEW_CMD_LEAFING,
              tr("Leafing") + U": " + tr(_leafing == PB_PAGE_LEAF_FRAMES ? "By frames"
                                         : _leafing == PB_PAGE_LEAF_RIBBON ? "As a ribbon" : "Next page at once"),
              _leafing == PB_PAGE_LEAF_FRAMES ? tr("The frames of a page one after another, then the next page")
              : _leafing == PB_PAGE_LEAF_RIBBON ? tr("The pages one under another; a page turn moves on by a screen")
              : tr("A page turn shows the same frame of the next page"));
    menu->add(PB_PAGEVIEW_CMD_ADJUST, tr("Set the frame"),
              tr("Zoom, position and turn of the page; kept for the next pages"));
    // a file whose page has text (a PDF with text, a DjVu with a text layer) can be read as a
    // book: the text in the fonts of the reader, with the dictionary, the search and the citations
    if (_doc->hasText(_page))
        menu->add(PB_PAGEVIEW_CMD_AS_BOOK, tr("View as a book"),
                  tr("The text of the file in the fonts of the reader, from this page"));
    menu->add(PB_PAGEVIEW_CMD_GOTO, tr("Go to page"),
              tr("Page") + U" " + lString32::itoa(_page + 1) + U" / " + lString32::itoa(_doc->pageCount()));
    menu->add(PB_PAGEVIEW_CMD_INFO, tr("Book info"));
    menu->add(PB_PAGEVIEW_CMD_EXIT, tr("Close the book"));
    activate(_wm, menu);
}

void PbPageWindow::showScaleList()
{
    lvRect rc = _wm->getScreen()->getRect();
    OpdsMenu * menu = new OpdsMenu(_wm, tr("Scale"), rc, false);
    menu->setupRows(true);
    menu->setCloseCommand(PB_PAGEVIEW_CMD_CLOSE);
    static const char * const notes[PB_PAGE_SCALE_COUNT] = {
        _("The page with its margins, whole in the screen"),
        _("The page as wide as the screen"),
        _("The margins cut off, the content whole in the screen"),
        _("The margins cut off, the content as wide as the screen"),
        _("The frame you have set: zoom, position and turn"),
        _("A crooked scan: each page turned level by itself, its margins cut off")
    };
    // in the order of the list: the automatic scale stands with the other ready ones, the own frame last
    static const int order[PB_PAGE_SCALE_COUNT] = {
        PB_PAGE_SCALE_PAGE, PB_PAGE_SCALE_WIDTH, PB_PAGE_SCALE_CONTENT, PB_PAGE_SCALE_CONTENT_WIDTH,
        PB_PAGE_SCALE_AUTO, PB_PAGE_SCALE_OWN
    };
    const int par = _page & 1;
    for (int n = 0; n < PB_PAGE_SCALE_COUNT; n++) {
        const int i = order[n];
        // the own frame is offered once it is set
        if (i == PB_PAGE_SCALE_OWN && !_own[par].set && !_own[par ^ 1].set)
            continue;
        OpdsItem * item = new OpdsItem(menu, PB_PAGEVIEW_CMD_SCALE + i, tr(scaleName(i)), tr(notes[i]));
        item->mark = i == _scale;
        menu->addItem(item);
    }
    activate(_wm, menu);
}

void PbPageWindow::showMarks()
{
    lvRect rc = _wm->getScreen()->getRect();
    OpdsMenu * menu = new OpdsMenu(_wm, tr("Bookmarks"), rc, true);
    menu->setupRows(false);
    menu->setCloseCommand(PB_PAGEVIEW_CMD_CLOSE);
    // the first row sets and takes off the bookmark of the page shown (it was a row of the menu)
    menu->add(PB_PAGEVIEW_CMD_MARK, tr("Bookmark on this page") + U": " + tr(isMarked() ? "On" : "Off"));
    for (size_t i = 0; i < _marks.size() && i < (size_t)(PB_PAGEVIEW_CMD_LAST - PB_PAGEVIEW_CMD_PAGE); i++) {
        OpdsItem * item = new OpdsItem(menu, PB_PAGEVIEW_CMD_PAGE + (int)i,
                                       tr("Page") + U" " + lString32::itoa(_marks[i] + 1), lString32::empty_str);
        item->mark = _marks[i] == _page;
        menu->addItem(item);
    }
    activate(_wm, menu);
}

void PbPageWindow::showContents()
{
    const std::vector<PbPageTocEntry> & toc = this->toc();
    if (toc.empty()) {
        // as a book without contents tells it
        Message(ICON_INFORMATION, const_cast<char *>("CoolReader"), const_cast<char *>("@No_contents"), 2000);
        return;
    }
    // the list of the book contents takes the entries of the firmware type; pages from 1
    std::vector<tocentry> entries(toc.size());
    std::vector<lString8> titles(toc.size());
    for (size_t i = 0; i < toc.size(); i++) {
        titles[i] = lString8(toc[i].title.c_str());
        memset(&entries[i], 0, sizeof(tocentry));
        entries[i].level = toc[i].level + 1;
        entries[i].page = toc[i].page + 1;
        entries[i].position = toc[i].page + 1;
        entries[i].text = const_cast<char *>(titles[i].c_str());
    }
    PbTocList::instance().open(_wm, &entries[0], (int)entries.size(), _page + 1);
}
