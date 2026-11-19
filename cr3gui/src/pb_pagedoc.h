/*
 * A document of pages: PDF (MuPDF) and DjVu (DjVuLibre) behind one interface.
 *
 * The picture mode of the reader (pb_pageview) shows a part of a page as it is: a frame. A frame
 * is given by the point of the page that stands in the middle of the screen, the zoom and the
 * angle; the library draws it straight into the gray buffer of the caller, at its final size.
 * Nothing here depends on the engine or on the firmware, so the PC bench builds this file alone
 * (dev/pdfdjvu/pagebench.cpp).
 */
#ifndef PB_PAGEDOC_H
#define PB_PAGEDOC_H

#include <string>
#include <vector>

/// The frame: which part of a page goes to the buffer.
struct PbPageView {
    float cx, cy;       ///< the point of the page (page units, y down) drawn in the middle of the buffer
    float zoom;         ///< buffer pixels per page unit
    float angle;        ///< the picture of the page is turned clockwise by this angle, degrees
    PbPageView() : cx(0), cy(0), zoom(1), angle(0) { }
};

/// Size of a page in its own units: points for a PDF, pixels of the scan for a DjVu.
struct PbPageSize {
    float width, height;
    float dpi;          ///< page units per inch: 72 for a PDF, the resolution of the scan for a DjVu
    PbPageSize() : width(0), height(0), dpi(72) { }
};

/// A rectangle of a page, page units.
struct PbPageBox {
    float x0, y0, x1, y1;
    PbPageBox() : x0(0), y0(0), x1(0), y1(0) { }
    PbPageBox(float a, float b, float c, float d) : x0(a), y0(b), x1(c), y1(d) { }
    bool empty() const { return x1 <= x0 || y1 <= y0; }
};

/// An entry of the contents of the file.
struct PbPageTocEntry {
    int level;          ///< 0 for the top level
    int page;           ///< page index from 0, -1 when the entry leads nowhere in the file
    std::string title;  ///< UTF-8
};

/// What a frame takes in.
enum PbPageScale {
    PB_PAGE_SCALE_PAGE = 0,       ///< the whole page in the screen
    PB_PAGE_SCALE_WIDTH,          ///< the page as wide as the screen
    PB_PAGE_SCALE_CONTENT,        ///< the whole content in the screen
    PB_PAGE_SCALE_CONTENT_WIDTH,  ///< the content as wide as the screen
    PB_PAGE_SCALE_OWN,            ///< the frame set by the reader
    PB_PAGE_SCALE_AUTO,           ///< every page levelled by itself and its content in the screen (levelView)
    PB_PAGE_SCALE_COUNT
};

/// What a page turn does.
enum PbPageLeafing {
    PB_PAGE_LEAF_PAGE = 0,        ///< shows the next page at once
    PB_PAGE_LEAF_FRAMES,          ///< goes through the frames of a page, then to the next page
    PB_PAGE_LEAF_RIBBON           ///< the pages one under another: moves on by a screen (stripOf)
};

/// The frame set by the reader: the point of the page in the middle of the screen and the zoom.
struct PbPageOwnFrame {
    float cx, cy, zoom;
    bool set;
    PbPageOwnFrame() : cx(0), cy(0), zoom(1), set(false) { }
};

/// A page as a piece of the ribbon (the leafing PB_PAGE_LEAF_RIBBON): the page, or its content by
/// the scale, as wide as the window. Its rows are drawn in parts, so that a screen holds the end
/// of a page and the beginning of the next one.
struct PbPageStrip {
    float angle;    ///< the turn of the picture, degrees
    float zoom;     ///< pixels per page unit
    float qcx;      ///< the middle of the strip across, on the turned page (page units)
    float qy0;      ///< its top there
    int height;     ///< rows of the strip
    PbPageStrip() : angle(0), zoom(1), qcx(0), qy0(0), height(0) { }
};

enum PbPageDocKind {
    PB_PAGEDOC_NONE = 0,
    PB_PAGEDOC_PDF,
    PB_PAGEDOC_DJVU
};

