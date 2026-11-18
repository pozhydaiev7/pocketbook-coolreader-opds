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

extern V3DocViewWin * main_win;
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

const char * scaleName(int scale)
{
    switch (scale) {
    case PB_PAGE_SCALE_PAGE: return _("Whole page");
    case PB_PAGE_SCALE_WIDTH: return _("Page width");
    case PB_PAGE_SCALE_CONTENT: return _("Whole content");
    case PB_PAGE_SCALE_CONTENT_WIDTH: return _("Content width");
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
      _adjust(false), _adjAngle(0), _touchStarted(false)
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
    delete _doc;
}

void PbPageWindow::closing()
{
    saveState();
}

/// The settings of the menu "PDF view settings" / "DjVu view settings": what a file opened for
/// the first time starts with, and whether the page number is shown.
void PbPageWindow::readDefaults()
{
    CRPropRef props = main_win->getProps();
    bool pdf = _doc->kind() == PB_PAGEDOC_PDF;
    _scale = props->getIntDef(pdf ? PROP_POCKETBOOK_PDF_SCALE : PROP_POCKETBOOK_DJVU_SCALE, PB_PAGE_SCALE_CONTENT);
    _leafing = props->getIntDef(pdf ? PROP_POCKETBOOK_PDF_LEAFING : PROP_POCKETBOOK_DJVU_LEAFING, PB_PAGE_LEAF_PAGE);
    _showNumber = props->getIntDef(pdf ? PROP_POCKETBOOK_PDF_PAGE_NUMBER : PROP_POCKETBOOK_DJVU_PAGE_NUMBER, 1) != 0;
    if (_scale < 0 || _scale >= PB_PAGE_SCALE_OWN)
        _scale = PB_PAGE_SCALE_CONTENT;
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
        _scale = PB_PAGE_SCALE_CONTENT;
    if (_leafing != PB_PAGE_LEAF_FRAMES)
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
    bool resized = rc.width() != _rect.width() || rc.height() != _rect.height();
    CRGUIWindowBase::setRect(rc);
    if (resized) {
        _frame = 0;
        changed();
    }
}

void PbPageWindow::reactivated()
{
    // back from a list or from the settings menu: the page number may be turned on or off
    CRPropRef props = main_win->getProps();
    bool pdf = _doc->kind() == PB_PAGEDOC_PDF;
    _showNumber = props->getIntDef(pdf ? PROP_POCKETBOOK_PDF_PAGE_NUMBER : PROP_POCKETBOOK_DJVU_PAGE_NUMBER, 1) != 0;
    setDirty();
}

// ------------------------------------------------------------------------------------ frames
void PbPageWindow::framesOfPage(int page, std::vector<PbPageView> & frames)
{
    const int par = page & 1;
    _doc->framesOf(page, _scale, _leafing, _angle[par], _own[par], _rect.width(), _rect.height(), frames);
}

void PbPageWindow::drawShot(Shot & shot, int page, int frame)
{
    const int W = _rect.width(), H = _rect.height();
    std::vector<PbPageView> frames;
    // the parts of the time, for the log of the callers
    _doc->readMs = 0;
    _doc->boxMs = 0;
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
    shot.ready = true;
}

void PbPageWindow::target(int direction, int & page, int & frame)
{
    page = _page;
    frame = _frame;
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
        CRLog::info("perf: picture mode: page %d frame %d prepared ahead in %.1f ms: page read %.1f, content box %.1f",
                    page + 1, frame, nowMs() - t0, _doc->readMs, _doc->boxMs);
}

// ------------------------------------------------------------------------------------ drawing
void PbPageWindow::draw()
{
    const int W = _rect.width(), H = _rect.height();
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
        image.DrawTo(canvas, _rect.left, _rect.top, 0, NULL);
    } else {
        // the pixels are in the depth of the canvas already (toCanvasDepth)
        LVGrayDrawBuf image(W, H, bpp, &_shown.pixels[0]);
        image.DrawTo(canvas, _rect.left, _rect.top, 0, NULL);
    }
    if (_adjust)
        drawAdjustPanel(canvas);
    else if (_showNumber)
        drawNumber(canvas);
    if (CRLog::isInfoEnabled()) {
        if (drawnNow)
            CRLog::info("perf: picture mode: page %d frame %d drawn now in %.1f ms: page read %.1f, content box %.1f; "
                        "to the canvas %.1f ms", _page + 1, _frame, t1 - t0, _doc->readMs, _doc->boxMs, nowMs() - t1);
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
    const int size = _rect.width() / 45 > 14 ? _rect.width() / 45 : 14;
    LVFontRef font = pageWindowFont(size);
    if (font.isNull())
        return;
    lString32 text = lString32::itoa(_page + 1) + U" / " + lString32::itoa(_doc->pageCount());
    const int tw = font->getTextWidth(text.c_str(), text.length());
    const int th = font->getHeight();
    const int pad = size / 3;
    const int x1 = _rect.right - pad, y1 = _rect.bottom - pad;
    const int x0 = x1 - tw - pad * 2, y0 = y1 - th;
    canvas->FillRect(x0, y0, x1, y1, 0xFFFFFF);
    canvas->SetTextColor(0x000000);
    font->DrawTextString(canvas, x0 + pad, y0, text.c_str(), text.length(), '?', NULL, false);
}

// ------------------------------------------------------------------------------------ the frame being set
static const char * const ADJUST_LABELS[8] = {
    "\xE2\x88\x92", "+", "\xE2\x88\x92" "0.5\xC2\xB0", "+0.5\xC2\xB0",
    "90\xC2\xB0", NULL, NULL, NULL   // the last three are words: Reset, Cancel, Done
};

void PbPageWindow::adjustButtons(std::vector<lvRect> & rects)
{
    // two rows of four buttons at the bottom of the window
    rects.clear();
    const int W = _rect.width();
    const int bh = _rect.height() / 14;
    const int top = _rect.bottom - bh * 2;
    for (int i = 0; i < 8; i++) {
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
    for (int i = 0; i < 8; i++) {
        const lvRect & rc = rects[i];
        canvas->FillRect(rc.left, rc.top, rc.right, rc.bottom, 0x000000);
        canvas->FillRect(rc.left + 2, rc.top + 2, rc.right - 2, rc.bottom - 2, 0xFFFFFF);
        lString32 text = ADJUST_LABELS[i] != NULL ? Utf8ToUnicode(lString8(ADJUST_LABELS[i]))
                       : i == 5 ? tr("Reset") : i == 6 ? tr("Cancel") : tr("Done");
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
        for (int i = 0; i < 8; i++) {
            if (!rects[i].isPointInside(lvPoint(x, y)))
                continue;
            switch (i) {
            // (a twentieth a step: a tenth was too coarse to fit a column to the screen)
            case 0: _adjFrame.zoom /= 1.05f; break;
            case 1: _adjFrame.zoom *= 1.05f; break;
            case 2: _adjAngle -= 0.5f; break;
            case 3: _adjAngle += 0.5f; break;
            case 4: _adjAngle += 90.0f; break;
            case 5: {
                // the whole content, upright
                PbPageSize size;
                PbPageBox box;
                if (_doc->pageSize(_page, size)) {
                    box = PbPageBox(0, 0, size.width, size.height);
                    _doc->contentBox(_page, box);
                    PbPageView view = PbPageDoc::viewOfBox(box, 0, _rect.width(), _rect.height());
                    _adjFrame.cx = view.cx;
                    _adjFrame.cy = view.cy;
                    _adjFrame.zoom = view.zoom;
                    _adjAngle = 0;
                }
                break;
            }
            case 6: adjustEnd(false); return redrawAfterTouch();
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
    // a drag moves the page under the finger: the centre of the frame goes the other way
    const float a = _adjAngle * PI_F / 180.0f;
    const float c = cosf(a), s = sinf(a);
    _adjFrame.cx -= (dx * c + dy * s) / _adjFrame.zoom;
    _adjFrame.cy -= (-dx * s + dy * c) / _adjFrame.zoom;
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
    if (command == DCMD_PAGEDOWN || command == DCMD_PAGEUP || command == DCMD_BEGIN || command == DCMD_END)
        _wm->postCommand(command, param);
    else if (command != 0)
        // every other zone of the book opens the menu of the picture mode
        _wm->postCommand(PB_PAGEVIEW_CMD_MENU, 0);
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
    case PB_PAGEVIEW_CMD_MENU:
    case PB_QUICK_MENU:
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
        _leafing = _leafing == PB_PAGE_LEAF_FRAMES ? PB_PAGE_LEAF_PAGE : PB_PAGE_LEAF_FRAMES;
        _frame = 0;
        saveState();
        changed();
        showMenu();
        return true;
    case PB_PAGEVIEW_CMD_ADJUST:
        adjustBegin();
        return true;
    case PB_PAGEVIEW_CMD_MARK: {
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
        showMenu();
        return true;
    }
    case PB_PAGEVIEW_CMD_MARKS:
        showMarks();
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
    case MCMD_GO_PAGE_APPLY:
    case MCMD_GO_LINK:
    case MCMD_GO_LINK_APPLY:
    case MCMD_OPEN_RECENT_BOOK:
    case MCMD_SWITCH_TO_RECENT_BOOK:
    case MCMD_SEARCH_FINDFIRST:
    case MCMD_DICT_VKEYBOARD:
    case MCMD_DICT_FIND:
    case MCMD_NEXT_MODE:
    case MCMD_PREV_MODE:
    case MCMD_DICT:
    case MCMD_CITE:
    case MCMD_SEARCH:
    case MCMD_BOOKMARK_LIST:
    case MCMD_BOOKMARK_LIST_GO_MODE:
    case MCMD_RECENT_BOOK_LIST:
    case MCMD_KBD_NEXTLAYOUT:
    case MCMD_KBD_PREVLAYOUT:
    case MCMD_HELP:
    case MCMD_HELP_KEYS:
    case MCMD_ABOUT:
    case PB_CMD_SELECT_DICT:
    case PB_CMD_TRANSLATE:
    case PB_CMD_SET_BOOKMARK:
    case PB_CMD_UNSET_BOOKMARK:
    case PB_CMD_BOOKMARK_REMOVE:
    case PB_CMD_STATUS_LINE:
    case PB_CMD_CLEAR_CACHE:
    case PB_CMD_READING_STATS:
        return true;
    default:
        return main_win->onCommand(command, params);
    }
}

// ------------------------------------------------------------------------------------ lists
void PbPageWindow::showMenu()
{
    lvRect rc = _wm->getScreen()->getRect();
    lString32 caption = tr(_doc->kind() == PB_PAGEDOC_PDF ? "PDF" : "DjVu");
    OpdsMenu * menu = new OpdsMenu(_wm, caption, rc, false);
    menu->setupRows(true);
    menu->setCloseCommand(PB_PAGEVIEW_CMD_CLOSE);
    std::vector<PbPageTocEntry> toc;
    _doc->contents(toc);
    if (!toc.empty())
        menu->add(PB_PAGEVIEW_CMD_CONTENTS, tr("Contents"));
    menu->add(PB_PAGEVIEW_CMD_GOTO, tr("Go to page"),
              tr("Page") + U" " + lString32::itoa(_page + 1) + U" / " + lString32::itoa(_doc->pageCount()));
    menu->add(PB_PAGEVIEW_CMD_SCALE_LIST, tr("Scale") + U": " + tr(scaleName(_scale)));
    menu->add(PB_PAGEVIEW_CMD_LEAFING,
              tr("Leafing") + U": " + tr(_leafing == PB_PAGE_LEAF_FRAMES ? "By frames" : "Next page at once"),
              _leafing == PB_PAGE_LEAF_FRAMES ? tr("The frames of a page one after another, then the next page")
                                              : tr("A page turn shows the same frame of the next page"));
    menu->add(PB_PAGEVIEW_CMD_ADJUST, tr("Set the frame"),
              tr("Zoom, position and turn of the page; kept for the next pages"));
    bool marked = false;
    for (size_t i = 0; i < _marks.size(); i++)
        if (_marks[i] == _page)
            marked = true;
    menu->add(PB_PAGEVIEW_CMD_MARK, tr("Bookmark on this page") + U": " + tr(marked ? "On" : "Off"));
    if (!_marks.empty())
        menu->add(PB_PAGEVIEW_CMD_MARKS, tr("Bookmarks"), lString32::itoa((int)_marks.size()));
    // a file whose page has text (a PDF with text, a DjVu with a text layer) can be read as a
    // book: the text in the fonts of the reader
    if (_doc->hasText(_page))
        menu->add(PB_PAGEVIEW_CMD_AS_BOOK, tr("View as a book"),
                  tr("The text of the file in the fonts of the reader, from this page"));
    menu->add(PB_PAGEVIEW_CMD_SETTINGS, tr("Settings"));
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
        _("The frame you have set: zoom, position and turn")
    };
    const int par = _page & 1;
    for (int i = 0; i < PB_PAGE_SCALE_COUNT; i++) {
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
    std::vector<PbPageTocEntry> toc;
    _doc->contents(toc);
    if (toc.empty())
        return;
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
