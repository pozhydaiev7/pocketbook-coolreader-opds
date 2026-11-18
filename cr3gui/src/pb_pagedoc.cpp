/*
 * A document of pages: PDF (MuPDF) and DjVu (DjVuLibre) behind one interface. See pb_pagedoc.h.
 *
 * Speed first: a frame is drawn by the library into the buffer of the caller at its final size
 * and depth (no colour picture, no scaling afterwards); the commands of the page shown are kept
 * (MuPDF display list) and so is the decoded page (DjVu), so another frame of the same page does
 * not read the page again.
 */
#include "pb_pagedoc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <mupdf/fitz.h>
#include "libdjvu/ddjvuapi.h"
#include "libdjvu/miniexp.h"

#if defined(__GLIBC__) && defined(__arm__) && (__GLIBC__ == 2) && (__GLIBC_MINOR__ < 25)
/* glibc before 2.25 (the SDK of the reader has 2.9) has no getentropy(); MuPDF asks for 44 bytes
 * once, as the seed of its random numbers. */
#include <fcntl.h>
#include <unistd.h>
extern "C" int getentropy(void * buffer, size_t length)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t got = read(fd, buffer, length);
    close(fd);
    return got == (ssize_t)length ? 0 : -1;
}
#endif

static const float PB_PI = 3.14159265358979f;