class PbPageDoc {
public:
    /// By the first bytes of the file: "%PDF" or the DjVu signature. A file in an archive is
    /// taken out of it by the caller first.
    static PbPageDocKind kindOfFile(const char * path);
    /// Opens the file; NULL and the reason in error when it cannot be opened.
    static PbPageDoc * open(const char * path, std::string * error = NULL);

    PbPageDoc() : readMs(0), boxMs(0), levelMs(0), _detailZoom(0), _detailLow(0) { }
    virtual ~PbPageDoc() { }
    /// For the log of the caller, who clears them: the time of reading and decoding pages, and of
    /// finding content boxes without the page read for them. Only added to here.
    double readMs, boxMs;
    virtual PbPageDocKind kind() const = 0;
    virtual int pageCount() = 0;
    /// The size of a page; kept once asked.
    virtual bool pageSize(int page, PbPageSize & size) = 0;
    /// Draws the frame into an 8-bit gray buffer of width x height pixels, top row first, white
    /// where the page is not. The buffer is written whole.
    virtual bool render(int page, const PbPageView & view, unsigned char * dst, int width, int height, int stride) = 0;
    /// The contents of the file, empty when it has none.
    virtual void contents(std::vector<PbPageTocEntry> & entries) = 0;
    /// The title and the author the file names for itself (UTF-8); empty when it names none.
    virtual void info(std::string & title, std::string & author) { title.clear(); author.clear(); }
    /// True when the page has text: a text PDF, a DjVu with a text layer.
    virtual bool hasText(int page) = 0;
    /// The page that will most likely be asked for after the ones in work. A document that can
    /// read a page beside the drawing (DjVu: in a thread of its library) starts reading it.
    virtual void hintNext(int page) { (void)page; }

    /// The part of the page that holds its content: the margins are cut off, and for a page
    /// photographed on a dark table the table too. Found on a small picture of the page and kept.
    /// False when nothing is found (an empty page): box is the whole page then.
    bool contentBox(int page, PbPageBox & box);

    /// The frame that shows the box, turned by angle, whole in a buffer of width x height.
    static PbPageView viewOfBox(const PbPageBox & box, float angle, int width, int height);

    // Levelling a page by itself: every page of a book photographed or scanned by hand is crooked
    // in its own way. The scale PB_PAGE_SCALE_AUTO of framesOf; measured on the PC bench
    // (dev/pdfdjvu/pagebench.cpp, "level" and "turns").

    /// The tilt of the lines of a gray picture of a page (w x h): degrees is the angle the picture
    /// has to be turned by clockwise (PbPageView::angle) for its lines to lie level. Looked for
    /// within maxDegrees to both sides: the dark pixels are summed along lines of every tilt, and
    /// the tilt at which the sums of neighbouring lines differ most is that of the text (the way
    /// k2pdfopt of KOReader goes, on a small picture and in two passes instead of two hundred
    /// angles over the whole page). False, and degrees 0, when no tilt stands out (a blank page, a
    /// photograph) or the best one lies at the limit. clear, when asked for: how many times the
    /// best tilt is above the usual one.
    static bool skewOfPicture(const unsigned char * gray, int w, int h, int stride, float maxDegrees,
                              float & degrees, float * clear = NULL);
    /// The same for a page: found on a small picture of it and kept.
    bool skewOf(int page, float maxDegrees, float & degrees);
    /// The frame that shows the content of the page levelled, whole in a buffer of width x height:
    /// the picture turned by the angle of skewOf, the margins cut off as they lie then. A page
    /// with no clear tilt gets the frame of its content box, upright. False when no content is
    /// found: the frame is the whole page then. Kept per page.
    bool levelView(int page, float maxDegrees, int width, int height, PbPageView & view);
    /// For the log of the caller, as boxMs: the time of skewOf and of the content of the levelled page.
    double levelMs;

    /// The frames of a page for a window of width x height, in the order they are read; at least
    /// one. scale: PbPageScale; leafing: PbPageLeafing ("next page at once" gives the first frame
    /// alone); angle: the turn of the picture; own: the frame of the reader, used by
    /// PB_PAGE_SCALE_OWN when it is set (the frames then go on from it to the end of the content).
    /// PB_PAGE_SCALE_AUTO takes the angle of the page itself in place of angle.
    void framesOf(int page, int scale, int leafing, float angle, const PbPageOwnFrame & own,
                  int width, int height, std::vector<PbPageView> & frames);
    /// The strip of a page for a window width pixels wide (height: the window, for the picture
    /// the content is looked for on). The scale tells what is as wide as the window: the page
    /// ("whole page", "page width"), its content ("whole content", "content width", "automatic":
    /// levelled by itself), or what the own frame of the reader takes in at its zoom.
    bool stripOf(int page, int scale, float angle, const PbPageOwnFrame & own, int width, int height,
                 PbPageStrip & strip);
    /// Draws the rows from..to of the strip (counted from its top) into dst: to - from rows of
    /// width pixels. A row outside of the page is white.
    bool renderStrip(int page, const PbPageStrip & strip, int from, int to, unsigned char * dst, int width,
                     int stride);

protected:
    /// What the scale takes of a page, as it lies on the page turned by the angle: q = R(angle) * p.
    /// PB_PAGE_SCALE_AUTO puts the angle of the page itself into angle. The part of framesOf and
    /// of stripOf that is the same.
    void turnedSpan(int page, const PbPageSize & size, int scale, const PbPageOwnFrame & own, int width, int height,
                    float & angle, float & qx0, float & qx1, float & qy0, float & qy1);
    /// The zoom (pixels per page unit) the frames of the page being laid out will be drawn at, at
    /// least; 0: not known. A picture of a page decoded for a smaller frame (the small picture of
    /// the content box) is decoded at once for this zoom, so it is decoded one time.
    float _detailZoom;
    /// The same, as the least the frames can have (the whole page in the window, or the own
    /// frame): for a document whose picture of a page costs by its size (DjVu), which makes it
    /// no larger than needed. _detailZoom allows for the content being larger than the page.
    float _detailLow;

    struct KeptBox {
        int page;
        bool found;
        PbPageBox box;
    };
    std::vector<KeptBox> _boxes;

    struct KeptSkew {
        int page;
        float maxDegrees;
        bool found;
        float degrees;
    };
    std::vector<KeptSkew> _skews;
    /// The content of a page levelled: the angle, the middle of the content (page units) and its
    /// size as it lies on the turned page (page units).
    struct KeptLevel {
        int page;
        float maxDegrees;
        bool found;
        float angle, cx, cy, w, h;
    };
    std::vector<KeptLevel> _levels;
};

/// Draws an 8-bit gray picture into another through an affine map: the pixel (x, y) of dst takes
/// the source at (ox + x * ax + y * bx, oy + x * ay + y * by), in pixels of src counted from the
/// middle of its first pixel. Two neighbours are mixed (bilinear) while a step of dst is under two
/// pixels of src; a larger step takes the mean of a few pixels spread over the step. Where the
/// source is not: white when whiteOutside, else dst is left as it is. src is at most 32000 pixels
/// wide and high.
void pbBlitGray(const unsigned char * src, int srcW, int srcH, int srcStride,
                float ox, float oy, float ax, float ay, float bx, float by,
                unsigned char * dst, int dstW, int dstH, int dstStride, bool whiteOutside);

/// Turns an 8-bit gray picture about a point: dst pixel (x, y) takes the source at
/// R(-angle) * (x - dcx, y - dcy) + (scx, scy), bilinear, white outside of the source.
void pbRotateGray(const unsigned char * src, int srcW, int srcH, int srcStride, float scx, float scy,
                  unsigned char * dst, int dstW, int dstH, int dstStride, float dcx, float dcy, float angle);

#endif // PB_PAGEDOC_H