static double pbNowMs()
{
    // gettimeofday: clock_gettime is in librt in the C library of the SDK
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// ------------------------------------------------------------------------------------ scaling and rotation
void pbBlitGray(const unsigned char * src, int srcW, int srcH, int srcStride,
                float ox, float oy, float ax, float ay, float bx, float by,
                unsigned char * dst, int dstW, int dstH, int dstStride, bool whiteOutside)
{
    if (srcW < 1 || srcH < 1 || dstW < 1 || dstH < 1)
        return;
    // 16.16 fixed point: the source position of a pixel is that of its left neighbour plus (ax, ay)
    const int stepX = (int)lrintf(ax * 65536.0f);
    const int stepY = (int)lrintf(ay * 65536.0f);
    const unsigned maxX = (unsigned)(srcW - 1), maxY = (unsigned)(srcH - 1);
    // a step of dst over several pixels of src: n x n of them, spread over the step, are averaged
    const float stepA = sqrtf(ax * ax + ay * ay), stepB = sqrtf(bx * bx + by * by);
    const float step = stepA > stepB ? stepA : stepB;
    // (one in two pixels of the step each way, at most 4 x 4: thin lines of the page still count
    // in the small picture the content box is found on)
    const int n = step < 2.0f ? 1 : step >= 7.0f ? 4 : (int)ceilf(step * 0.5f);
    int offX[16], offY[16];
    for (int j = 0; j < n; j++)
        for (int i = 0; i < n; i++) {
            offX[j * n + i] = (int)lrintf(((2 * i + 1) / (2.0f * n) - 0.5f) * step * 65536.0f);
            offY[j * n + i] = (int)lrintf(((2 * j + 1) / (2.0f * n) - 0.5f) * step * 65536.0f);
        }
    const int recip = 65536 / (n * n);
    const bool upright = stepY == 0 && fabsf(ay) < 1e-7f && fabsf(bx) < 1e-7f;
    std::vector<unsigned char> line(upright && n == 1 ? (size_t)srcW : 0);
    // the source is taken from half a pixel around it: its edge pixels are repeated there
    const float loU = -0.5f, hiU = srcW - 0.5f, loV = -0.5f, hiV = srcH - 0.5f;
    for (int y = 0; y < dstH; y++) {
        unsigned char * d = dst + (size_t)y * dstStride;
        const float u0 = ox + bx * y, v0 = oy + by * y;
        // the pixels of this row that fall on the source: both positions are linear in x
        float xa = 0, xb = (float)(dstW - 1);
        bool any = true;
        if (fabsf(ax) < 1e-7f) {
            any = u0 >= loU && u0 <= hiU;
        } else {
            float t0 = (loU - u0) / ax, t1 = (hiU - u0) / ax;
            if (t0 > t1) { float t = t0; t0 = t1; t1 = t; }
            if (t0 > xa) xa = t0;
            if (t1 < xb) xb = t1;
        }
        if (any && fabsf(ay) < 1e-7f) {
            any = v0 >= loV && v0 <= hiV;
        } else if (any) {
            float t0 = (loV - v0) / ay, t1 = (hiV - v0) / ay;
            if (t0 > t1) { float t = t0; t0 = t1; t1 = t; }
            if (t0 > xa) xa = t0;
            if (t1 < xb) xb = t1;
        }
        int x0 = (int)ceilf(xa), x1 = (int)floorf(xb);
        if (!any || x1 < x0) {
            if (whiteOutside)
                memset(d, 255, dstW);
            continue;
        }
        if (whiteOutside) {
            if (x0 > 0)
                memset(d, 255, x0);
            if (x1 + 1 < dstW)
                memset(d + x1 + 1, 255, dstW - x1 - 1);
        }
        int fx = (int)lrintf((u0 + ax * x0) * 65536.0f);
        int fy = (int)lrintf((v0 + ay * x0) * 65536.0f);
        if (n == 1 && upright) {
            // A frame that is not turned takes a whole row of dst from two rows of src: they are
            // mixed once into a row (a plain pass along the memory), and a pixel of dst is then
            // mixed from two neighbours of that row.
            int iy = fy >> 16, wy = (fy >> 8) & 0xFF;
            if (iy < 0) {
                iy = 0;
                wy = 0;
            } else if (iy >= (int)maxY) {
                iy = (int)maxY;
                wy = 0;
            }
            const unsigned char * r0 = src + (size_t)iy * srcStride;
            const unsigned char * row = r0;
            if (wy != 0) {
                const unsigned char * r1 = r0 + srcStride;
                const int fxEnd = fx + stepX * (x1 - x0);
                int s0 = (fx < fxEnd ? fx : fxEnd) >> 16, s1 = ((fx < fxEnd ? fxEnd : fx) >> 16) + 1;
                if (s0 < 0)
                    s0 = 0;
                if (s1 > (int)maxX)
                    s1 = (int)maxX;
                for (int i = s0; i <= s1; i++)
                    line[i] = (unsigned char)(((r0[i] << 8) + (r1[i] - r0[i]) * wy) >> 8);
                row = &line[0];
            }
            for (int x = x0; x <= x1; x++, fx += stepX) {
                const int ix = fx >> 16;
                if ((unsigned)ix < maxX) {
                    const int wx = (fx >> 8) & 0xFF;
                    d[x] = (unsigned char)(((row[ix] << 8) + (row[ix + 1] - row[ix]) * wx) >> 8);
                } else {
                    d[x] = row[ix < 0 ? 0 : (int)maxX];
                }
            }
        } else if (n == 1) {
            for (int x = x0; x <= x1; x++, fx += stepX, fy += stepY) {
                const int ix = fx >> 16, iy = fy >> 16;
                if ((unsigned)ix < maxX && (unsigned)iy < maxY) {
                    const unsigned char * p = src + (size_t)iy * srcStride + ix;
                    const int wx = (fx >> 8) & 0xFF, wy = (fy >> 8) & 0xFF;
                    const int top = (p[0] << 8) + (p[1] - p[0]) * wx;
                    const int bottom = (p[srcStride] << 8) + (p[srcStride + 1] - p[srcStride]) * wx;
                    d[x] = (unsigned char)(((top << 8) + (bottom - top) * wy) >> 16);
                } else {
                    // at the edge: the nearest pixel of the source
                    const int cx = ix < 0 ? 0 : ix > (int)maxX ? (int)maxX : ix;
                    const int cy = iy < 0 ? 0 : iy > (int)maxY ? (int)maxY : iy;
                    d[x] = src[(size_t)cy * srcStride + cx];
                }
            }
        } else {
            for (int x = x0; x <= x1; x++, fx += stepX, fy += stepY) {
                int sum = 0;
                for (int k = 0; k < n * n; k++) {
                    int ix = (fx + offX[k] + 32768) >> 16, iy = (fy + offY[k] + 32768) >> 16;
                    if ((unsigned)ix > maxX)
                        ix = ix < 0 ? 0 : (int)maxX;
                    if ((unsigned)iy > maxY)
                        iy = iy < 0 ? 0 : (int)maxY;
                    sum += src[(size_t)iy * srcStride + ix];
                }
                d[x] = (unsigned char)((sum * recip) >> 16);
            }
        }
    }
}

void pbRotateGray(const unsigned char * src, int srcW, int srcH, int srcStride, float scx, float scy,
                  unsigned char * dst, int dstW, int dstH, int dstStride, float dcx, float dcy, float angle)
{
    const float a = angle * PB_PI / 180.0f;
    const float c = cosf(a), s = sinf(a);
    pbBlitGray(src, srcW, srcH, srcStride, scx - dcx * c - dcy * s, scy + dcx * s - dcy * c, c, -s, s, c,
               dst, dstW, dstH, dstStride, true);
}

// ------------------------------------------------------------------------------------ PDF
static void pbMupdfQuiet(void *, const char *) { }

class PbPdfDoc : public PbPageDoc {
    fz_context * _ctx;
    fz_document * _doc;
    int _count;
    std::vector<PbPageSize> _sizes;     // width < 0: not asked yet
    std::vector<fz_point> _origins;     // the corner of the page box in the coordinates of the page
    /// A page kept: its commands and the large pictures decoded for it.
    struct Slot {
        int page;
        unsigned used;                      // the turn it was asked for last
        fz_display_list * list;
        std::vector<fz_image *> sources;    // pictures of the page ...
        std::vector<fz_image *> decoded;    // ... and their decoded gray copies (NULL: left as it is)
        std::vector<int> reduced;           // a copy holds every 2^n-th pixel of its picture
        Slot() : page(-1), used(0), list(NULL) { }
    };
    // two pages: the one on the screen and the one prepared ahead, so that going back to the
    // first one, or another frame of it, reads and decodes nothing
    Slot _slots[2];
    Slot * _cur;
    unsigned _turn;

    void dropSlot(Slot & slot) {
        if (slot.list)
            fz_drop_display_list(_ctx, slot.list);
        for (size_t i = 0; i < slot.decoded.size(); i++)
            fz_drop_image(_ctx, slot.decoded[i]);
        for (size_t i = 0; i < slot.sources.size(); i++)
            fz_drop_image(_ctx, slot.sources[i]);
        slot.decoded.clear();
        slot.sources.clear();
        slot.reduced.clear();
        slot.list = NULL;
        slot.page = -1;
    }
    /// The commands of the page, kept; _cur is its slot.
    bool loadList(int page) {
        if (page < 0 || page >= _count)
            return false;
        _turn++;
        for (int i = 0; i < 2; i++)
            if (_slots[i].page == page) {
                _cur = &_slots[i];
                _cur->used = _turn;
                return true;
            }
        Slot & slot = _slots[0].used <= _slots[1].used ? _slots[0] : _slots[1];
        dropSlot(slot);
        _cur = NULL;
        const double t0 = pbNowMs();
        fz_page * p = NULL;
        fz_display_list * list = NULL;
        fz_rect box = fz_empty_rect;
        fz_var(p);
        fz_var(list);
        fz_var(box);
        fz_try(_ctx) {
            p = fz_load_page(_ctx, _doc, page);
            box = fz_bound_page(_ctx, p);
            list = fz_new_display_list_from_page(_ctx, p);
        }
        fz_always(_ctx) {
            fz_drop_page(_ctx, p);
        }
        fz_catch(_ctx) {
            return false;
        }
        slot.list = list;
        slot.page = page;
        slot.used = _turn;
        _cur = &slot;
        _sizes[page].width = box.x1 - box.x0;
        _sizes[page].height = box.y1 - box.y0;
        _sizes[page].dpi = 72;
        _origins[page].x = box.x0;
        _origins[page].y = box.y0;
        readMs += pbNowMs() - t0;
        return true;
    }

    /// The device between the commands of a page and the drawing: it gives a large picture as its
    /// decoded copy. The library decodes a picture anew for every zoom and for every part of it
    /// that is seen (50 ms on the PC for 12 megapixels, a second or two on a reader): each frame
    /// of a photographed page, each step of setting a frame. The copy is decoded once, in gray,
    /// at the size the frames need (a half or a quarter of the pixels do for a whole page on a
    /// small screen, and are decoded faster), at most PB_KEPT_SIDE pixels long; every frame is
    /// scaled from it. A frame that needs more pixels than the copy has makes it decoded again.
    ///
    /// The copy is also drawn here, not by the library, when nothing but the picture decides its
    /// pixels: the library reduces a picture with a filter over all its pixels (11 ms on the PC
    /// and 0.66 s on a reader for a frame of a scanned page); pbBlitGray mixes two neighbours of
    /// a copy that is under twice the size needed. For that the device follows the clips: the
    /// picture is drawn here only inside rectangular clips that hold all of it, with no mask,
    /// group or pattern open.
    struct ImageDevice {
        fz_device super;
        PbPdfDoc * owner;
        float boost;    // the frames to come are this many times larger than the one drawn
        fz_matrix toBuffer;                 // page -> pixels of the buffer
        bool upright;                       // ... with no turn but quarter turns
        unsigned char * dst;
        int width, height, stride;
        enum { MAX_CLIPS = 24 };
        int depth;                          // clips, masks, groups and patterns open
        int notRect;                        // of them, those that are not a rectangle
        bool isRect[MAX_CLIPS];
        fz_rect rects[MAX_CLIPS];           // in the coordinates of the page
    };
    static void pushClip(ImageDevice * self, const fz_rect * rect) {
        if (self->depth < ImageDevice::MAX_CLIPS && rect != NULL) {
            self->isRect[self->depth] = true;
            self->rects[self->depth] = *rect;
        } else {
            if (self->depth < ImageDevice::MAX_CLIPS)
                self->isRect[self->depth] = false;
            self->notRect++;
        }
        self->depth++;
    }
    static void popClip(ImageDevice * self) {
        if (self->depth <= 0)
            return;
        self->depth--;
        if (self->depth >= ImageDevice::MAX_CLIPS || !self->isRect[self->depth])
            self->notRect--;
    }
    static void devClipPath(fz_context * ctx, fz_device * dev, const fz_path * path, int evenOdd, fz_matrix ctm,
                            fz_rect scissor) {
        fz_rect bounds;
        pushClip((ImageDevice *)dev, fz_path_is_rect_with_bounds(ctx, path, ctm, &bounds) ? &bounds : NULL);
        fz_clip_path(ctx, dev->passthrough, path, evenOdd, ctm, scissor);
    }
    static void devClipStrokePath(fz_context * ctx, fz_device * dev, const fz_path * path, const fz_stroke_state * stroke,
                                  fz_matrix ctm, fz_rect scissor) {
        pushClip((ImageDevice *)dev, NULL);
        fz_clip_stroke_path(ctx, dev->passthrough, path, stroke, ctm, scissor);
    }
    static void devClipText(fz_context * ctx, fz_device * dev, const fz_text * text, fz_matrix ctm, fz_rect scissor) {
        pushClip((ImageDevice *)dev, NULL);
        fz_clip_text(ctx, dev->passthrough, text, ctm, scissor);
    }
    static void devClipStrokeText(fz_context * ctx, fz_device * dev, const fz_text * text, const fz_stroke_state * stroke,
                                  fz_matrix ctm, fz_rect scissor) {
        pushClip((ImageDevice *)dev, NULL);
        fz_clip_stroke_text(ctx, dev->passthrough, text, stroke, ctm, scissor);
    }
    static void devClipImageMask(fz_context * ctx, fz_device * dev, fz_image * image, fz_matrix ctm, fz_rect scissor) {
        pushClip((ImageDevice *)dev, NULL);
        fz_clip_image_mask(ctx, dev->passthrough, image, ctm, scissor);
    }
    static void devPopClip(fz_context * ctx, fz_device * dev) {
        popClip((ImageDevice *)dev);
        fz_pop_clip(ctx, dev->passthrough);
    }
    // a mask is closed by end_mask and stays a clip until pop_clip
    static void devBeginMask(fz_context * ctx, fz_device * dev, fz_rect area, int luminosity, fz_colorspace * cs,
                             const float * bc, fz_color_params params) {
        pushClip((ImageDevice *)dev, NULL);
        fz_begin_mask(ctx, dev->passthrough, area, luminosity, cs, bc, params);
    }
    static void devBeginGroup(fz_context * ctx, fz_device * dev, fz_rect area, fz_colorspace * cs, int isolated,
                              int knockout, int blendmode, float alpha) {
        pushClip((ImageDevice *)dev, NULL);
        fz_begin_group(ctx, dev->passthrough, area, cs, isolated, knockout, blendmode, alpha);
    }
    static void devEndGroup(fz_context * ctx, fz_device * dev) {
        popClip((ImageDevice *)dev);
        fz_end_group(ctx, dev->passthrough);
    }
    static int devBeginTile(fz_context * ctx, fz_device * dev, fz_rect area, fz_rect view, float xstep, float ystep,
                            fz_matrix ctm, int id, int docId) {
        pushClip((ImageDevice *)dev, NULL);
        return fz_begin_tile_tid(ctx, dev->passthrough, area, view, xstep, ystep, ctm, id, docId);
    }
    static void devEndTile(fz_context * ctx, fz_device * dev) {
        popClip((ImageDevice *)dev);
        fz_end_tile(ctx, dev->passthrough);
    }
    enum { PB_KEPT_SIDE = 3400, PB_KEPT_MIN_PIXELS = 1500000 };
    /// wanted: the width in pixels the picture is needed at.
    fz_image * decodedOf(fz_image * image, float wanted) {
        Slot & slot = *_cur;
        int reduce = 0;
        while (reduce < 3 && (float)(image->w >> (reduce + 1)) >= wanted)
            reduce++;
        while (reduce < 3 && ((image->w > image->h ? image->w : image->h) >> reduce) > PB_KEPT_SIDE)
            reduce++;
        size_t at = slot.sources.size();
        for (size_t i = 0; i < slot.sources.size(); i++)
            if (slot.sources[i] == image) {
                if (slot.decoded[i] == NULL || slot.reduced[i] <= reduce)
                    return slot.decoded[i];
                at = i;     // kept with fewer pixels than this frame needs
                break;
            }
        fz_image * result = NULL;
        fz_pixmap * pix = NULL;
        fz_pixmap * gray = NULL;
        fz_var(result);
        fz_var(pix);
        fz_var(gray);
        if (!image->imagemask && image->mask == NULL && image->colorspace != NULL
                && (long)image->w * image->h >= PB_KEPT_MIN_PIXELS) {
            fz_try(_ctx) {
                if (reduce > 0) {
                    // asked for at this size, the picture is reduced while it is decoded
                    fz_matrix m = fz_scale((float)(image->w >> reduce), (float)(image->h >> reduce));
                    pix = fz_get_pixmap_from_image(_ctx, image, NULL, &m, NULL, NULL);
                } else {
                    pix = fz_get_unscaled_pixmap_from_image(_ctx, image);
                }
                if (fz_pixmap_colorspace(_ctx, pix) == fz_device_gray(_ctx))
                    gray = fz_keep_pixmap(_ctx, pix);
                else
                    gray = fz_convert_pixmap(_ctx, pix, fz_device_gray(_ctx), NULL, NULL, fz_default_color_params, 1);
                result = fz_new_image_from_pixmap(_ctx, gray, NULL);
            }
            fz_always(_ctx) {
                fz_drop_pixmap(_ctx, pix);
                fz_drop_pixmap(_ctx, gray);
            }
            fz_catch(_ctx) {
                result = NULL;
            }
        }
        if (at == slot.sources.size()) {
            slot.sources.push_back(fz_keep_image(_ctx, image));
            slot.decoded.push_back(result);
            slot.reduced.push_back(reduce);
        } else {
            fz_drop_image(_ctx, slot.decoded[at]);
            slot.decoded[at] = result;
            slot.reduced[at] = reduce;
        }
        return result;
    }
    static void fillImage(fz_context * ctx, fz_device * dev, fz_image * image, fz_matrix ctm, float alpha,
                          fz_color_params params) {
        ImageDevice * self = (ImageDevice *)dev;
        // the width of the picture on the screen, in pixels
        fz_image * decoded = self->owner->decodedOf(image, sqrtf(ctm.a * ctm.a + ctm.b * ctm.b) * self->boost);
        // A clip that is a rectangle on the page is one in the buffer only when the frame is not
        // turned: the library then narrows its drawing area and draws into the buffer itself. For
        // any other clip it draws into a layer and lays the layer over the buffer afterwards,
        // which would wipe out what is drawn here.
        if (decoded != NULL && alpha >= 0.999f && self->notRect == 0 && (self->depth == 0 || self->upright)
                && drawCopy(ctx, self, decoded, ctm))
            return;
        fz_fill_image(ctx, dev->passthrough, decoded ? decoded : image, ctm, alpha, params);
    }
    /// The gray copy drawn straight into the buffer; false: the library has to draw it.
    static bool drawCopy(fz_context * ctx, ImageDevice * self, fz_image * decoded, fz_matrix ctm) {
        // every clip holds the whole picture (half a point of slack: the box of a page is often
        // written with rounded numbers)
        const fz_rect box = fz_transform_rect(fz_unit_rect, ctm);
        for (int i = 0; i < self->depth; i++) {
            const fz_rect & r = self->rects[i];
            if (box.x0 < r.x0 - 0.5f || box.y0 < r.y0 - 0.5f || box.x1 > r.x1 + 0.5f || box.y1 > r.y1 + 0.5f)
                return false;
        }
        bool drawn = false;
        fz_pixmap * gray = NULL;
        fz_var(gray);
        fz_try(ctx) {
            gray = fz_get_pixmap_from_image(ctx, decoded, NULL, NULL, NULL, NULL);
            const int w = fz_pixmap_width(ctx, gray), h = fz_pixmap_height(ctx, gray);
            if (fz_pixmap_components(ctx, gray) == 1 && w > 0 && h > 0 && w <= 32000 && h <= 32000) {
                // pixel of the copy -> unit square -> page -> buffer, and back
                fz_matrix m = fz_concat(fz_concat(fz_scale(1.0f / w, 1.0f / h), ctm), self->toBuffer);
                const float det = m.a * m.d - m.b * m.c;
                if (fabsf(det) > 1e-9f) {
                    const fz_matrix inv = fz_invert_matrix(m);
                    // the middle of a pixel of the buffer, to the pixels of the copy counted from the middle of its first one
                    pbBlitGray(fz_pixmap_samples(ctx, gray), w, h, fz_pixmap_stride(ctx, gray),
                               0.5f * inv.a + 0.5f * inv.c + inv.e - 0.5f, 0.5f * inv.b + 0.5f * inv.d + inv.f - 0.5f,
                               inv.a, inv.b, inv.c, inv.d, self->dst, self->width, self->height, self->stride, false);
                    drawn = true;
                }
            }
        }
        fz_always(ctx) {
            fz_drop_pixmap(ctx, gray);
        }
        fz_catch(ctx) {
            drawn = false;
        }
        return drawn;
    }

    void walk(fz_outline * item, int level, std::vector<PbPageTocEntry> & entries) {
        for (; item; item = item->next) {
            PbPageTocEntry entry;
            entry.level = level;
            entry.page = -1;
            fz_try(_ctx) {
                entry.page = fz_page_number_from_location(_ctx, _doc, item->page);
            }
            fz_catch(_ctx) {
                entry.page = -1;
            }
            if (entry.page >= _count)
                entry.page = -1;
            entry.title = item->title ? item->title : "";
            entries.push_back(entry);
            if (item->down)
                walk(item->down, level + 1, entries);
        }
    }

public:
    PbPdfDoc() : _ctx(NULL), _doc(NULL), _count(0), _cur(NULL), _turn(0) { }
    virtual ~PbPdfDoc() {
        if (_ctx) {
            dropSlot(_slots[0]);
            dropSlot(_slots[1]);
            fz_drop_document(_ctx, _doc);
            fz_drop_context(_ctx);
        }
    }
    bool init(const char * path, std::string * error) {
        // the store holds decoded pictures: one photographed page of 12 megapixels is 12 MB in gray
        _ctx = fz_new_context(NULL, NULL, 48 << 20);
        if (!_ctx) {
            if (error)
                *error = "no memory";
            return false;
        }
        fz_set_warning_callback(_ctx, pbMupdfQuiet, NULL);
        fz_set_error_callback(_ctx, pbMupdfQuiet, NULL);
        // no colour management: the screen is gray, and matching colours by their profiles costs
        // time at every picture
        if (!getenv("PB_PAGEDOC_ICC"))
            fz_disable_icc(_ctx);
        fz_try(_ctx) {
            fz_register_document_handlers(_ctx);
            _doc = fz_open_document(_ctx, path);
            if (fz_needs_password(_ctx, _doc))
                fz_throw(_ctx, FZ_ERROR_ARGUMENT, "the file is protected by a password");
            _count = fz_count_pages(_ctx, _doc);
        }
        fz_catch(_ctx) {
            if (error)
                *error = fz_caught_message(_ctx);
            return false;
        }
        PbPageSize unknown;
        unknown.width = -1;
        _sizes.assign(_count, unknown);
        fz_point zero = { 0, 0 };
        _origins.assign(_count, zero);
        return _count > 0;
    }
    virtual PbPageDocKind kind() const { return PB_PAGEDOC_PDF; }
    virtual int pageCount() { return _count; }
    virtual bool pageSize(int page, PbPageSize & size) {
        if (page < 0 || page >= _count)
            return false;
        if (_sizes[page].width < 0) {
            fz_page * p = NULL;
            fz_rect box = fz_empty_rect;
            fz_var(p);
            fz_var(box);
            fz_try(_ctx) {
                p = fz_load_page(_ctx, _doc, page);
                box = fz_bound_page(_ctx, p);
            }
            fz_always(_ctx) {
                fz_drop_page(_ctx, p);
            }
            fz_catch(_ctx) {
                return false;
            }
            _sizes[page].width = box.x1 - box.x0;
            _sizes[page].height = box.y1 - box.y0;
            _sizes[page].dpi = 72;
            _origins[page].x = box.x0;
            _origins[page].y = box.y0;
        }
        size = _sizes[page];
        return size.width > 0 && size.height > 0;
    }
    virtual bool render(int page, const PbPageView & view, unsigned char * dst, int width, int height, int stride) {
        if (!loadList(page)) {
            for (int y = 0; y < height; y++)
                memset(dst + (size_t)y * stride, 255, width);
            return false;
        }
        // page point (cx, cy) -> zoom -> turn -> the middle of the buffer
        fz_matrix m = fz_translate(-_origins[page].x - view.cx, -_origins[page].y - view.cy);
        m = fz_concat(m, fz_scale(view.zoom, view.zoom));
        if (view.angle != 0)
            m = fz_concat(m, fz_rotate(view.angle));
        m = fz_concat(m, fz_translate(width * 0.5f, height * 0.5f));
        fz_pixmap * pix = NULL;
        fz_device * dev = NULL;
        bool ok = true;
        fz_var(pix);
        fz_var(dev);
        fz_try(_ctx) {
            // the pixmap is the buffer of the caller: the page is drawn where it will be shown from
            pix = fz_new_pixmap_with_data(_ctx, fz_device_gray(_ctx), width, height, NULL, 0, stride, dst);
            fz_clear_pixmap_with_value(_ctx, pix, 255);
            // dropping the device of the pictures drops the draw device it passes to
            // (the macro of the library for this names a variable "ctx"; the function is called itself)
            ImageDevice * images = (ImageDevice *)fz_new_passthrough_device_of_size(
                _ctx, fz_new_draw_device(_ctx, m, pix), sizeof(ImageDevice));
            images->owner = this;
            images->boost = _detailZoom > view.zoom ? _detailZoom / view.zoom : 1.0f;
            images->toBuffer = m;
            images->upright = (fabsf(m.b) < 1e-6f && fabsf(m.c) < 1e-6f) || (fabsf(m.a) < 1e-6f && fabsf(m.d) < 1e-6f);
            images->dst = dst;
            images->width = width;
            images->height = height;
            images->stride = stride;
            images->depth = 0;
            images->notRect = 0;
            images->super.fill_image = fillImage;
            images->super.clip_path = devClipPath;
            images->super.clip_stroke_path = devClipStrokePath;
            images->super.clip_text = devClipText;
            images->super.clip_stroke_text = devClipStrokeText;
            images->super.clip_image_mask = devClipImageMask;
            images->super.pop_clip = devPopClip;
            images->super.begin_mask = devBeginMask;
            images->super.begin_group = devBeginGroup;
            images->super.end_group = devEndGroup;
            images->super.begin_tile = devBeginTile;
            images->super.end_tile = devEndTile;
            dev = &images->super;
            fz_run_display_list(_ctx, _cur->list, dev, fz_identity, fz_infinite_rect, NULL);
            fz_close_device(_ctx, dev);
        }
        fz_always(_ctx) {
            fz_drop_device(_ctx, dev);
            fz_drop_pixmap(_ctx, pix);
        }
        fz_catch(_ctx) {
            ok = false;
        }
        return ok;
    }
    virtual void contents(std::vector<PbPageTocEntry> & entries) {
        entries.clear();
        fz_outline * outline = NULL;
        fz_var(outline);
        fz_try(_ctx) {
            outline = fz_load_outline(_ctx, _doc);
        }
        fz_catch(_ctx) {
            outline = NULL;
        }
        if (outline) {
            walk(outline, 0, entries);
            fz_drop_outline(_ctx, outline);
        }
    }
    virtual bool hasText(int page) {
        if (!loadList(page))
            return false;
        bool found = false;
        fz_stext_page * text = NULL;
        fz_var(text);
        fz_try(_ctx) {
            fz_stext_options options;
            memset(&options, 0, sizeof(options));
            text = fz_new_stext_page_from_display_list(_ctx, _cur->list, &options);
            for (fz_stext_block * b = text->first_block; b && !found; b = b->next)
                if (b->type == FZ_STEXT_BLOCK_TEXT && b->u.t.first_line && b->u.t.first_line->first_char)
                    found = true;
        }
        fz_always(_ctx) {
            fz_drop_stext_page(_ctx, text);
        }
        fz_catch(_ctx) {
            found = false;
        }
        return found;
    }
};

// ------------------------------------------------------------------------------------ DjVu
class PbDjvuDoc : public PbPageDoc {
    ddjvu_context_t * _ctx;
    ddjvu_document_t * _doc;
    ddjvu_format_t * _format;
    int _count;
    std::vector<PbPageSize> _sizes;     // width < 0: not asked yet
    int _pageNo;
    ddjvu_page_t * _page;               // the decoded page, kept until another page is asked for
    // The page that will be asked for next (hintNext): its decoding runs in a thread of the
    // library, on the second core, while the page before it is drawn and read.
    int _deckNo;
    ddjvu_page_t * _deck;
    // The picture of the page: the whole page in gray, every _picRed-th pixel, turned by
    // _picQuarters quarter turns. The decoder draws it once, at a whole reduction (its fast way:
    // any other size goes through its scaler, 10 ms on the PC against 6, and a compound page takes
    // 37 ms for every frame); the frames, the small picture of the content box and the steps of
    // setting a frame are taken from it by pbBlitGray.
    std::vector<unsigned char> _pic;
    // A page with more pixels than PB_PIC_PIXELS at the reduction a frame needs (a frame zoomed
    // in on a page of 600 dpi) is kept as a part: the frame with a margin around it.
    int _picPage, _picRed, _picQuarters, _picW, _picH;
    int _picX, _picY;                   // the corner of the part, in pixels of the reduced page
    enum { PB_PIC_PIXELS = 13000000 };
    // The zoom of the last frame drawn for the reader. The picture made for the content box of
    // the next page is given the pixels of that zoom: the pages of a book have the same margins,
    // so the frame of the page is then taken from the same picture. Without it a page with wide
    // margins (a photographed book) had its picture made twice: for the box, then larger for
    // the frame.
    float _lastZoom;

    void pump(bool wait) {
        if (wait)
            ddjvu_message_wait(_ctx);
        while (ddjvu_message_peek(_ctx))
            ddjvu_message_pop(_ctx);
    }
    bool loadPage(int page) {
        if (page == _pageNo)
            return true;
        if (page < 0 || page >= _count)
            return false;
        if (_page)
            ddjvu_page_release(_page);
        _pageNo = -1;
        const double t0 = pbNowMs();
        if (page == _deckNo && _deck != NULL) {
            // being decoded already, or decoded
            _page = _deck;
            _deck = NULL;
            _deckNo = -1;
        } else {
            _page = ddjvu_page_create_by_pageno(_doc, page);
        }
        if (!_page)
            return false;
        while (!ddjvu_page_decoding_done(_page))
            pump(true);
        readMs += pbNowMs() - t0;
        if (ddjvu_page_decoding_error(_page)) {
            ddjvu_page_release(_page);
            _page = NULL;
            return false;
        }
        _pageNo = page;
        return true;
    }
    /// The picture of the decoded page, turned by quarters (clockwise), with at least `zoom`
    /// pixels for a unit of the page when the page has them. x0..y1: the part of the turned page
    /// the frame needs, in units of the page.
    void ensurePicture(const PbPageSize & size, float zoom, int quarters, float x0, float y0, float x1, float y1) {
        const int pw = (int)((quarters & 1) ? size.height : size.width);
        const int ph = (int)((quarters & 1) ? size.width : size.height);
        // the largest whole reduction that still gives the pixels asked for; a picture enlarged
        // by up to an eighth is taken rather than one with four times the pixels
        int red = zoom > 0 ? (int)floorf(1.125f / zoom + 0.001f) : 12;
        if (red < 1)
            red = 1;
        if (red > 12)
            red = 12;
        int fw, fh, rx, ry, rw, rh;
        for (;; red++) {
            // the reduced page as the decoder makes it without its scaler
            fw = (pw + red - 1) / red;
            fh = (ph + red - 1) / red;
            rx = 0; ry = 0; rw = fw; rh = fh;
            if ((long)fw * fh <= PB_PIC_PIXELS || red >= 12)
                break;
            // too many pixels to keep the page whole: the part the frame needs ...
            int nx0 = (int)floorf(x0 / red) - 2, ny0 = (int)floorf(y0 / red) - 2;
            int nx1 = (int)ceilf(x1 / red) + 2, ny1 = (int)ceilf(y1 / red) + 2;
            if (nx0 < 0) nx0 = 0;
            if (ny0 < 0) ny0 = 0;
            if (nx1 > fw) nx1 = fw;
            if (ny1 > fh) ny1 = fh;
            if (nx1 <= nx0 || ny1 <= ny0)
                continue;
            // ... with a quarter of its size around it for the frames beside this one, when that fits
            const int mx = (nx1 - nx0) / 4, my = (ny1 - ny0) / 4;
            rx = nx0 - mx > 0 ? nx0 - mx : 0;
            ry = ny0 - my > 0 ? ny0 - my : 0;
            rw = (nx1 + mx < fw ? nx1 + mx : fw) - rx;
            rh = (ny1 + my < fh ? ny1 + my : fh) - ry;
            if ((long)rw * rh <= PB_PIC_PIXELS)
                break;
            rx = nx0; ry = ny0; rw = nx1 - nx0; rh = ny1 - ny0;
            if ((long)rw * rh <= PB_PIC_PIXELS)
                break;
            // a frame that large is far from the pixels of the page: a coarser picture does
        }
        if (rw < 1 || rh < 1) {
            rx = 0; ry = 0; rw = fw > 0 ? fw : 1; rh = fh > 0 ? fh : 1;
        }
        if (_picPage == _pageNo && _picQuarters == quarters && _picRed <= red) {
            // kept with the pixels a new picture would have: does it hold the part the frame needs?
            // (in units of the page)
            const float kx0 = (float)_picX * _picRed, ky0 = (float)_picY * _picRed;
            const float kx1 = (float)(_picX + _picW) * _picRed, ky1 = (float)(_picY + _picH) * _picRed;
            if ((x0 < 0 ? 0 : x0) >= kx0 && (y0 < 0 ? 0 : y0) >= ky0
                    && (x1 > pw ? (float)pw : x1) <= kx1 && (y1 > ph ? (float)ph : y1) <= ky1)
                return;
        }
        _picPage = _pageNo;
        _picQuarters = quarters;
        _picRed = red;
        _picX = rx;
        _picY = ry;
        _picW = rw;
        _picH = rh;
        _pic.assign((size_t)rw * rh, 255);
        // DjVu counts quarter turns counter-clockwise
        const int initial = (int)ddjvu_page_get_initial_rotation(_page);
        ddjvu_page_set_rotation(_page, (ddjvu_page_rotation_t)((initial + 4 - quarters) & 3));
        ddjvu_rect_t whole, part;
        whole.x = 0;
        whole.y = 0;
        whole.w = (unsigned)fw;
        whole.h = (unsigned)fh;
        part.x = rx;
        part.y = ry;
        part.w = (unsigned)rw;
        part.h = (unsigned)rh;
        ddjvu_page_render(_page, DDJVU_RENDER_COLOR, &whole, &part, _format, rw, (char *)&_pic[0]);
    }
    void walk(miniexp_t list, int level, std::vector<PbPageTocEntry> & entries) {
        for (; miniexp_consp(list); list = miniexp_cdr(list)) {
            miniexp_t item = miniexp_car(list);
            if (!miniexp_consp(item) || !miniexp_stringp(miniexp_car(item)))
                continue;
            PbPageTocEntry entry;
            entry.level = level;
            entry.page = -1;
            entry.title = miniexp_to_str(miniexp_car(item));
            miniexp_t rest = miniexp_cdr(item);
            if (miniexp_consp(rest) && miniexp_stringp(miniexp_car(rest))) {
                const char * url = miniexp_to_str(miniexp_car(rest));
                if (url && url[0] == '#') {
                    char * end = NULL;
                    long number = strtol(url + 1, &end, 10);
                    if (end && *end == 0 && number >= 1 && number <= _count)
                        entry.page = (int)number - 1;
                    else
                        entry.page = ddjvu_document_search_pageno(_doc, url + 1);
                }
                rest = miniexp_cdr(rest);
            }
            entries.push_back(entry);
            walk(rest, level + 1, entries);
        }
    }

public:
    PbDjvuDoc() : _ctx(NULL), _doc(NULL), _format(NULL), _count(0), _pageNo(-1), _page(NULL), _deckNo(-1), _deck(NULL),
                  _picPage(-1), _picRed(0), _picQuarters(0), _picW(0), _picH(0), _picX(0), _picY(0), _lastZoom(0) { }
    virtual ~PbDjvuDoc() {
        if (_deck)
            ddjvu_page_release(_deck);
        if (_page)
            ddjvu_page_release(_page);
        if (_format)
            ddjvu_format_release(_format);
        if (_doc)
            ddjvu_document_release(_doc);
        if (_ctx)
            ddjvu_context_release(_ctx);
    }
    bool init(const char * path, std::string * error) {
        _ctx = ddjvu_context_create("cr3");
        _doc = _ctx ? ddjvu_document_create_by_filename_utf8(_ctx, path, TRUE) : NULL;
        if (!_doc) {
            if (error)
                *error = "the file cannot be opened";
            return false;
        }
        while (!ddjvu_document_decoding_done(_doc))
            pump(true);
        if (ddjvu_document_decoding_error(_doc)) {
            if (error)
                *error = "the file is not a DjVu document or is damaged";
            return false;
        }
        _count = ddjvu_document_get_pagenum(_doc);
        _format = ddjvu_format_create(DDJVU_FORMAT_GREY8, 0, NULL);
        ddjvu_format_set_row_order(_format, 1);
        ddjvu_format_set_y_direction(_format, 1);
        PbPageSize unknown;
        unknown.width = -1;
        _sizes.assign(_count, unknown);
        return _count > 0;
    }
    virtual PbPageDocKind kind() const { return PB_PAGEDOC_DJVU; }
    virtual int pageCount() { return _count; }
    virtual bool pageSize(int page, PbPageSize & size) {
        if (page < 0 || page >= _count)
            return false;
        if (_sizes[page].width < 0) {
            // the header of the page is enough: the page is not decoded for its size
            ddjvu_pageinfo_t info;
            ddjvu_status_t status;
            while ((status = ddjvu_document_get_pageinfo(_doc, page, &info)) < DDJVU_JOB_OK)
                pump(true);
            if (status >= DDJVU_JOB_FAILED)
                return false;
            bool turned = (info.rotation & 1) != 0;
            _sizes[page].width = (float)(turned ? info.height : info.width);
            _sizes[page].height = (float)(turned ? info.width : info.height);
            _sizes[page].dpi = info.dpi > 0 ? (float)info.dpi : 300.0f;
        }
        size = _sizes[page];
        return size.width > 0 && size.height > 0;
    }
    virtual bool render(int page, const PbPageView & view, unsigned char * dst, int width, int height, int stride) {
        PbPageSize size;
        if (!pageSize(page, size) || !loadPage(page)) {
            for (int y = 0; y < height; y++)
                memset(dst + (size_t)y * stride, 255, width);
            return false;
        }
        // a turn is quarter turns, which the decoder makes itself, and the rest, a small angle
        int quarters = (int)lrintf(view.angle / 90.0f);
        const float rest = view.angle - quarters * 90.0f;
        quarters = ((quarters % 4) + 4) % 4;
        if (view.zoom <= 0) {
            for (int y = 0; y < height; y++)
                memset(dst + (size_t)y * stride, 255, width);
            return true;
        }
        // the middle of the frame on the page turned by quarters
        float x = view.cx, y = view.cy;
        switch (quarters) {
        case 1: x = size.height - view.cy; y = view.cx; break;
        case 2: x = size.width - view.cx; y = size.height - view.cy; break;
        case 3: x = view.cy; y = size.width - view.cx; break;
        default: break;
        }
        const float a = rest * PB_PI / 180.0f;
        const float ca = cosf(a), sa = sinf(a);
        // what the buffer covers of the page: a rectangle around the middle, larger when turned
        const float hw = (width * fabsf(ca) + height * fabsf(sa)) * 0.5f / view.zoom;
        const float hh = (width * fabsf(sa) + height * fabsf(ca)) * 0.5f / view.zoom;
        float pixels = view.zoom;
        if (_detailLow > 0) {
            // the small picture of the content box: the frame comes next
            if (_detailLow > pixels)
                pixels = _detailLow;
            if (_lastZoom > pixels && _lastZoom < _detailLow * 4)
                pixels = _lastZoom;
        } else {
            _lastZoom = view.zoom;
        }
        ensurePicture(size, pixels, quarters, x - hw, y - hh, x + hw, y + hh);
        // a pixel of the buffer -> the page (back by the small angle, by the zoom) -> the picture
        const float k = 1.0f / (view.zoom * _picRed);
        const float c = ca * k, s = sa * k;
        const float dx = 0.5f - width * 0.5f, dy = 0.5f - height * 0.5f;
        pbBlitGray(&_pic[0], _picW, _picH, _picW,
                   x / _picRed - 0.5f - _picX + dx * c + dy * s, y / _picRed - 0.5f - _picY - dx * s + dy * c,
                   c, -s, s, c, dst, width, height, stride, true);
        return true;
    }
    virtual void hintNext(int page) {
        if (page < 0 || page >= _count || page == _pageNo || page == _deckNo)
            return;
        if (_deck)
            ddjvu_page_release(_deck);
        // the decoding starts in a thread of the library; nobody waits for it here
        _deck = ddjvu_page_create_by_pageno(_doc, page);
        _deckNo = _deck ? page : -1;
        pump(false);
    }
    virtual void contents(std::vector<PbPageTocEntry> & entries) {
        entries.clear();
        miniexp_t outline;
        while ((outline = ddjvu_document_get_outline(_doc)) == miniexp_dummy)
            pump(true);
        if (miniexp_consp(outline) && miniexp_symbolp(miniexp_car(outline)))
            walk(miniexp_cdr(outline), 0, entries);
        if (outline != miniexp_nil)
            ddjvu_miniexp_release(_doc, outline);
    }
    virtual bool hasText(int page) {
        if (page < 0 || page >= _count)
            return false;
        miniexp_t text;
        while ((text = ddjvu_document_get_pagetext(_doc, page, "page")) == miniexp_dummy)
            pump(true);
        bool found = miniexp_consp(text);
        if (found)
            ddjvu_miniexp_release(_doc, text);
        return found;
    }
};

// ------------------------------------------------------------------------------------ common
PbPageDocKind PbPageDoc::kindOfFile(const char * path)
{
    FILE * f = fopen(path, "rb");
    if (!f)
        return PB_PAGEDOC_NONE;
    char head[1024];
    size_t got = fread(head, 1, sizeof(head), f);
    fclose(f);
    if (got >= 8 && memcmp(head, "AT&TFORM", 8) == 0)
        return PB_PAGEDOC_DJVU;
    // the header of a PDF may stand after a few bytes of something else
    for (size_t i = 0; i + 5 <= got; i++)
        if (head[i] == '%' && memcmp(head + i, "%PDF-", 5) == 0)
            return PB_PAGEDOC_PDF;
    return PB_PAGEDOC_NONE;
}

PbPageDoc * PbPageDoc::open(const char * path, std::string * error)
{
    switch (kindOfFile(path)) {
    case PB_PAGEDOC_PDF: {
        PbPdfDoc * doc = new PbPdfDoc();
        if (doc->init(path, error))
            return doc;
        delete doc;
        return NULL;
    }
    case PB_PAGEDOC_DJVU: {
        PbDjvuDoc * doc = new PbDjvuDoc();
        if (doc->init(path, error))
            return doc;
        delete doc;
        return NULL;
    }
    default:
        if (error)
            *error = "not a PDF or DjVu file";
        return NULL;
    }
}

PbPageView PbPageDoc::viewOfBox(const PbPageBox & box, float angle, int width, int height)
{
    PbPageView view;
    view.cx = (box.x0 + box.x1) * 0.5f;
    view.cy = (box.y0 + box.y1) * 0.5f;
    view.angle = angle;
    const float a = angle * PB_PI / 180.0f;
    const float bw = box.x1 - box.x0, bh = box.y1 - box.y0;
    const float w = fabsf(bw * cosf(a)) + fabsf(bh * sinf(a));
    const float h = fabsf(bw * sinf(a)) + fabsf(bh * cosf(a));
    const float zx = w > 0 ? width / w : 1, zy = h > 0 ? height / h : 1;
    view.zoom = zx < zy ? zx : zy;
    return view;
}

// a frame of "by frames" shares this part of its size with the frame before it
static const float PB_FRAME_OVERLAP = 0.08f;

/// The positions of the frames along one axis of the turned page, from lo to hi; span is the
/// size of a frame. many: the frames of "by frames", else the first frame alone.
static void pbFramePositions(float lo, float hi, float span, bool many, bool fromOwn, float first,
                             std::vector<float> & out)
{
    out.clear();
    const float step = span * (1.0f - PB_FRAME_OVERLAP);
    if (fromOwn) {
        // from the frame of the reader on to the end of the content
        float p = first;
        out.push_back(p);
        while (many && out.size() < 64) {
            // what is left after this frame; a sliver is not worth a frame
            if (hi - (p + span * 0.5f) <= span * 0.05f)
                break;
            float next = p + step;
            if (next + span * 0.5f > hi)
                next = hi - span * 0.5f;    // the last frame ends where the content ends
            if (next <= p + span * 0.05f)
                break;
            p = next;
            out.push_back(p);
        }
        return;
    }
    const float length = hi - lo;
    // fits, or all but a sliver at both edges (the air around the content): one frame, centred;
    // a second frame that differs by a few rows would be a page turn for nothing
    if (length <= span * 1.05f) {
        out.push_back((lo + hi) * 0.5f);
        return;
    }
    if (!many) {
        out.push_back(lo + span * 0.5f);
        return;
    }
    int n = (int)ceilf((length - span * PB_FRAME_OVERLAP) / step);
    if (n < 2)
        n = 2;
    if (n > 64)
        n = 64;
    // evenly, the last frame ends where the content ends
    for (int i = 0; i < n; i++)
        out.push_back(lo + span * 0.5f + (length - span) * i / (n - 1));
}

// The tilt looked for by the scale "automatic", degrees to both sides: a book photographed by hand
// was reported with pages tilted by 10 to 15.
static const float PB_LEVEL_DEGREES = 15.0f;

void PbPageDoc::framesOf(int page, int scale, int leafing, float angle, const PbPageOwnFrame & own,
                         int width, int height, std::vector<PbPageView> & frames)
{
    frames.clear();
    const float W = (float)width, H = (float)height;
    PbPageSize size;
    if (!pageSize(page, size) || W < 1 || H < 1) {
        frames.push_back(PbPageView());
        return;
    }
    PbPageBox box(0, 0, size.width, size.height);
    // PB_PAGE_SCALE_AUTO: the page levelled, which gives the angle in place of the one of the caller
    const KeptLevel * level = NULL;
    // The content box is found on a small picture of the page. A page that is a photograph would
    // be decoded small for it and then again for its frame: the zoom the frame will have at
    // least (the whole page in the window; the content is larger than that) is told beforehand.
    const float fitW = W / size.width, fitH = H / size.height;
    _detailZoom = 0;
    _detailLow = 0;
    if (scale != PB_PAGE_SCALE_PAGE && scale != PB_PAGE_SCALE_WIDTH) {
        const float fit = fitW < fitH ? fitW : fitH;
        // margins of a fifth of the page are usual: the picture made for the content box then
        // serves the frame of the content too
        _detailLow = fit * 1.25f;
        _detailZoom = fit * 1.6f;
        if (scale == PB_PAGE_SCALE_OWN && own.set && own.zoom > 0) {
            _detailLow = own.zoom;
            if (own.zoom > _detailZoom)
                _detailZoom = own.zoom;
        }
        if (scale == PB_PAGE_SCALE_AUTO) {
            // every page by itself: its own angle, and its content as it lies turned by it
            PbPageView whole;
            levelView(page, PB_LEVEL_DEGREES, width, height, whole);
            for (size_t i = 0; i < _levels.size(); i++)
                if (_levels[i].page == page && _levels[i].maxDegrees == PB_LEVEL_DEGREES) {
                    level = &_levels[i];
                    angle = level->angle;
                }
        } else {
            contentBox(page, box);
        }
        _detailZoom = 0;
        _detailLow = 0;
    }
    // the turned page: q = R(angle) * p; a frame is an upright rectangle there
    const float a = angle * PB_PI / 180.0f;
    const float c = cosf(a), s = sinf(a);
    float qx0 = 0, qx1 = 0, qy0 = 0, qy1 = 0;
    if (level != NULL) {
        // the content of the levelled page is kept as it lies on the turned page
        const float qcx = level->cx * c - level->cy * s, qcy = level->cx * s + level->cy * c;
        qx0 = qcx - level->w * 0.5f;
        qx1 = qcx + level->w * 0.5f;
        qy0 = qcy - level->h * 0.5f;
        qy1 = qcy + level->h * 0.5f;
    } else {
        const float xs[4] = { box.x0, box.x1, box.x0, box.x1 };
        const float ys[4] = { box.y0, box.y0, box.y1, box.y1 };
        for (int i = 0; i < 4; i++) {
            const float qx = xs[i] * c - ys[i] * s, qy = xs[i] * s + ys[i] * c;
            if (i == 0 || qx < qx0) qx0 = qx;
            if (i == 0 || qx > qx1) qx1 = qx;
            if (i == 0 || qy < qy0) qy0 = qy;
            if (i == 0 || qy > qy1) qy1 = qy;
        }
    }
    const float bw = qx1 - qx0 > 1 ? qx1 - qx0 : 1, bh = qy1 - qy0 > 1 ? qy1 - qy0 : 1;
    const bool fromOwn = scale == PB_PAGE_SCALE_OWN && own.set && own.zoom > 0;
    const bool many = leafing == PB_PAGE_LEAF_FRAMES;
    float zoom;
    if (fromOwn)
        zoom = own.zoom;
    else if (scale == PB_PAGE_SCALE_WIDTH || scale == PB_PAGE_SCALE_CONTENT_WIDTH
             || (scale == PB_PAGE_SCALE_AUTO && many))
        // (the levelled content read by frames is as wide as the screen, as "content width" is)
        zoom = W / bw;
    else
        zoom = W / bw < H / bh ? W / bw : H / bh;
    std::vector<float> px, py;
    pbFramePositions(qx0, qx1, W / zoom, many, fromOwn, own.cx * c - own.cy * s, px);
    pbFramePositions(qy0, qy1, H / zoom, many, fromOwn, own.cx * s + own.cy * c, py);
    for (size_t j = 0; j < py.size(); j++)
        for (size_t i = 0; i < px.size(); i++) {
            PbPageView view;
            view.cx = px[i] * c + py[j] * s;
            view.cy = -px[i] * s + py[j] * c;
            view.zoom = zoom;
            view.angle = angle;
            frames.push_back(view);
        }
}

/// The level that parts the dark pixels from the light ones (Otsu) of a histogram.
static int pbOtsu(const int * hist, int total)
{
    double sum = 0;
    for (int i = 0; i < 256; i++)
        sum += (double)i * hist[i];
    double sumB = 0, best = -1;
    int wB = 0, level = 128;
    for (int i = 0; i < 256; i++) {
        wB += hist[i];
        if (wB == 0)
            continue;
        int wF = total - wB;
        if (wF == 0)
            break;
        sumB += (double)i * hist[i];
        double mB = sumB / wB, mF = (sum - sumB) / wF;
        double between = (double)wB * wF * (mB - mF) * (mB - mF);
        if (between > best) {
            best = between;
            level = i;
        }
    }
    return level;
}

/// The content of a small gray picture of a page (W x H, rows of W bytes): x0..x1, y0..y1 in its
/// pixels, with a little air around. A page photographed on a dark table: the paper is found
/// first. level: the level between the paper and its text. False when nothing is found.
static bool pbContentOfPicture(const unsigned char * small, int W, int H, int & x0, int & y0, int & x1, int & y1,
                               int & level)
{
    const size_t count = (size_t)W * H;
    int hist[256];
    memset(hist, 0, sizeof(hist));
    for (size_t i = 0; i < count; i++)
        hist[small[i]]++;
    level = pbOtsu(hist, (int)count);

    // a page photographed on a dark table: the rim of the picture is dark. The paper is then
    // the rows and the columns that are light for the most part.
    int px0 = 0, py0 = 0, px1 = W, py1 = H;
    const int rim = W / 25 > 2 ? W / 25 : 2;
    int rimDark = 0, rimAll = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (x < rim || x >= W - rim || y < rim || y >= H - rim) {
                rimAll++;
                if (small[(size_t)y * W + x] < level)
                    rimDark++;
            }
    if (rimDark * 10 > rimAll * 6) {
        std::vector<int> colLight(W, 0), rowLight(H, 0);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (small[(size_t)y * W + x] >= level) {
                    colLight[x]++;
                    rowLight[y]++;
                }
        while (px0 < W && colLight[px0] * 2 < H) px0++;
        while (px1 > px0 && colLight[px1 - 1] * 2 < H) px1--;
        while (py0 < H && rowLight[py0] * 2 < W) py0++;
        while (py1 > py0 && rowLight[py1 - 1] * 2 < W) py1--;
        // the edge of the paper and its shadow stay outside
        const int inX = (px1 - px0) / 30 + 1, inY = (py1 - py0) / 30 + 1;
        px0 += inX; px1 -= inX; py0 += inY; py1 -= inY;
        if (px1 - px0 > 16 && py1 - py0 > 16) {
            // the level between the paper and its text, by the paper alone
            memset(hist, 0, sizeof(hist));
            for (int y = py0; y < py1; y++)
                for (int x = px0; x < px1; x++)
                    hist[small[(size_t)y * W + x]]++;
            level = pbOtsu(hist, (px1 - px0) * (py1 - py0));
        } else {
            px0 = 0; py0 = 0; px1 = W; py1 = H;
        }
    }
    // the content: the rows and the columns of the paper with enough dark pixels; a speck is not content
    const int pw = px1 - px0, ph = py1 - py0;
    std::vector<int> colDark(W, 0), rowDark(H, 0);
    for (int y = py0; y < py1; y++)
        for (int x = px0; x < px1; x++)
            if (small[(size_t)y * W + x] < level) {
                colDark[x]++;
                rowDark[y]++;
            }
    const int needCol = ph / 120 + 2, needRow = pw / 120 + 2;
    x0 = px0; x1 = px1; y0 = py0; y1 = py1;
    while (x0 < x1 && colDark[x0] < needCol) x0++;
    while (x1 > x0 && colDark[x1 - 1] < needCol) x1--;
    while (y0 < y1 && rowDark[y0] < needRow) y0++;
    while (y1 > y0 && rowDark[y1 - 1] < needRow) y1--;
    if (x1 - x0 <= 8 || y1 - y0 <= 8)
        return false;
    // a little air around the content
    const int air = W / 64;
    x0 = x0 - air > px0 ? x0 - air : px0;
    y0 = y0 - air > py0 ? y0 - air : py0;
    x1 = x1 + air < px1 ? x1 + air : px1;
    y1 = y1 + air < py1 ? y1 + air : py1;
    return true;
}

bool PbPageDoc::contentBox(int page, PbPageBox & box)
{
    for (size_t i = 0; i < _boxes.size(); i++)
        if (_boxes[i].page == page) {
            box = _boxes[i].box;
            return _boxes[i].found;
        }
    PbPageSize size;
    if (!pageSize(page, size))
        return false;
    const double t0 = pbNowMs(), read0 = readMs;
    KeptBox kept;
    kept.page = page;
    kept.found = false;
    kept.box = PbPageBox(0, 0, size.width, size.height);

    // a small picture of the whole page is enough to see where its content is
    const int W = 256;
    int H = (int)lrintf(size.height * W / size.width);
    if (H < 32)
        H = 32;
    if (H > 1024)
        H = 1024;
    std::vector<unsigned char> small((size_t)W * H);
    PbPageView view;
    view.cx = size.width * 0.5f;
    view.cy = size.height * 0.5f;
    view.zoom = W / size.width;
    const float zoomY = H / size.height;
    if (zoomY < view.zoom)
        view.zoom = zoomY;
    if (render(page, view, &small[0], W, H, W)) {
        int x0, y0, x1, y1, level;
        if (pbContentOfPicture(&small[0], W, H, x0, y0, x1, y1, level)) {
            // back from the small picture to the page: its middle is the middle of the page
            const float ox = size.width * 0.5f - W * 0.5f / view.zoom;
            const float oy = size.height * 0.5f - H * 0.5f / view.zoom;
            PbPageBox found(ox + x0 / view.zoom, oy + y0 / view.zoom, ox + x1 / view.zoom, oy + y1 / view.zoom);
            if (found.x0 < 0) found.x0 = 0;
            if (found.y0 < 0) found.y0 = 0;
            if (found.x1 > size.width) found.x1 = size.width;
            if (found.y1 > size.height) found.y1 = size.height;
            if (!found.empty()) {
                kept.box = found;
                kept.found = true;
            }
        }
    }
    if (_boxes.size() >= 64)
        _boxes.erase(_boxes.begin());
    _boxes.push_back(kept);
    box = kept.box;
    boxMs += pbNowMs() - t0 - (readMs - read0);
    return kept.found;
}

// ------------------------------------------------------------------------------------ levelling a page

/// How well the dark pixels gather into lines of the tilt t (the tangent of the angle): the mean
/// square of the difference between the sums of neighbouring lines. dark: cw x ch bytes, 1 where
/// the picture is dark. The lines are those through the rows margin..ch-margin, which every tilt
/// asked for keeps inside the picture; step: every step-th column and row. off: room for cw numbers.
static double pbSkewScore(const unsigned char * dark, int cw, int ch, int margin, float t, int step, int * off)
{
    const float xc = (cw - 1) * 0.5f;
    int n = 0;
    for (int x = 0; x < cw; x += step)
        off[n++] = (int)lrintf(t * (x - xc)) * cw + x;
    double score = 0;
    int prev = 0, count = 0;
    bool first = true;
    for (int r = margin; r < ch - margin; r += step) {
        const unsigned char * row = dark + (size_t)r * cw;
        int sum = 0;
        for (int i = 0; i < n; i++)
            sum += row[off[i]];
        if (!first) {
            const int d = sum - prev;
            score += (double)d * d;
            count++;
        }
        prev = sum;
        first = false;
    }
    return count > 0 ? score / count : 0;
}

/// The lines of text read along the tilt t: how many times the sum of a row rises above the mean
/// of all the rows. On a small picture the rows between two lines are not empty (a line is six
/// pixels high there), but they hold less than the mean. sums: room for ch numbers.
static int pbSkewLines(const unsigned char * dark, int cw, int ch, int margin, float t, int * off, int * sums)
{
    const float xc = (cw - 1) * 0.5f;
    for (int x = 0; x < cw; x++)
        off[x] = (int)lrintf(t * (x - xc)) * cw + x;
    long total = 0;
    int rows = 0;
    for (int r = margin; r < ch - margin; r++) {
        const unsigned char * row = dark + (size_t)r * cw;
        int sum = 0;
        for (int x = 0; x < cw; x++)
            sum += row[off[x]];
        sums[r] = sum;
        total += sum;
        rows++;
    }
    if (rows < 2 || total == 0)
        return 0;
    // sums[r] > mean, without a division
    int lines = 0;
    bool above = (long)sums[margin] * rows > total;
    for (int r = margin + 1; r < ch - margin; r++) {
        const bool now = (long)sums[r] * rows > total;
        if (now && !above)
            lines++;
        above = now;
    }
    return lines;
}

// The best tilt has to stand this many times above the usual one to be taken. Pages of text gave
// 15 to 55 on the bench, black and white ones the least; pages with next to no lines 2 to 8.
static const float PB_SKEW_CLEAR = 8.0f;
// and the page has to have this many lines: a title page, a slide with a picture and three words
// gave tilts that were not there
static const int PB_SKEW_LINES = 6;
// a tilt under this is none: a turn costs time and sharpness, and nobody sees a tenth of a degree
static const float PB_SKEW_LEAST = 0.15f;

bool PbPageDoc::skewOfPicture(const unsigned char * gray, int w, int h, int stride, float maxDegrees,
                              float & degrees, float * clear)
{
    degrees = 0;
    if (clear)
        *clear = 0;
    if (w < 64 || h < 64 || maxDegrees <= 0)
        return false;
    std::vector<unsigned char> pic((size_t)w * h);
    for (int y = 0; y < h; y++)
        memcpy(&pic[(size_t)y * w], gray + (size_t)y * stride, w);
    int x0, y0, x1, y1, level;
    if (!pbContentOfPicture(&pic[0], w, h, x0, y0, x1, y1, level))
        return false;
    // the content alone, 1 where it is dark: the table and the margins tell nothing of the lines
    const int cw = x1 - x0, ch = y1 - y0;
    const float tMax = tanf(maxDegrees * PB_PI / 180.0f);
    const int margin = (int)(tMax * cw * 0.5f) + 2;
    if (cw < 48 || ch - margin * 2 < 24)
        return false;
    std::vector<unsigned char> dark((size_t)cw * ch);
    for (int y = 0; y < ch; y++) {
        const unsigned char * src = &pic[(size_t)(y + y0) * w + x0];
        unsigned char * dst = &dark[(size_t)y * cw];
        for (int x = 0; x < cw; x++)
            dst[x] = src[x] < level ? 1 : 0;
    }
    std::vector<int> off(cw);

    // the first pass: half a degree a step, every second column and row
    const float coarse = 0.5f;
    const int nc = (int)(maxDegrees / coarse + 0.5f);
    std::vector<double> scores(nc * 2 + 1);
    int best = 0;
    for (int i = -nc; i <= nc; i++) {
        scores[i + nc] = pbSkewScore(&dark[0], cw, ch, margin, tanf(i * coarse * PB_PI / 180.0f), 2, &off[0]);
        if (scores[i + nc] > scores[best + nc])
            best = i;
    }
    std::vector<double> sorted(scores);
    for (size_t i = 1; i < sorted.size(); i++)          // a few dozen numbers: by insertion
        for (size_t k = i; k > 0 && sorted[k] < sorted[k - 1]; k--) {
            const double v = sorted[k];
            sorted[k] = sorted[k - 1];
            sorted[k - 1] = v;
        }
    const double usual = sorted[sorted.size() / 2];
    const float ratio = usual > 0 ? (float)(scores[best + nc] / usual) : (scores[best + nc] > 0 ? 1000.0f : 0.0f);
    if (clear)
        *clear = ratio;
    if (scores[best + nc] <= 0 || ratio < PB_SKEW_CLEAR || best == -nc || best == nc)
        return false;

    // the second pass: a tenth of a degree a step, every pixel, half a degree to both sides
    const float fine = 0.1f;
    double f[11];
    for (int k = 0; k <= 10; k++) {
        const float deg = best * coarse + (k - 5) * fine;
        f[k] = pbSkewScore(&dark[0], cw, ch, margin, tanf(deg * PB_PI / 180.0f), 1, &off[0]);
    }
    int top = 5;
    for (int k = 0; k <= 10; k++)
        if (f[k] > f[top])
            top = k;
    // On a small picture a tenth of a degree moves the ends of a line by half a pixel: steps
    // beside the best one may read the same pixels and score the same. The middle of such a run.
    int lo = top, hi = top;
    while (lo > 0 && f[lo - 1] >= f[top] * 0.995) lo--;
    while (hi < 10 && f[hi + 1] >= f[top] * 0.995) hi++;
    float tilt = best * coarse + ((lo + hi) * 0.5f - 5) * fine;
    // one best step: the top of the parabola through it and its neighbours
    if (lo == hi && top > 0 && top < 10) {
        const double bend = f[top - 1] - 2 * f[top] + f[top + 1];
        if (bend < 0)
            tilt += fine * (float)(0.5 * (f[top - 1] - f[top + 1]) / bend);
    }
    std::vector<int> sums(ch);
    if (pbSkewLines(&dark[0], cw, ch, margin, tanf(tilt * PB_PI / 180.0f), &off[0], &sums[0]) < PB_SKEW_LINES)
        return false;
    // lines that go down to the right by the tilt are levelled by turning the picture back;
    // a page that lies level is found too, with no turn to make
    degrees = fabsf(tilt) < PB_SKEW_LEAST ? 0.0f : -tilt;
    return true;
}

bool PbPageDoc::skewOf(int page, float maxDegrees, float & degrees)
{
    degrees = 0;
    for (size_t i = 0; i < _skews.size(); i++)
        if (_skews[i].page == page && _skews[i].maxDegrees == maxDegrees) {
            degrees = _skews[i].degrees;
            return _skews[i].found;
        }
    PbPageSize size;
    if (!pageSize(page, size))
        return false;
    const double t0 = pbNowMs(), read0 = readMs;
    KeptSkew kept;
    kept.page = page;
    kept.maxDegrees = maxDegrees;
    kept.found = false;
    kept.degrees = 0;
    // the lines of a text need more pixels than the content box does
    const int W = 512;
    int H = (int)lrintf(size.height * W / size.width);
    if (H < 64)
        H = 64;
    if (H > 1024)
        H = 1024;
    std::vector<unsigned char> small((size_t)W * H);
    PbPageView view;
    view.cx = size.width * 0.5f;
    view.cy = size.height * 0.5f;
    view.zoom = W / size.width;
    const float zoomY = H / size.height;
    if (zoomY < view.zoom)
        view.zoom = zoomY;
    if (render(page, view, &small[0], W, H, W))
        kept.found = skewOfPicture(&small[0], W, H, W, maxDegrees, kept.degrees);
    if (_skews.size() >= 64)
        _skews.erase(_skews.begin());
    _skews.push_back(kept);
    degrees = kept.degrees;
    levelMs += pbNowMs() - t0 - (readMs - read0);
    return kept.found;
}

/// The content of a small picture of a page as it lies after the picture is turned clockwise by
/// angle (degrees) about its middle: u0..u1, v0..v1 in pixels of the turned picture counted from
/// its middle (u to the right, v down). What pbContentOfPicture does, with the pixels counted into
/// the columns and the rows of the turned picture; the picture itself is not turned, so its
/// corners bring in nothing that is not of the page.
static bool pbContentOfPictureTurned(const unsigned char * small, int W, int H, float angle,
                                     float & u0, float & v0, float & u1, float & v1)
{
    const size_t count = (size_t)W * H;
    int hist[256];
    memset(hist, 0, sizeof(hist));
    for (size_t i = 0; i < count; i++)
        hist[small[i]]++;
    int level = pbOtsu(hist, (int)count);

    // the column and the row of the turned picture every pixel falls into
    const float a = angle * PB_PI / 180.0f;
    const float c = cosf(a), s = sinf(a);
    const int half = (int)(sqrtf((float)W * W + (float)H * H) * 0.5f) + 2;
    const int N = half * 2;
    std::vector<short> bu(count), bv(count);
    int pu0 = N, pu1 = 0, pv0 = N, pv1 = 0;
    for (int y = 0; y < H; y++) {
        const float dx = 0.5f - W * 0.5f, dy = y + 0.5f - H * 0.5f;
        float u = dx * c - dy * s, v = dx * s + dy * c;
        short * ru = &bu[(size_t)y * W];
        short * rv = &bv[(size_t)y * W];
        for (int x = 0; x < W; x++) {
            const int iu = (int)floorf(u) + half, iv = (int)floorf(v) + half;
            ru[x] = (short)iu;
            rv[x] = (short)iv;
            if (iu < pu0) pu0 = iu;
            if (iu >= pu1) pu1 = iu + 1;
            if (iv < pv0) pv0 = iv;
            if (iv >= pv1) pv1 = iv + 1;
            u += c;
            v += s;
        }
    }
    const int allU0 = pu0, allU1 = pu1, allV0 = pv0, allV1 = pv1;

    // a page photographed on a dark table: the rim of the picture is dark. The paper is then the
    // columns and the rows of the turned picture that are light along half of the longest one.
    const int rim = W / 25 > 2 ? W / 25 : 2;
    int rimDark = 0, rimAll = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (x < rim || x >= W - rim || y < rim || y >= H - rim) {
                rimAll++;
                if (small[(size_t)y * W + x] < level)
                    rimDark++;
            }
    if (rimDark * 10 > rimAll * 6) {
        std::vector<int> lightU(N, 0), lightV(N, 0);
        for (size_t i = 0; i < count; i++)
            if (small[i] >= level) {
                lightU[bu[i]]++;
                lightV[bv[i]]++;
            }
        int mostU = 0, mostV = 0;
        for (int i = 0; i < N; i++) {
            if (lightU[i] > mostU) mostU = lightU[i];
            if (lightV[i] > mostV) mostV = lightV[i];
        }
        while (pu0 < pu1 && lightU[pu0] * 2 < mostU) pu0++;
        while (pu1 > pu0 && lightU[pu1 - 1] * 2 < mostU) pu1--;
        while (pv0 < pv1 && lightV[pv0] * 2 < mostV) pv0++;
        while (pv1 > pv0 && lightV[pv1 - 1] * 2 < mostV) pv1--;
        // the edge of the paper and its shadow stay outside
        const int inU = (pu1 - pu0) / 30 + 1, inV = (pv1 - pv0) / 30 + 1;
        pu0 += inU; pu1 -= inU; pv0 += inV; pv1 -= inV;
        if (pu1 - pu0 > 16 && pv1 - pv0 > 16) {
            // the level between the paper and its text, by the paper alone
            memset(hist, 0, sizeof(hist));
            int paper = 0;
            for (size_t i = 0; i < count; i++)
                if (bu[i] >= pu0 && bu[i] < pu1 && bv[i] >= pv0 && bv[i] < pv1) {
                    hist[small[i]]++;
                    paper++;
                }
            if (paper > 0)
                level = pbOtsu(hist, paper);
        } else {
            pu0 = allU0; pu1 = allU1; pv0 = allV0; pv1 = allV1;
        }
    }
    // the content: the columns and the rows of the paper with enough dark pixels
    std::vector<int> darkU(N, 0), darkV(N, 0);
    for (size_t i = 0; i < count; i++)
        if (small[i] < level && bu[i] >= pu0 && bu[i] < pu1 && bv[i] >= pv0 && bv[i] < pv1) {
            darkU[bu[i]]++;
            darkV[bv[i]]++;
        }
    const int needU = (pv1 - pv0) / 120 + 2, needV = (pu1 - pu0) / 120 + 2;
    int x0 = pu0, x1 = pu1, y0 = pv0, y1 = pv1;
    while (x0 < x1 && darkU[x0] < needU) x0++;
    while (x1 > x0 && darkU[x1 - 1] < needU) x1--;
    while (y0 < y1 && darkV[y0] < needV) y0++;
    while (y1 > y0 && darkV[y1 - 1] < needV) y1--;
    if (x1 - x0 <= 8 || y1 - y0 <= 8)
        return false;
    // a little air around the content
    const int air = W / 64;
    x0 = x0 - air > pu0 ? x0 - air : pu0;
    y0 = y0 - air > pv0 ? y0 - air : pv0;
    x1 = x1 + air < pu1 ? x1 + air : pu1;
    y1 = y1 + air < pv1 ? y1 + air : pv1;
    u0 = (float)(x0 - half);
    u1 = (float)(x1 - half);
    v0 = (float)(y0 - half);
    v1 = (float)(y1 - half);
    return true;
}

bool PbPageDoc::levelView(int page, float maxDegrees, int width, int height, PbPageView & view)
{
    view = PbPageView();
    PbPageSize size;
    if (!pageSize(page, size) || width < 1 || height < 1)
        return false;
    const KeptLevel * level = NULL;
    for (size_t i = 0; i < _levels.size(); i++)
        if (_levels[i].page == page && _levels[i].maxDegrees == maxDegrees)
            level = &_levels[i];
    if (level == NULL) {
        const double t0 = pbNowMs(), read0 = readMs, box0 = boxMs;
        KeptLevel kept;
        kept.page = page;
        kept.maxDegrees = maxDegrees;
        kept.found = false;
        kept.angle = 0;
        kept.cx = size.width * 0.5f;
        kept.cy = size.height * 0.5f;
        kept.w = size.width;
        kept.h = size.height;
        // one small picture of the page serves both: the tilt, and the content as it lies turned
        const int W = 512;
        int H = (int)lrintf(size.height * W / size.width);
        if (H < 64)
            H = 64;
        if (H > 1024)
            H = 1024;
        std::vector<unsigned char> small((size_t)W * H);
        PbPageView whole;
        whole.cx = size.width * 0.5f;
        whole.cy = size.height * 0.5f;
        whole.zoom = W / size.width;
        const float zoomY = H / size.height;
        if (zoomY < whole.zoom)
            whole.zoom = zoomY;
        // A page of a PDF that has text was drawn by the program that made the file: it lies
        // level. The little such a page may hold beside the text (a map, a drawing) gave tilts
        // that were not there, two pages of thirty of a book. A DjVu page is a scan with or
        // without a text layer, and is always looked at.
        const bool exact = kind() == PB_PAGEDOC_PDF && hasText(page);
        const bool drawn = !exact && render(page, whole, &small[0], W, H, W);
        KeptSkew skew;
        skew.page = page;
        skew.maxDegrees = maxDegrees;
        skew.degrees = 0;
        skew.found = drawn && skewOfPicture(&small[0], W, H, W, maxDegrees, skew.degrees);
        bool known = false;
        for (size_t i = 0; i < _skews.size(); i++)
            if (_skews[i].page == page && _skews[i].maxDegrees == maxDegrees)
                known = true;
        if (!known) {
            if (_skews.size() >= 64)
                _skews.erase(_skews.begin());
            _skews.push_back(skew);
        }
        kept.angle = skew.degrees;
        if (kept.angle == 0) {
            // no turn to make: the content box of the upright page
            PbPageBox box(0, 0, size.width, size.height);
            kept.found = contentBox(page, box);
            kept.cx = (box.x0 + box.x1) * 0.5f;
            kept.cy = (box.y0 + box.y1) * 0.5f;
            kept.w = box.x1 - box.x0;
            kept.h = box.y1 - box.y0;
        } else {
            const float a = kept.angle * PB_PI / 180.0f;
            const float c = cosf(a), s = sinf(a);
            float u0, v0, u1, v1;
            if (pbContentOfPictureTurned(&small[0], W, H, kept.angle, u0, v0, u1, v1)) {
                // from the middle of the content on the turned picture back to the page
                const float qx = (u0 + u1) * 0.5f, qy = (v0 + v1) * 0.5f;
                kept.cx = whole.cx + (qx * c + qy * s) / whole.zoom;
                kept.cy = whole.cy + (-qx * s + qy * c) / whole.zoom;
                kept.w = (u1 - u0) / whole.zoom;
                kept.h = (v1 - v0) / whole.zoom;
                kept.found = true;
            } else {
                // the whole page as it lies turned
                kept.w = fabsf(size.width * c) + fabsf(size.height * s);
                kept.h = fabsf(size.width * s) + fabsf(size.height * c);
            }
        }
        if (_levels.size() >= 64)
            _levels.erase(_levels.begin());
        _levels.push_back(kept);
        level = &_levels.back();
        levelMs += pbNowMs() - t0 - (readMs - read0) - (boxMs - box0);
    }
    view.cx = level->cx;
    view.cy = level->cy;
    view.angle = level->angle;
    const float zx = level->w > 0 ? width / level->w : 1, zy = level->h > 0 ? height / level->h : 1;
    view.zoom = zx < zy ? zx : zy;
    return level->found;
}
