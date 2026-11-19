/*
 * A PDF with text as a book. See pb_pdftext.h.
 *
 * Two passes. The first reads every page with the text extraction of MuPDF (page segmentation,
 * paragraph breaks, table hunt, the structure tags of a tagged file) into a list of items:
 * paragraphs with their runs of style, tables, pictures. A picture is decoded, reduced to the
 * screen, packed as a gray JPEG and written out at once, so the pictures of a file are never in
 * memory together. The second pass knows the whole file: the font size of the body text, what
 * repeats at the top and the bottom of the pages, the contents; it writes the book.
 *
 * A call of MuPDF that can throw stands in a small fz_try with plain variables only: an error of
 * the library is a longjmp, which must not pass over a C++ object.
 */
#include "pb_pdftext.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <algorithm>
#include <map>
#include <vector>

#include <mupdf/fitz.h>

namespace {

double nowMs()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

void quiet(void *, const char *) { }

// ------------------------------------------------------------------------------------ text
// S_IMAGE: the run is a picture in the line (a formula); its text is the number of the picture
enum { S_BOLD = 1, S_ITALIC = 2, S_MONO = 4, S_SUP = 8, S_SUB = 16, S_IMAGE = 32 };

struct Run {
    unsigned style;
    std::string text;   // UTF-8, as it is in the file
};
typedef std::vector<Run> Runs;

void appendUtf8(std::string & s, int c)
{
    if (c < 0x80) {
        s += (char)c;
    } else if (c < 0x800) {
        s += (char)(0xC0 | (c >> 6));
        s += (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        s += (char)(0xE0 | (c >> 12));
        s += (char)(0x80 | ((c >> 6) & 0x3F));
        s += (char)(0x80 | (c & 0x3F));
    } else {
        s += (char)(0xF0 | (c >> 18));
        s += (char)(0x80 | ((c >> 12) & 0x3F));
        s += (char)(0x80 | ((c >> 6) & 0x3F));
        s += (char)(0x80 | (c & 0x3F));
    }
}

/// The character that starts at s[i]; i moves past it.
int nextChar(const std::string & s, size_t & i)
{
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80)
        return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
    int value = c & (0x3F >> extra);
    while (extra-- > 0 && i < s.size())
        value = (value << 6) | ((unsigned char)s[i++] & 0x3F);
    return value;
}

std::string textOf(const Runs & runs)
{
    std::string s;
    for (size_t i = 0; i < runs.size(); i++)
        if (!(runs[i].style & S_IMAGE))
            s += runs[i].text;
    return s;
}

int firstChar(const Runs & runs)
{
    for (size_t r = 0; r < runs.size(); r++)
        for (size_t i = 0; i < runs[r].text.size(); ) {
            int c = nextChar(runs[r].text, i);
            if (c != ' ' && c != 0xA0 && c != '\t')
                return c;
        }
    return 0;
}

/// The number of characters of the first word.
int firstWord(const Runs & runs)
{
    int n = 0;
    for (size_t r = 0; r < runs.size(); r++)
        for (size_t i = 0; i < runs[r].text.size(); ) {
            int c = nextChar(runs[r].text, i);
            if (c == ' ' || c == 0xA0 || c == '\t') {
                if (n > 0)
                    return n;
            } else {
                n++;
            }
        }
    return n;
}

/// "a)" or "a." at the start: an entry of a list, not a paragraph that goes on.
bool startsEntry(const Runs & runs)
{
    const std::string s = runs.empty() ? std::string() : runs[0].text;
    size_t i = 0;
    while (i < s.size() && s[i] == ' ')
        i++;
    if (i >= s.size())
        return false;
    nextChar(s, i);
    return i < s.size() && (s[i] == ')' || s[i] == '.');
}

/// The last character of the text itself: a footnote mark set above the line after it is not counted.
int lastChar(const Runs & runs)
{
    int last = 0;
    for (size_t r = 0; r < runs.size(); r++) {
        if ((runs[r].style & 8) != 0 && last != 0)      // S_SUP
            continue;
        for (size_t i = 0; i < runs[r].text.size(); ) {
            int c = nextChar(runs[r].text, i);
            if (c != ' ' && c != 0xA0 && c != '\t')
                last = c;
        }
    }
    return last;
}

/// A sentence is over: after this a paragraph does not go on, whatever the next one starts with.
bool endsFully(int c)
{
    return c == '.' || c == '!' || c == '?' || c == 0x2026;
}

bool isLower(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7) || (c >= 0x430 && c <= 0x45F)
        || (c >= 0x3B1 && c <= 0x3C9) || c == 0x491;
}

bool isLetter(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0x24F) || (c >= 0x370 && c <= 0x52F);
}

bool isHyphen(int c)
{
    return c == '-' || c == 0x2010 || c == 0x2011 || c == 0xAD;
}

/// The end of a sentence, or of a line that is one by itself: nothing goes on after it.
bool endsSentence(int c)
{
    return c == '.' || c == '!' || c == '?' || c == 0x2026 || c == ':' || c == ';' || c == 0xBB || c == 0x201D
        || c == '"' || c == ')' || c == ']';
}

/// Digits as '#', letters in lower case where that is one step, spaces single: what is the same
/// in a running title from page to page.
std::string normalized(const std::string & s)
{
    std::string out;
    bool space = true, digit = false;
    for (size_t i = 0; i < s.size(); ) {
        int c = nextChar(s, i);
        if (c == ' ' || c == 0xA0 || c == '\t') {
            if (!space)
                out += ' ';
            space = true;
            digit = false;
            continue;
        }
        space = false;
        if (c >= '0' && c <= '9') {
            if (!digit)
                out += '#';
            digit = true;
            continue;
        }
        digit = false;
        if (c >= 'A' && c <= 'Z')
            c += 32;
        else if (c >= 0x410 && c <= 0x42F)
            c += 32;
        else if (c >= 0x400 && c <= 0x40F)
            c += 80;
        appendUtf8(out, c);
    }
    while (!out.empty() && out[out.size() - 1] == ' ')
        out.erase(out.size() - 1);
    return out;
}

bool onlyNumber(const std::string & key)
{
    if (key.empty() || key.find('#') == std::string::npos)
        return false;
    for (size_t i = 0; i < key.size(); i++) {
        unsigned char c = (unsigned char)key[i];
        if (c != '#' && c != ' ' && c != '-' && c != '.' && c != '|' && c < 0x80)
            return false;
        if (c >= 0x80 && !(c == 0xE2 || c == 0x80 || c == 0x93 || c == 0x94))   // en and em dash
            return false;
    }
    return key.size() <= 12;
}

/// A page number written with Roman numerals, as the pages of a preface are: "xvi", "XXIII",
/// alone or with the marks a page number has around it. Up to 399 (no D and no M): a longer
/// front matter is not met, and words made of these letters ("dim", "mix") are.
bool romanNumber(const std::string & s)
{
    std::string letters;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (c == 'i' || c == 'v' || c == 'x' || c == 'l' || c == 'c')
            letters += c;
        else if (!(c == ' ' || c == '-' || c == '.' || c == '[' || c == ']' || c == '(' || c == ')' || c == '|'))
            return false;
    }
    if (letters.empty() || letters.size() > 9)
        return false;
    // hundreds, tens, units, each written the one way a number is written
    static const char * const parts[3][10] = {
        { "", "c", "cc", "ccc", "", "", "", "", "", "" },
        { "", "x", "xx", "xxx", "xl", "l", "lx", "lxx", "lxxx", "xc" },
        { "", "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix" }
    };
    size_t at = 0;
    for (int rank = 0; rank < 3; rank++) {
        size_t best = 0;
        for (int d = 1; d < 10; d++) {
            const size_t n = strlen(parts[rank][d]);
            if (n > best && letters.compare(at, n, parts[rank][d]) == 0)
                best = n;
        }
        at += best;
    }
    return at == letters.size();
}

// ------------------------------------------------------------------------------------ items
enum Kind { K_PARA, K_HEADING, K_IMAGE, K_TABLE, K_NOTE };

struct Cell {
    int colspan, rowspan;
    Runs runs;
    Cell() : colspan(1), rowspan(1) { }
};

struct Item {
    Kind kind;
    int page;           // from 0
    int level;          // of a heading, from 1
    float size;         // the font size of its first line
    float x0, y0, x1, y1;
    int chars, boldChars, monoChars;
    int mathChars;      // signs of formulas: of a mathematical font, or mathematical signs
    int weakMath;       // signs a formula has and a text may have: = + and the like
    int lines;
    float firstX, restX; // where its first line begins, and the leftmost of its other lines (1e9: one line)
    float lastX1;       // the right end of its last line
    float rowX1;        // ... and of its first one
    bool verse;         // a line of verse: the lines of its block are kept
    // of a paragraph of one line: the pieces of the line that stand apart (the cells of a row
    // of a table without rules); a tab parts them in the text
    std::vector<float> cellX0, cellX1;
    bool tagged;        // the kind is told by the structure of the file
    bool quote;
    bool drop;
    Runs runs;
    int image;          // K_IMAGE: the number of the picture
    std::vector<std::vector<Cell> > rows;
    Item() : kind(K_PARA), page(0), level(0), size(0), x0(0), y0(0), x1(0), y1(0), chars(0), boldChars(0),
             monoChars(0), mathChars(0), weakMath(0), lines(0), firstX(0), restX(1e9f), lastX1(0), rowX1(0),
             verse(false), tagged(false),
             quote(false), drop(false), image(-1) { }
};

struct Picture {
    int page;
    std::string digest;
    bool large;         // takes most of its page
};

struct PageInfo {
    float width, height;
    int chars;
    bool hanging;       // a list of entries whose first lines hang out to the left
    size_t firstItem, endItem;
};

/// What a block inherits from the structure of a tagged file.
struct Tag {
    int heading;        // level, 0: none
    bool note, mono, quote, tagged;
    Tag() : heading(0), note(false), mono(false), quote(false), tagged(false) { }
};

struct OutlineEntry {
    int level, page;
    std::string title;
};

class Translator {
public:
    fz_context * ctx;
    fz_document * doc;
    std::vector<Item> items;
    std::vector<Picture> pictures;
    std::vector<PageInfo> pages;
    std::map<int, long> sizes;      // font size in half points -> characters
    long cyrillic, latin, ukrainian;
    FILE * binaries;
    PbPdfTextStats stats;
    int page;
    float pageW, pageH;
    float pageTop;          // the top of the page in the units its blocks are given in
    fz_page * curPage;      // the page being read, for the pictures cut out of it

    Translator() : ctx(NULL), doc(NULL), cyrillic(0), latin(0), ukrainian(0), binaries(NULL), page(0), pageW(0), pageH(0),
                   pageTop(0), curPage(NULL), mathFontAsked(NULL), mathFontIs(false), pageHanging(false), pageOut(0)
    {
        memset(&stats, 0, sizeof(stats));
    }

    // -------------------------------------------------------------------------------- pass 1
    static void addChar(Runs & runs, unsigned style, int c)
    {
        if (runs.empty() || runs.back().style != style) {
            Run run;
            run.style = style;
            runs.push_back(run);
        }
        appendUtf8(runs.back().text, c);
    }

    static bool endsWithSpace(const Runs & runs)
    {
        if (runs.empty())
            return true;
        if (runs.back().style & S_IMAGE)
            return false;
        const std::string & t = runs.back().text;
        return t.empty() || t[t.size() - 1] == ' ' || t[t.size() - 1] == '\t';
    }

    void countLetter(int c)
    {
        if (c >= 0x400 && c <= 0x4FF) {
            cyrillic++;
            if (c == 0x456 || c == 0x457 || c == 0x454 || c == 0x491 || c == 0x406 || c == 0x407 || c == 0x404)
                ukrainian++;
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            latin++;
        }
    }

    struct Span {
        fz_stext_char * from, * to;     // the characters the picture stands for; NULL: none, see after
        int image;
        fz_stext_char * after;          // from == NULL: the picture goes in after this character
        fz_rect region;                 // what is cut out besides the characters; empty: nothing
    };

    /// A sign whose formula cannot be told in the characters of a line: a root, a sum, a product,
    /// an integral, or a sign of a font of large signs.
    bool bigSign(fz_stext_char * ch)
    {
        const int c = ch->c;
        if (c == 0x221A || c == 0x221B || c == 0x2211 || c == 0x220F || c == 0x2210 || (c >= 0x222B && c <= 0x2233))
            return true;
        const char * name = ch->font ? fz_font_name(ctx, ch->font) : NULL;
        return name && (strstr(name, "CMEX") || strstr(name, "MTEX"));
    }

    /// A piece between spaces that is part of a formula and not a word: short, without three
    /// letters in a row, with a digit, a bracket or a sign in it, or a single Latin or Greek letter.
    static bool formulaPiece(const std::vector<fz_stext_char *> & chars, size_t a, size_t b)
    {
        if (b <= a || b - a > 8)
            return false;
        int letters = 0;
        bool mark = false;
        for (size_t i = a; i < b; i++) {
            const int c = chars[i]->c;
            const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c < 0x530);
            letters = letter ? letters + 1 : 0;
            if (letters >= 3)
                return false;
            if ((c >= '0' && c <= '9') || strchr("=+-/()[]{}^<>|*", c < 128 ? c : 'x') || c == 0xB1 || c == 0xD7
                    || c == 0xB7 || (c >= 0x2200 && c <= 0x22FF))
                mark = true;
        }
        const int only = chars[a]->c;
        return mark || (b - a == 1 && ((only >= 'a' && only <= 'z') || (only >= 'A' && only <= 'Z')
                                       || (only >= 0x391 && only <= 0x3C9)));
    }

    /// The formulas of a line that have a root, a sum or the like in them: from the first sign of
    /// a formula to the last one, with the pieces of a formula around them.
    void findFormulas(fz_stext_line * line, std::vector<Span> & spans)
    {
        std::vector<fz_stext_char *> chars;
        for (fz_stext_char * ch = line->first_char; ch; ch = ch->next)
            chars.push_back(ch);
        const size_t n = chars.size();
        for (size_t i = 0; i < n; i++) {
            if (isMath(chars[i]) != 2)
                continue;
            size_t last = i;
            bool big = bigSign(chars[i]);
            int letters = 0;
            for (size_t k = i + 1; k < n && k - last <= 12; k++) {
                const int c = chars[k]->c;
                if (isMath(chars[k]) == 2) {
                    last = k;
                    letters = 0;
                    big = big || bigSign(chars[k]);
                    continue;
                }
                const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c < 0x530);
                letters = letter ? letters + 1 : 0;
                if (letters >= 3)
                    break;
            }
            if (!big) {
                i = last;
                continue;
            }
            // to the ends of the pieces the signs stand in, then over the pieces of a formula
            size_t from = i, to = last + 1;
            while (from > 0 && chars[from - 1]->c != ' ' && i - (from - 1) <= 8)
                from--;
            while (to < n && chars[to]->c != ' ' && to - last <= 10)
                to++;
            for (int more = 0; more < 4 && to < n && chars[to]->c == ' '; more++) {
                size_t end = to + 1;
                while (end < n && chars[end]->c != ' ')
                    end++;
                if (!formulaPiece(chars, to + 1, end))
                    break;
                to = end;
            }
            for (int more = 0; more < 4 && from >= 2 && chars[from - 1]->c == ' '; more++) {
                size_t begin = from - 1;
                while (begin > 0 && chars[begin - 1]->c != ' ')
                    begin--;
                if (!formulaPiece(chars, begin, from - 1))
                    break;
                from = begin;
            }
            // the mark of the sentence after it stays in the text
            while (to - 1 > last && strchr(".,;:", chars[to - 1]->c < 128 ? chars[to - 1]->c : 'x'))
                to--;
            if (!spans.empty() && chars[from] <= spans.back().to)
                break;
            Span span;
            span.from = chars[from];
            span.to = chars[to - 1];
            span.image = -1;
            span.after = NULL;
            span.region = fz_empty_rect;
            spans.push_back(span);
            i = to - 1;
        }
    }

    /// A part of the page as a picture to stand in a line of the text: the letters of the page
    /// come out about as tall as the letters of a book on the screen. The number of the picture,
    /// -1 when it is not made.
    int takeInline(fz_rect region, float size)
    {
        if (!curPage || !binaries || size <= 0 || region.x1 - region.x0 < 2)
            return -1;
        const double t0 = nowMs();
        // (the boxes of the characters reach from the top of the tall letters to the bottom of the tails)
        region.x0 -= 1; region.x1 += 1; region.y0 -= size * 0.05f; region.y1 += size * 0.05f;
        float zoom = 34.0f / size;
        zoom = zoom < 1.5f ? 1.5f : zoom > 6 ? 6 : zoom;
        fz_pixmap * pix = NULL;
        fz_device * dev = NULL;
        fz_buffer * packed = NULL;
        unsigned char digest[16];
        unsigned char * data = NULL;
        size_t length = 0;
        fz_var(pix);
        fz_var(dev);
        fz_var(packed);
        fz_var(data);
        fz_var(length);
        fz_try(ctx) {
            const fz_matrix m = fz_scale(zoom, zoom);
            pix = fz_new_pixmap_with_bbox(ctx, fz_device_gray(ctx), fz_round_rect(fz_transform_rect(region, m)), NULL, 0);
            fz_clear_pixmap_with_value(ctx, pix, 255);
            dev = fz_new_draw_device(ctx, m, pix);
            fz_run_page(ctx, curPage, dev, fz_identity, NULL);
            fz_close_device(ctx, dev);
            packed = fz_new_buffer_from_pixmap_as_jpeg(ctx, pix, fz_default_color_params, 85, 0);
            fz_md5_buffer(ctx, packed, digest);
            length = fz_buffer_storage(ctx, packed, &data);
        }
        fz_always(ctx) {
            fz_drop_device(ctx, dev);
            fz_drop_pixmap(ctx, pix);
        }
        fz_catch(ctx) {
            data = NULL;
        }
        int number = -1;
        if (data && length > 0) {
            Picture picture;
            picture.page = page;
            picture.digest.assign((const char *)digest, 16);
            picture.large = false;
            number = (int)pictures.size();
            fprintf(binaries, "<binary id=\"i%d\" content-type=\"image/jpeg\">", number);
            writeBase64(binaries, data, length);
            fputs("</binary>\n", binaries);
            pictures.push_back(picture);
            stats.formulas++;
        }
        fz_drop_buffer(ctx, packed);
        stats.imageMs += nowMs() - t0;
        return number;
    }

    /// The characters of a line into the runs; the hyphen of a word the library has joined with
    /// the next line is left out. Returns true when the next line goes on without a space.
    /// sameRow: the line stands on the row of the line before it, to its right (the library gives
    /// the cells of a row as lines of their own).
    bool takeLine(fz_stext_line * line, Item & item, bool sameRow)
    {
        const bool level = line->wmode == 0 && line->dir.x > 0.99f;
        const float base = line->first_char ? line->first_char->origin.y : 0;
        const float firstSize = line->first_char ? line->first_char->size : 0;
        bool joined = false;
        float lastRight = sameRow ? item.lastX1 : -1e9f;
        const bool first = sameRow ? item.lines == 1 : item.lines == 0;
        if (!sameRow)
            item.lines++;
        // a formula in the line that the characters of a book cannot tell: a picture in its place
        // (a line is looked into only when it has such a sign: nearly none has)
        std::vector<Span> spans;
        bool big = false;
        if (level && curPage && firstSize > 0)
            for (fz_stext_char * ch = line->first_char; ch && !big; ch = ch->next)
                big = ch->c > 0x7F && isMath(ch) == 2 && bigSign(ch);
        // the fractions of the line (findStacked), in the order of its characters; a line that has
        // them is not looked into for other formulas
        std::vector<Span> inserts;
        bool fractions = false;
        for (size_t k = 0; k < stacks.size(); k++)
            fractions = fractions || stacks[k].host == line;
        if (fractions && level && curPage && firstSize > 0) {
            for (fz_stext_char * ch = line->first_char; ch; ch = ch->next)
                for (size_t k = 0; k < stacks.size(); k++)
                    if (stacks[k].host == line && stacks[k].span.from == ch)
                        spans.push_back(stacks[k].span);
            for (size_t k = 0; k < stacks.size(); k++)
                if (stacks[k].host == line && stacks[k].span.from == NULL)
                    inserts.push_back(stacks[k].span);
        } else if (big) {
            findFormulas(line, spans);
        }
        for (size_t s = 0; s < spans.size(); s++) {
            fz_rect region = fz_rect_from_quad(spans[s].from->quad);
            if (!fz_is_empty_rect(spans[s].region))
                region = fz_union_rect(region, spans[s].region);
            for (fz_stext_char * ch = spans[s].from; ch; ch = ch->next) {
                region = fz_union_rect(region, fz_rect_from_quad(ch->quad));
                if (ch == spans[s].to)
                    break;
            }
            spans[s].image = takeInline(region, firstSize);
        }
        for (size_t s = 0; s < inserts.size(); s++)
            inserts[s].image = takeInline(inserts[s].region, firstSize);
        size_t span = 0;
        bool inSpan = false;
        fz_stext_char * before = NULL;
        for (fz_stext_char * ch = line->first_char; ; before = ch, ch = ch->next) {
            // a picture that stands for no character of the line: after the character before it
            for (size_t s = 0; s < inserts.size(); s++)
                if (inserts[s].image >= 0 && inserts[s].after == before) {
                    char number[16];
                    snprintf(number, sizeof(number), "%d", inserts[s].image);
                    if (!endsWithSpace(item.runs))
                        addChar(item.runs, item.runs.back().style & ~(unsigned)S_IMAGE, ' ');
                    Run run;
                    run.style = S_IMAGE;
                    run.text = number;
                    item.runs.push_back(run);
                    item.chars++;
                    inserts[s].image = -1;
                }
            if (!ch)
                break;
            int c = ch->c;
            while (span < spans.size() && spans[span].image < 0)
                span++;
            if (span < spans.size() && (inSpan || ch == spans[span].from)) {
                if (!inSpan) {
                    char number[16];
                    snprintf(number, sizeof(number), "%d", spans[span].image);
                    Run run;
                    run.style = S_IMAGE;
                    run.text = number;
                    item.runs.push_back(run);
                    inSpan = true;
                }
                // The characters of a formula with a root or a sum count as signs of a formula: a
                // formula set on lines of its own is then taken whole (takeFigures). Those of a
                // fraction do not: the line around it is a text.
                item.chars++;
                if (!fractions)
                    item.mathChars++;
                lastRight = ch->quad.lr.x > ch->quad.ur.x ? ch->quad.lr.x : ch->quad.ur.x;
                item.lastX1 = lastRight;
                if (first)
                    item.rowX1 = lastRight;
                if (ch == spans[span].to) {
                    inSpan = false;
                    span++;
                }
                continue;
            }
            if (ch->next == NULL && (line->flags & FZ_STEXT_LINE_FLAGS_JOINED) != 0 && fz_is_unicode_hyphen(c)) {
                joined = true;
                continue;
            }
            if (c == 0xAD || c == 0xFEFF || c == 0x200B)
                continue;
            if (c < 32 || c == 0x7F)
                c = ' ';
            if (c == ' ') {
                if (!endsWithSpace(item.runs))
                    addChar(item.runs, item.runs.back().style & ~(unsigned)S_IMAGE, ' ');
                continue;
            }
            unsigned style = 0;
            if (ch->font) {
                if (fz_font_is_bold(ctx, ch->font) || (ch->flags & FZ_STEXT_BOLD) != 0)
                    style |= S_BOLD;
                if (fz_font_is_italic(ctx, ch->font))
                    style |= S_ITALIC;
                if (fz_font_is_monospaced(ctx, ch->font))
                    style |= S_MONO;
            }
            // raised above the line and smaller: a footnote mark, a power
            if (level && ch->origin.y < base - ch->size * 0.1f && ch != line->first_char)
                style |= S_SUP;
            // lowered and smaller: an index
            else if (level && ch->origin.y > base + ch->size * 0.12f && ch->size < firstSize * 0.85f)
                style |= S_SUB;
            const int math = isMath(ch);
            if (math == 2)
                item.mathChars++;
            else if (math == 1)
                item.weakMath++;
            if (level) {
                const float left = ch->quad.ll.x < ch->quad.ul.x ? ch->quad.ll.x : ch->quad.ul.x;
                const float right = ch->quad.lr.x > ch->quad.ur.x ? ch->quad.lr.x : ch->quad.ur.x;
                if (first) {
                    if (lastRight < -1e8f) {
                        item.cellX0.push_back(left);
                    } else if (left - lastRight > ch->size * 1.5f && !item.runs.empty()) {
                        // far from the piece before: the next cell of a row
                        std::string & tail = item.runs.back().text;
                        if (!tail.empty() && tail[tail.size() - 1] == ' ')
                            tail[tail.size() - 1] = '\t';
                        else
                            tail += '\t';
                        item.cellX1.push_back(lastRight);
                        item.cellX0.push_back(left);
                    }
                }
                lastRight = right;
                item.lastX1 = right;
                if (first)
                    item.rowX1 = right;
            }
            addChar(item.runs, style, c);
            item.chars++;
            if (style & S_BOLD)
                item.boldChars++;
            if (style & S_MONO)
                item.monoChars++;
            countLetter(c);
        }
        return joined;
    }

    fz_font * mathFontAsked;
    bool mathFontIs;

    /// 2: a sign of a formula (a mathematical sign, or any character of a font made for formulas);
    /// 1: a sign a formula has and a text may have.
    int isMath(fz_stext_char * ch)
    {
        const int c = ch->c;
        if ((c >= 0x2200 && c <= 0x22FF) || (c >= 0x2A00 && c <= 0x2AFF) || (c >= 0x1D400 && c <= 0x1D7FF)
                || (c >= 0x27C0 && c <= 0x27EF) || (c >= 0x2190 && c <= 0x21FF))
            return 2;
        if (c == '=' || c == '+' || c == 0xB1 || c == 0xD7 || c == 0xF7 || c == '<' || c == '>')
            return 1;
        if (!ch->font)
            return 0;
        if (ch->font != mathFontAsked) {
            mathFontAsked = ch->font;
            const char * name = fz_font_name(ctx, ch->font);
            mathFontIs = name && (strstr(name, "Math") || strstr(name, "CMMI") || strstr(name, "CMSY")
                                  || strstr(name, "CMEX") || strstr(name, "MSAM") || strstr(name, "MSBM")
                                  || strstr(name, "Symbol") || strstr(name, "MTSY") || strstr(name, "MTEX"));
        }
        return mathFontIs ? 2 : 0;
    }

    static float lineSize(fz_stext_line * line, int & count)
    {
        float sum = 0;
        count = 0;
        for (fz_stext_char * ch = line->first_char; ch; ch = ch->next)
            if (ch->c > 32) {
                sum += ch->size;
                count++;
            }
        return count ? sum / count : 0;
    }

    /// A row of five dots and more, with or without spaces between them, becomes an ellipsis.
    static void squeezeDots(std::string & t)
    {
        for (size_t i = 0; i < t.size(); i++) {
            if (t[i] != '.')
                continue;
            size_t j = i;
            int dots = 0;
            while (j < t.size() && (t[j] == '.' || t[j] == ' ')) {
                dots += t[j] == '.' ? 1 : 0;
                j++;
            }
            while (j > i && t[j - 1] == ' ')
                j--;
            if (dots >= 5) {
                t.replace(i, j - i, " \xE2\x80\xA6");
                i += 3;
            } else {
                i = j > i ? j - 1 : i;
            }
        }
    }

    void finish(Item & item)
    {
        // no space at the ends
        while (!item.runs.empty() && !(item.runs.back().style & S_IMAGE)) {
            std::string & t = item.runs.back().text;
            while (!t.empty() && t[t.size() - 1] == ' ')
                t.erase(t.size() - 1);
            if (!t.empty())
                break;
            item.runs.pop_back();
        }
        if (item.chars == 0 || item.runs.empty())
            return;
        for (size_t i = 0; i < item.runs.size(); i++)
            if (!(item.runs[i].style & S_IMAGE))
                squeezeDots(item.runs[i].text);
        // the last piece of the first line ends where the line ends
        if (item.cellX1.size() + 1 == item.cellX0.size())
            item.cellX1.push_back(item.rowX1);
        sizes[(int)lrintf(item.size * 2)] += item.chars;
        items.push_back(item);
    }

    void startItem(Item & item, const Tag & tag, float size)
    {
        item = Item();
        item.page = page;
        item.size = size;
        item.tagged = tag.tagged;
        item.quote = tag.quote;
        if (tag.heading > 0) {
            item.kind = K_HEADING;
            item.level = tag.heading;
        } else if (tag.note) {
            item.kind = K_NOTE;
        }
        item.x0 = item.y0 = 1e9f;
        item.x1 = item.y1 = -1e9f;
    }

    /// A text block: one paragraph, or several when the size of its lines changes (a heading
    /// set close to its text), or a line each when it is code.
    /// A row of a text block: a line with the pieces that stand to its right on it.
    struct TextRow {
        fz_stext_line * first;
        float x0, size;
        std::vector<float> starts;  // where its pieces begin: the first one, and each one set apart
        bool bold, endBold;         // of its first and of its last character
        bool spaced;                // has words with spaces between them
        int last;                   // its last character
        int firstC;                 // its first character
        float x1;                   // its right end
        float firstWord;            // the width of its first word
    };

    /// True when every piece of the row with fewer pieces begins where a piece of the other does.
    static bool sameColumns(const TextRow & a, const TextRow & b)
    {
        if (a.starts.size() < 2 || b.starts.size() < 2)
            return false;
        const TextRow & few = a.starts.size() <= b.starts.size() ? a : b;
        const TextRow & many = a.starts.size() <= b.starts.size() ? b : a;
        for (size_t i = 0; i < few.starts.size(); i++) {
            bool found = false;
            for (size_t k = 0; k < many.starts.size() && !found; k++)
                found = fabsf(few.starts[i] - many.starts[k]) < few.size * 0.6f;
            if (!found)
                return false;
        }
        return true;
    }

    /// The rows of a block, and which of them begin a paragraph of their own:
    ///  - the rows of a table without rules: pieces set apart, at the same places as those of the
    ///    row beside (each row then is a paragraph of one line, which plainTables makes a table of);
    ///  - the entries of a list whose first lines hang out to the left (a glossary, a list of
    ///    books), when the block begins with such a line and has as many lines set in as not;
    ///  - an entry that begins in bold after a sentence that does not end in bold, in a block
    ///    that begins in bold itself.
    void findRows(fz_stext_block * block, std::vector<TextRow> & rows, std::vector<bool> & begins)
    {
        fz_rect prev = fz_empty_rect;
        float lastRight = -1e9f;
        for (fz_stext_line * line = block->u.t.first_line; line; line = line->next) {
            int count;
            const float size = lineSize(line, count);
            if (count == 0 || isStacked(line))
                continue;
            const bool same = !rows.empty() && line->bbox.x0 > prev.x1 - size * 0.2f
                && fabsf((line->bbox.y0 + line->bbox.y1) - (prev.y0 + prev.y1)) < size * 0.6f;
            prev = line->bbox;
            if (!same) {
                TextRow row;
                row.first = line;
                row.x0 = line->bbox.x0;
                row.size = size;
                row.bold = row.endBold = row.spaced = false;
                row.last = 0;
                row.firstC = 0;
                row.x1 = line->bbox.x0;
                row.firstWord = 0;
                rows.push_back(row);
                lastRight = -1e9f;
            }
            TextRow & row = rows.back();
            for (fz_stext_char * ch = line->first_char; ch; ch = ch->next) {
                if (ch->c <= 32) {
                    row.spaced = row.spaced || row.last != 0;
                    continue;
                }
                const float left = ch->quad.ll.x < ch->quad.ul.x ? ch->quad.ll.x : ch->quad.ul.x;
                const float right = ch->quad.lr.x > ch->quad.ur.x ? ch->quad.lr.x : ch->quad.ur.x;
                if (lastRight < -1e8f || left - lastRight > ch->size * 1.5f)
                    row.starts.push_back(left);
                const bool bold = ch->font && (fz_font_is_bold(ctx, ch->font) || (ch->flags & FZ_STEXT_BOLD) != 0);
                if (row.last == 0) {
                    row.bold = bold;
                    row.firstC = ch->c;
                }
                if (!row.spaced)
                    row.firstWord = right - row.x0;
                row.endBold = bold;
                row.last = ch->c;
                row.x1 = right;
                lastRight = right;
            }
        }
        begins.assign(rows.size(), false);
        if (rows.size() < 2)
            return;
        float least = 1e9f;
        for (size_t r = 0; r < rows.size(); r++)
            least = std::min(least, rows[r].x0);
        int setIn = 0, setOut = 0;
        for (size_t r = 0; r < rows.size(); r++)
            (rows[r].x0 > least + rows[r].size * 0.8f ? setIn : setOut)++;
        const bool hanging = rows[0].x0 <= least + rows[0].size * 0.8f && setOut >= 2 && setIn >= setOut;
        // a block with a usual line of several words is a text, all of it: the pieces set apart
        // in it are the words of a line spread wide (a narrow justified column)
        bool text = false;
        float edge = -1e9f;         // the right edge of the block
        for (size_t r = 0; r < rows.size(); r++) {
            text = text || (rows[r].starts.size() == 1 && rows[r].spaced);
            edge = std::max(edge, rows[r].x1);
        }
        for (size_t r = 0; r < rows.size(); r++) {
            const bool cells = !text && ((r > 0 && sameColumns(rows[r - 1], rows[r]))
                                         || (r + 1 < rows.size() && sameColumns(rows[r], rows[r + 1])));
            if (cells) {
                begins[r] = true;
                if (r + 1 < rows.size())
                    begins[r + 1] = true;
            }
            if (r == 0)
                continue;
            if (hanging && rows[r].x0 <= least + rows[r].size * 0.8f)
                begins[r] = true;
            if (rows[0].bold && rows[r].bold && !rows[r - 1].endBold && endsSentence(rows[r - 1].last))
                begins[r] = true;
            if (pageHanging && rows[r].x0 <= pageOut + rows[r].size * 0.8f && endsSentence(rows[r - 1].last)
                    && !isLower(rows[r].firstC))
                begins[r] = true;
            // the row before ends a sentence, and the first word of this one would have fitted
            // after it: the paragraph ended there (an entry of one line before the next entry)
            if (endsSentence(rows[r - 1].last) && !isLower(rows[r].firstC)
                    && rows[r - 1].x1 + rows[r].firstWord + rows[r].size * 0.6f < edge)
                begins[r] = true;
        }
    }

    /// A fraction inside a line of text: the line it stands in and what is cut out as its picture.
    struct Stack {
        fz_stext_line * host;
        Span span;
    };
    std::vector<Stack> stacks;

    void collectLines(fz_stext_block * block, std::vector<fz_stext_line *> & lines, int depth)
    {
        for (; block && depth < 16; block = block->next) {
            if (block->type == FZ_STEXT_BLOCK_TEXT) {
                for (fz_stext_line * line = block->u.t.first_line; line; line = line->next)
                    lines.push_back(line);
            } else if (block->type == FZ_STEXT_BLOCK_STRUCT && block->u.s.down) {
                collectLines(block->u.s.down->first_block, lines, depth + 1);
            }
        }
    }

    /// True when the page is a list of entries whose first lines hang out to the left (a glossary,
    /// a list of books), with pageOut the margin the entries begin at. A line set in from the
    /// margin is the second line of an entry when the line above it is full, and the first line
    /// of a paragraph when the line above it is short: the page is such a list when the first
    /// is seen three times and far more often than the second.
    bool pageHanging;
    float pageOut;

    void findStyle(const std::vector<fz_stext_line *> & lines, const std::vector<int> & counts,
                   const std::vector<float> & sizes)
    {
        float left = 1e9f, right = -1e9f;
        for (size_t i = 0; i < lines.size(); i++)
            if (counts[i] >= 20) {
                left = std::min(left, lines[i]->bbox.x0);
                right = std::max(right, lines[i]->bbox.x1);
            }
        int full = 0, shortAbove = 0;
        size_t above = (size_t)-1;
        for (size_t i = 0; i < lines.size(); i++) {
            if (counts[i] < 5)
                continue;
            const fz_rect b = lines[i]->bbox;
            const float size = sizes[i];
            if (above != (size_t)-1 && b.x0 > left + size * 0.8f && b.x0 < left + size * 5) {
                const fz_rect a = lines[above]->bbox;
                if (b.y0 > a.y0 && b.y0 - a.y0 < size * 1.8f && a.x0 < b.x0 - size * 0.8f) {
                    // full: the first word of this line would not have fitted after it
                    float word = 0;
                    bool begun = false;
                    for (fz_stext_char * ch = lines[i]->first_char; ch; ch = ch->next) {
                        if (ch->c <= 32 && begun)
                            break;
                        if (ch->c > 32) {
                            begun = true;
                            word = (ch->quad.lr.x > ch->quad.ur.x ? ch->quad.lr.x : ch->quad.ur.x) - b.x0;
                        }
                    }
                    (a.x1 + word + size * 0.3f > right ? full : shortAbove)++;
                }
            }
            above = i;
        }
        if (getenv("PB_PDFTEXT_LINES") && atoi(getenv("PB_PDFTEXT_LINES")) == page + 1)
            fprintf(stderr, "style: lines set in under a full line %d, under a short line %d; margin %.0f - %.0f\n",
                    full, shortAbove, left, right);
        // (a list goes on over pages: after a page of it one such line is enough, since a page
        // of entries of one line has few lines set in)
        const bool goesOn = !pages.empty() && pages.back().hanging;
        pageHanging = full >= (goesOn ? 1 : 3) && full > shortAbove * 2;
        pageOut = left;
    }

    /// Finds the parts of fractions set inside lines of text: a short line (a numerator, a
    /// denominator) that stands inside the width of a long line of text, a little above or under
    /// its row, where that line has no characters of its own row. What the long line has there
    /// off its row (a numerator the library has taken into the line) goes into the picture too.
    void findStacked(fz_stext_block * blocks)
    {
        stacked.clear();
        stacks.clear();
        pageHanging = false;
        std::vector<fz_stext_line *> lines;
        collectLines(blocks, lines, 0);
        if (lines.size() < 2 || lines.size() > 3000)
            return;
        std::vector<int> counts(lines.size(), 0);
        std::vector<float> sizes(lines.size(), 0);
        // a line of words: most of it letters, with a word of four letters in it (a formula set
        // on lines of its own is not one, and stays one picture with its parts)
        std::vector<bool> wordy(lines.size(), false);
        for (size_t i = 0; i < lines.size(); i++) {
            sizes[i] = lineSize(lines[i], counts[i]);
            int letters = 0, run = 0, longest = 0;
            for (fz_stext_char * ch = lines[i]->first_char; ch; ch = ch->next) {
                run = isLetter(ch->c) ? run + 1 : 0;
                letters += isLetter(ch->c) ? 1 : 0;
                longest = run > longest ? run : longest;
            }
            wordy[i] = longest >= 4 && letters * 10 >= counts[i] * 6;
        }
        findStyle(lines, counts, sizes);
        for (size_t s = 0; s < lines.size(); s++) {
            fz_stext_line * part = lines[s];
            if (counts[s] == 0 || counts[s] > 12 || part->wmode != 0 || part->dir.x < 0.99f)
                continue;
            const fz_rect pb = part->bbox;
            const float py = (pb.y0 + pb.y1) * 0.5f;
            for (size_t m = 0; m < lines.size(); m++) {
                fz_stext_line * host = lines[m];
                const float size = sizes[m];
                if (m == s || counts[m] < 15 || host->wmode != 0 || host->dir.x < 0.99f || sizes[s] > size * 1.05f
                        || !wordy[m])
                    continue;
                const fz_rect hb = host->bbox;
                // inside the width of the line or right after its end; near its row and not on it
                // (a piece of the row), and nearer than the lines above and under it
                if (pb.x0 < hb.x0 + size || pb.x0 > hb.x1 + size * 3 || py < hb.y0 - size * 0.45f || py > hb.y1 + size * 0.45f)
                    continue;
                const float base = host->first_char->origin.y;
                if (fabsf(part->first_char->origin.y - base) < size * 0.25f)
                    continue;
                // the characters of the line over and under the part: all off the row, or none
                const float firstSize = host->first_char->size;
                fz_rect region = pb;
                fz_stext_char * from = NULL, * to = NULL, * after = NULL;
                bool onRow = false;
                for (fz_stext_char * ch = host->first_char; ch; ch = ch->next) {
                    if (ch->c <= 32)
                        continue;
                    const fz_rect q = fz_rect_from_quad(ch->quad);
                    if (q.x1 <= pb.x0 - size * 0.5f) {
                        after = ch;
                        continue;
                    }
                    if (q.x0 >= pb.x1 + size * 0.5f)
                        break;
                    if (fabsf(ch->origin.y - base) > size * 0.15f || ch->size < firstSize * 0.9f) {
                        if (!from)
                            from = ch;
                        to = ch;
                        region = fz_union_rect(region, q);
                    } else if (q.x0 < pb.x1 && q.x1 > pb.x0) {
                        onRow = true;
                    } else if (!from) {
                        after = ch;
                    }
                }
                if (onRow)
                    continue;
                stacked.push_back(part);
                // a second part of the same fraction (the numerator, then the denominator)
                bool merged = false;
                for (size_t k = 0; k < stacks.size() && !merged; k++) {
                    Stack & have = stacks[k];
                    if (have.host != host || have.span.region.x0 > region.x1 + size || have.span.region.x1 < region.x0 - size)
                        continue;
                    have.span.region = fz_union_rect(have.span.region, region);
                    if (from && !have.span.from) {
                        have.span.from = from;
                        have.span.to = to;
                    }
                    merged = true;
                }
                if (!merged) {
                    Stack stack;
                    stack.host = host;
                    stack.span.from = from;
                    stack.span.to = to;
                    stack.span.after = after;
                    stack.span.region = region;
                    stack.span.image = -1;
                    stacks.push_back(stack);
                }
                break;
            }
        }
    }

    /// The lines of the page that are parts of a fraction set inside a line of text: they stand
    /// above or under that line and are shown in a picture in it (findStacked).
    std::vector<fz_stext_line *> stacked;
    bool isStacked(fz_stext_line * line) const
    {
        for (size_t i = 0; i < stacked.size(); i++)
            if (stacked[i] == line)
                return true;
        return false;
    }

    /// True when the text ends as a line of a contents page does: a row of dots, then a number.
    static bool endsAsEntry(const Runs & runs)
    {
        const std::string text = textOf(runs);
        size_t at = text.size();
        while (at > 0 && (text[at - 1] == ' ' || text[at - 1] == '\t'))
            at--;
        const size_t end = at;
        while (at > 0 && end - at < 8 && strchr("0123456789ivxlcIVXLC", text[at - 1]))
            at--;
        if (at == end)
            return false;
        int dots = 0;
        while (at > 0 && (text[at - 1] == '.' || text[at - 1] == ' ' || text[at - 1] == '\t')) {
            dots += text[at - 1] == '.' ? 1 : 0;
            at--;
        }
        return dots >= 4;
    }

    void takeText(fz_stext_block * block, const Tag & tag)
    {
        // for the PC tool: the lines of the block as the library gives them
        if (getenv("PB_PDFTEXT_LINES") && atoi(getenv("PB_PDFTEXT_LINES")) == page + 1) {
            fprintf(stderr, "block %.0f,%.0f - %.0f,%.0f\n", block->bbox.x0, block->bbox.y0, block->bbox.x1, block->bbox.y1);
            for (fz_stext_line * line = block->u.t.first_line; line; line = line->next) {
                std::string s;
                for (fz_stext_char * ch = line->first_char; ch; ch = ch->next)
                    appendUtf8(s, ch->c);
                fprintf(stderr, "  line %.0f,%.0f - %.0f,%.0f: %.70s\n", line->bbox.x0, line->bbox.y0, line->bbox.x1,
                        line->bbox.y1, s.c_str());
            }
        }
        // code keeps its lines
        long mono = 0, all = 0;
        for (fz_stext_line * line = block->u.t.first_line; line; line = line->next)
            for (fz_stext_char * ch = line->first_char; ch; ch = ch->next)
                if (ch->c > 32) {
                    all++;
                    if (tag.mono || (ch->font && fz_font_is_monospaced(ctx, ch->font)))
                        mono++;
                }
        const bool code = all > 0 && mono * 10 >= all * 9;
        // Verse, and any list set a line to an entry: three lines or more, each starting with a
        // capital letter (in a paragraph of prose the lines start where the words fall), none
        // ending in a divided word.
        bool verse = !code && tag.heading == 0;
        int verseLines = 0;
        fz_rect above = fz_empty_rect;
        for (fz_stext_line * line = block->u.t.first_line; line && verse; line = line->next) {
            fz_stext_char * ch = line->first_char;
            while (ch && ch->c <= 32)
                ch = ch->next;
            if (!ch)
                continue;
            // a piece on the row of the line before (a cell of a row) is not a line
            const bool sameRow = verseLines > 0 && line->bbox.x0 > above.x1 - ch->size * 0.2f
                && fabsf((line->bbox.y0 + line->bbox.y1) - (above.y0 + above.y1)) < ch->size * 0.6f;
            above = line->bbox;
            if (sameRow) {
                verse = false;
                break;
            }
            verseLines++;
            int c = ch->c;
            if ((c == 0x2014 || c == 0x2013 || c == 0xAB || c == '"' || c == '(') && ch->next)
                c = ch->next->c == ' ' && ch->next->next ? ch->next->next->c : ch->next->c;
            if (!isLetter(c) || isLower(c) || (line->flags & FZ_STEXT_LINE_FLAGS_JOINED) != 0)
                verse = false;
        }
        // (the entries of a list that have a line each are not verse)
        verse = verse && verseLines >= 3 && !pageHanging;
        // The rows of the block that begin a paragraph of their own though the library gives the
        // block as one: the rows of a table without rules, the entries of a glossary.
        std::vector<TextRow> rows;
        std::vector<bool> begins;
        // (PB_PDFTEXT_NOROWS, for the PC tool: without it, to see what it changes in a book)
        if (!code && !verse && tag.heading == 0 && !getenv("PB_PDFTEXT_NOROWS"))
            findRows(block, rows, begins);
        size_t rowAt = 0;
        Item item;
        bool open = false, glue = false;
        fz_rect before = fz_empty_rect;
        for (fz_stext_line * line = block->u.t.first_line; line; line = line->next) {
            int count;
            const float size = lineSize(line, count);
            // (the parts of a fraction that stand above and under a line are in the picture of it)
            if (count == 0 || isStacked(line))
                continue;
            // on the row of the line before, to its right
            const bool sameRow = open && line->bbox.x0 > before.x1 - size * 0.2f
                && fabsf((line->bbox.y0 + line->bbox.y1) - (before.y0 + before.y1)) < size * 0.6f;
            before = line->bbox;
            if (rowAt + 1 < rows.size() && rows[rowAt + 1].first == line)
                rowAt++;
            const bool ownRow = !sameRow && rowAt < rows.size() && rows[rowAt].first == line && begins[rowAt];
            // (a line of a contents page, a title with a row of dots and a number, is a paragraph)
            if (open && !sameRow
                    && (code || verse || (tag.heading == 0 && fabsf(size - item.size) > item.size * 0.12f)
                        || endsAsEntry(item.runs) || ownRow)) {
                finish(item);
                open = false;
            }
            if (!open) {
                startItem(item, tag, size);
                item.verse = verse;
                item.firstX = line->bbox.x0;
                open = true;
            } else {
                if (!sameRow && line->bbox.x0 < item.restX)
                    item.restX = line->bbox.x0;
                if (!glue && !endsWithSpace(item.runs))
                    addChar(item.runs, item.runs.back().style & ~(unsigned)S_IMAGE, ' ');
            }
            glue = takeLine(line, item, sameRow);
            if (line->bbox.x0 < item.x0) item.x0 = line->bbox.x0;
            if (line->bbox.y0 < item.y0) item.y0 = line->bbox.y0;
            if (line->bbox.x1 > item.x1) item.x1 = line->bbox.x1;
            if (line->bbox.y1 > item.y1) item.y1 = line->bbox.y1;
        }
        if (open)
            finish(item);
    }

    /// The text under a block of the tree, as one run of runs (a cell of a table).
    void gather(fz_stext_block * block, Item & into)
    {
        for (; block; block = block->next) {
            if (block->type == FZ_STEXT_BLOCK_TEXT) {
                bool glue = true;
                for (fz_stext_line * line = block->u.t.first_line; line; line = line->next) {
                    if (!glue && !endsWithSpace(into.runs))
                        addChar(into.runs, into.runs.back().style, ' ');
                    glue = takeLine(line, into, false);
                }
                if (!endsWithSpace(into.runs))
                    addChar(into.runs, into.runs.back().style, ' ');
            } else if (block->type == FZ_STEXT_BLOCK_STRUCT && block->u.s.down) {
                gather(block->u.s.down->first_block, into);
            }
        }
    }

    static void gridPos(fz_stext_grid_positions * xs, float a, float b, int & ia, int & ib)
    {
        int i;
        ia = -1;
        ib = -1;
        for (i = 1; i < xs->len; i++)
            if (a < xs->list[i].pos) {
                ia = i - 1;
                break;
            }
        for (; i < xs->len; i++)
            if (b < xs->list[i].pos) {
                ib = i - 1;
                break;
            }
        if (i == xs->len)
            ib = i - 1;
    }

    /// A table the library has found: rows of cells with their spans, read off its grid as the
    /// library itself does for HTML. False when it is not a table worth the name: its blocks are
    /// then taken as text.
    bool takeTable(fz_stext_block * first, const fz_rect & box)
    {
        fz_stext_block * grid = first;
        while (grid && grid->type != FZ_STEXT_BLOCK_GRID)
            grid = grid->next;
        if (!grid || !grid->u.b.xs || !grid->u.b.ys)
            return false;
        const int w = grid->u.b.xs->len, h = grid->u.b.ys->len;
        if (w < 2 || h < 2 || w > 64 || h > 2000)
            return false;
        std::vector<unsigned char> taken((size_t)w * h, 0);
        Item table;
        table.kind = K_TABLE;
        table.page = page;
        table.x0 = box.x0; table.y0 = box.y0; table.x1 = box.x1; table.y1 = box.y1;
        int y = 0, filled = 0;
        for (fz_stext_block * tr = grid->next; tr && y < h; tr = tr->next) {
            if (tr->type != FZ_STEXT_BLOCK_STRUCT || !tr->u.s.down || tr->u.s.down->standard != FZ_STRUCTURE_TR)
                continue;
            std::vector<Cell> row;
            int x = 0;
            for (fz_stext_block * td = tr->u.s.down->first_block; td; td = td->next) {
                if (td->type != FZ_STEXT_BLOCK_STRUCT || !td->u.s.down || td->u.s.down->standard != FZ_STRUCTURE_TD)
                    continue;
                int cx0, cx1, cy0, cy1;
                gridPos(grid->u.b.xs, td->bbox.x0, td->bbox.x1, cx0, cx1);
                gridPos(grid->u.b.ys, td->bbox.y0, td->bbox.y1, cy0, cy1);
                if (cx0 < 0 || cx1 < 0 || cx1 >= w) {
                    cx0 = x;
                    cx1 = x + 1;
                }
                if (cy0 < 0 || cy1 < 0 || cy1 >= h) {
                    cy0 = y;
                    cy1 = y + 1;
                }
                if (cx1 <= cx0)
                    cx1 = cx0 + 1;
                if (cy1 <= cy0)
                    cy1 = cy0 + 1;
                if (cx0 >= w || y < cy0)
                    continue;
                for (; x < cx0; x++)
                    if (!taken[(size_t)y * w + x]) {
                        row.push_back(Cell());
                        taken[(size_t)y * w + x] = 1;
                    }
                Cell cell;
                cell.colspan = cx1 - cx0;
                cell.rowspan = cy1 - cy0;
                Item text;
                gather(td->u.s.down->first_block, text);
                while (!text.runs.empty() && !text.runs.back().text.empty()
                       && text.runs.back().text[text.runs.back().text.size() - 1] == ' ')
                    text.runs.back().text.erase(text.runs.back().text.size() - 1);
                cell.runs = text.runs;
                if (text.chars > 0)
                    filled++;
                table.chars += text.chars;
                row.push_back(cell);
                for (int yy = cy0; yy < cy1 && yy < h; yy++)
                    for (int xx = cx0; xx < cx1 && xx < w; xx++)
                        taken[(size_t)yy * w + xx] = 1;
                x = cx1;
            }
            table.rows.push_back(row);
            y++;
        }
        // One row, text in a few cells only, most rows with one cell, or cells that hold whole
        // paragraphs: the hunt took a layout for a table. Its blocks are then read as text.
        int rowsOfSeveral = 0, rowsOfDots = 0;
        size_t longest = 0;
        for (size_t r = 0; r < table.rows.size(); r++) {
            int withText = 0;
            bool dots = false;
            for (size_t c = 0; c < table.rows[r].size(); c++) {
                const std::string cellText = textOf(table.rows[r][c].runs);
                dots = dots || cellText.find("....") != std::string::npos || cellText.find(". . . .") != std::string::npos;
                const size_t length = cellText.size();
                if (length > 0)
                    withText++;
                if (length > longest)
                    longest = length;
            }
            if (withText >= 2)
                rowsOfSeveral++;
            if (dots)
                rowsOfDots++;
        }
        // (a contents page: the titles and the page numbers stand in columns, with rows of dots)
        if (table.rows.size() < 3 || filled < 4 || rowsOfSeveral * 2 < (int)table.rows.size() || longest > 700
                || table.chars > filled * 160 || rowsOfDots * 3 >= (int)table.rows.size())
            return false;
        items.push_back(table);
        return true;
    }

    /// True for what lies whole in the top or the bottom eleventh of the page, where the running
    /// titles are.
    bool inMargin(const fz_rect & r) const
    {
        return pageH > 0 && (r.y1 - pageTop <= pageH * 0.09f || r.y0 - pageTop >= pageH * 0.91f);
    }

    /// A picture of the page: decoded no larger than the screen needs it, gray, JPEG, written to
    /// the file of the pictures at once.
    void takeImage(fz_stext_block * block)
    {
        fz_image * image = block->u.i.image;
        const fz_matrix m = block->u.i.transform;
        const float w = sqrtf(m.a * m.a + m.b * m.b), h = sqrtf(m.c * m.c + m.d * m.d);
        if (!image || image->imagemask || image->w < 24 || image->h < 24 || pageW <= 0)
            return;
        // What counts as a picture of the book, by its size on paper, its shape and its place: at
        // least 12 mm wide and 7 mm high (34 and 20 points), not a narrow strip (a rule drawn as a
        // picture), and not a small thing that lies whole in the top or the bottom margin (the
        // mark of a publisher, the ornament of a running title).
        if (w < 34 || h < 20 || w > 12 * h || h > 12 * w)
            return;
        if (inMargin(block->bbox))
            return;
        const double t0 = nowMs();
        // as wide on the screen as on the page; the screen is 1404 pixels wide at the most
        int target = (int)lrintf(w / pageW * 1404);
        if (target > image->w)
            target = image->w;
        if (target < 96)
            target = image->w < 96 ? image->w : 96;
        int targetH = (int)lrintf((float)target * image->h / image->w);
        if (targetH < 1)
            targetH = 1;
        fz_pixmap * pix = NULL;
        fz_pixmap * gray = NULL;
        fz_pixmap * small = NULL;
        fz_buffer * packed = NULL;
        unsigned char digest[16];
        unsigned char * data = NULL;
        size_t length = 0;
        fz_var(pix);
        fz_var(gray);
        fz_var(small);
        fz_var(packed);
        fz_var(data);
        fz_var(length);
        // A JPEG of the file that is gray or RGB and not much larger than it is shown goes into
        // the book as it is: decoding it, making it gray and packing it again took 8 ms a picture
        // on the PC, half a second on a reader, and lost a little of it.
        bool asIs = false;
        fz_var(asIs);
        fz_try(ctx) {
            fz_compressed_buffer * compressed = fz_compressed_image_buffer(ctx, image);
            if (compressed && compressed->buffer && compressed->params.type == FZ_IMAGE_JPEG && image->mask == NULL
                    && !image->use_decode && !image->use_colorkey && image->colorspace
                    && fz_colorspace_n(ctx, image->colorspace) != 4 && image->orientation <= 1
                    && image->w <= target + target * 6 / 10) {
                fz_md5_buffer(ctx, compressed->buffer, digest);
                length = fz_buffer_storage(ctx, compressed->buffer, &data);
                asIs = length > 0;
            }
        }
        fz_catch(ctx) {
            asIs = false;
        }
        // (fz_try, fz_always and fz_catch are three statements: they stand in a block of their own)
        if (!asIs) {
        fz_try(ctx) {
            fz_matrix want = fz_scale((float)target, (float)targetH);
            pix = fz_get_pixmap_from_image(ctx, image, NULL, &want, NULL, NULL);
            if (fz_pixmap_colorspace(ctx, pix) == fz_device_gray(ctx) && !fz_pixmap_alpha(ctx, pix))
                gray = fz_keep_pixmap(ctx, pix);
            else
                gray = fz_convert_pixmap(ctx, pix, fz_device_gray(ctx), NULL, NULL, fz_default_color_params, 0);
            if (fz_pixmap_width(ctx, gray) > target + target / 3) {
                small = fz_scale_pixmap(ctx, gray, 0, 0, (float)target, (float)targetH, NULL);
                if (small) {
                    fz_drop_pixmap(ctx, gray);
                    gray = small;
                    small = NULL;
                }
            }
            packed = fz_new_buffer_from_pixmap_as_jpeg(ctx, gray, fz_default_color_params, 80, 0);
            fz_md5_buffer(ctx, packed, digest);
            length = fz_buffer_storage(ctx, packed, &data);
        }
        fz_always(ctx) {
            fz_drop_pixmap(ctx, pix);
            fz_drop_pixmap(ctx, gray);
            fz_drop_pixmap(ctx, small);
        }
        fz_catch(ctx) {
            data = NULL;
        }
        }
        if (data && length > 0 && binaries) {
            Picture picture;
            picture.page = page;
            picture.digest.assign((const char *)digest, 16);
            picture.large = (block->bbox.x1 - block->bbox.x0) * (block->bbox.y1 - block->bbox.y0) > pageW * pageH * 0.6f;
            fprintf(binaries, "<binary id=\"i%d\" content-type=\"image/jpeg\">", (int)pictures.size());
            writeBase64(binaries, data, length);
            fputs("</binary>\n", binaries);
            Item item;
            item.kind = K_IMAGE;
            item.page = page;
            item.image = (int)pictures.size();
            item.x0 = block->bbox.x0; item.y0 = block->bbox.y0; item.x1 = block->bbox.x1; item.y1 = block->bbox.y1;
            pictures.push_back(picture);
            items.push_back(item);
            if (asIs)
                stats.asIs++;
        }
        fz_drop_buffer(ctx, packed);
        stats.imageMs += nowMs() - t0;
    }

    static void writeBase64(FILE * f, const unsigned char * data, size_t length)
    {
        static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        char line[77];
        int at = 0;
        for (size_t i = 0; i < length; i += 3) {
            const unsigned a = data[i], b = i + 1 < length ? data[i + 1] : 0, c = i + 2 < length ? data[i + 2] : 0;
            line[at++] = alphabet[a >> 2];
            line[at++] = alphabet[((a & 3) << 4) | (b >> 4)];
            line[at++] = i + 1 < length ? alphabet[((b & 15) << 2) | (c >> 6)] : '=';
            line[at++] = i + 2 < length ? alphabet[c & 63] : '=';
            if (at >= 76) {
                fwrite(line, 1, at, f);
                fputc('\n', f);
                at = 0;
            }
        }
        if (at > 0)
            fwrite(line, 1, at, f);
    }

    void walk(fz_stext_block * block, const Tag & tag)
    {
        for (; block; block = block->next) {
            if (block->type == FZ_STEXT_BLOCK_TEXT) {
                takeText(block, tag);
            } else if (block->type == FZ_STEXT_BLOCK_IMAGE) {
                takeImage(block);
            } else if (block->type == FZ_STEXT_BLOCK_STRUCT && block->u.s.down) {
                fz_stext_struct * s = block->u.s.down;
                // what the file itself calls not part of the text: running titles, page numbers
                if (s->standard == FZ_STRUCTURE_ARTIFACT)
                    continue;
                if (s->standard == FZ_STRUCTURE_TABLE && takeTable(s->first_block, block->bbox))
                    continue;
                Tag inner = tag;
                if (s->standard >= FZ_STRUCTURE_H1 && s->standard <= FZ_STRUCTURE_H6) {
                    inner.heading = s->standard - FZ_STRUCTURE_H1 + 1;
                    inner.tagged = true;
                } else if (s->standard == FZ_STRUCTURE_H || s->standard == FZ_STRUCTURE_TITLE) {
                    inner.heading = 1;
                    inner.tagged = true;
                } else if (s->standard == FZ_STRUCTURE_NOTE) {
                    inner.note = true;
                    inner.tagged = true;
                } else if (s->standard == FZ_STRUCTURE_CODE) {
                    inner.mono = true;
                } else if (s->standard == FZ_STRUCTURE_BLOCKQUOTE) {
                    inner.quote = true;
                }
                walk(s->first_block, inner);
            }
        }
    }

    static bool overlapX(const Item & a, const Item & b)
    {
        const float lo = a.x0 > b.x0 ? a.x0 : b.x0, hi = a.x1 < b.x1 ? a.x1 : b.x1;
        const float wa = a.x1 - a.x0, wb = b.x1 - b.x0;
        return hi - lo > (wa < wb ? wa : wb) * 0.5f;
    }

    /// The library gives the blocks in the order the file draws them, column by column when it
    /// has segmented the page. A file may draw a footnote or a running title first: an item that
    /// lies wholly below a later one of the same column goes after it.
    void readingOrder(size_t first)
    {
        const size_t n = items.size() - first;
        if (n < 2 || n > 400)
            return;
        for (size_t guard = 0; guard < n * n; guard++) {
            bool moved = false;
            for (size_t i = first; i < items.size() && !moved; i++) {
                size_t after = 0;
                for (size_t j = i + 1; j < items.size(); j++)
                    if (items[i].y0 >= items[j].y1 - 1.0f && overlapX(items[i], items[j]))
                        after = j;
                if (after == 0)
                    continue;
                Item item = items[i];
                for (size_t k = i; k < after; k++)
                    items[k] = items[k + 1];
                items[after] = item;
                moved = true;
            }
            if (!moved)
                break;
        }
    }

    void collectVectors(fz_stext_block * block, std::vector<fz_rect> & boxes)
    {
        for (; block; block = block->next) {
            if (block->type == FZ_STEXT_BLOCK_VECTOR)
                boxes.push_back(block->bbox);
            else if (block->type == FZ_STEXT_BLOCK_STRUCT && block->u.s.down)
                collectVectors(block->u.s.down->first_block, boxes);
        }
    }

    /// A part of the page as a picture, at the zoom that makes the page as wide as the screen.
    bool takeRegion(fz_page * p, fz_rect region, const fz_rect & box)
    {
        region.x0 -= 4; region.y0 -= 4; region.x1 += 4; region.y1 += 4;
        region = fz_intersect_rect(region, box);
        if (region.x1 - region.x0 < 8 || region.y1 - region.y0 < 8 || pageW <= 0)
            return false;
        const double t0 = nowMs();
        float zoom = 1404.0f / pageW;
        if ((region.y1 - region.y0) * zoom > 2200)
            zoom = 2200 / (region.y1 - region.y0);
        fz_pixmap * pix = NULL;
        fz_device * dev = NULL;
        fz_buffer * packed = NULL;
        unsigned char digest[16];
        unsigned char * data = NULL;
        size_t length = 0;
        fz_var(pix);
        fz_var(dev);
        fz_var(packed);
        fz_var(data);
        fz_var(length);
        fz_try(ctx) {
            const fz_matrix m = fz_scale(zoom, zoom);
            pix = fz_new_pixmap_with_bbox(ctx, fz_device_gray(ctx), fz_round_rect(fz_transform_rect(region, m)), NULL, 0);
            fz_clear_pixmap_with_value(ctx, pix, 255);
            dev = fz_new_draw_device(ctx, m, pix);
            fz_run_page(ctx, p, dev, fz_identity, NULL);
            fz_close_device(ctx, dev);
            packed = fz_new_buffer_from_pixmap_as_jpeg(ctx, pix, fz_default_color_params, 85, 0);
            fz_md5_buffer(ctx, packed, digest);
            length = fz_buffer_storage(ctx, packed, &data);
        }
        fz_always(ctx) {
            fz_drop_device(ctx, dev);
            fz_drop_pixmap(ctx, pix);
        }
        fz_catch(ctx) {
            data = NULL;
        }
        const bool ok = data && length > 0 && binaries;
        if (ok) {
            Picture picture;
            picture.page = page;
            picture.digest.assign((const char *)digest, 16);
            picture.large = false;
            fprintf(binaries, "<binary id=\"i%d\" content-type=\"image/jpeg\">", (int)pictures.size());
            writeBase64(binaries, data, length);
            fputs("</binary>\n", binaries);
            Item item;
            item.kind = K_IMAGE;
            item.page = page;
            item.image = (int)pictures.size();
            item.x0 = region.x0; item.y0 = region.y0; item.x1 = region.x1; item.y1 = region.y1;
            pictures.push_back(picture);
            items.push_back(item);
        }
        fz_drop_buffer(ctx, packed);
        stats.imageMs += nowMs() - t0;
        return ok;
    }

    /// What is drawn with lines and shapes (a chart, a scheme) and what is set as a formula cannot
    /// be told in the words of a book: that part of the page becomes a picture, and the text that
    /// lies in it (the labels of a drawing) is in the picture.
    void takeFigures(fz_page * p, fz_stext_block * blocks, const fz_rect & box, size_t first)
    {
        const size_t end = items.size();
        // A formula set on lines of its own: a short piece of text made of signs of formulas, and
        // the short pieces close around it (the left side, the numerator and the denominator of
        // a fraction, the number of the formula are pieces of their own).
        for (size_t i = first; i < end; i++) {
            const Item & seed = items[i];
            if (seed.drop || seed.kind != K_PARA || seed.monoChars > 0 || seed.chars > 80 || seed.cellX0.size() > 2
                    || !(seed.mathChars >= 2 || (seed.mathChars >= 1 && seed.chars <= 25))
                    || (seed.mathChars + seed.weakMath) * 5 < seed.chars)
                continue;
            fz_rect region = fz_make_rect(seed.x0, seed.y0, seed.x1, seed.y1);
            const float near = seed.size;
            std::vector<size_t> pieces(1, i);
            for (bool grown = true; grown; ) {
                grown = false;
                for (size_t k = first; k < end; k++) {
                    const Item & item = items[k];
                    if (item.drop || item.kind != K_PARA || item.monoChars > 0)
                        continue;
                    bool has = false;
                    for (size_t q = 0; q < pieces.size(); q++)
                        has = has || pieces[q] == k;
                    if (has || !(item.chars <= 15 || (item.mathChars + item.weakMath >= 1 && item.chars <= 40)))
                        continue;
                    if (item.x0 > region.x1 + near * 3 || item.x1 < region.x0 - near * 3
                            || item.y0 > region.y1 + near * 0.8f || item.y1 < region.y0 - near * 0.8f)
                        continue;
                    region = fz_union_rect(region, fz_make_rect(item.x0, item.y0, item.x1, item.y1));
                    pieces.push_back(k);
                    grown = true;
                }
            }
            if (!takeRegion(p, region, box))
                continue;
            // the picture takes the place of the first piece
            Item picture = items.back();
            items.pop_back();
            size_t at = pieces[0];
            for (size_t q = 0; q < pieces.size(); q++) {
                items[pieces[q]].drop = true;
                if (pieces[q] < at)
                    at = pieces[q];
            }
            items[at] = picture;
            stats.formulas++;
        }
        // drawings: lines and shapes close to each other, not those of a table
        std::vector<fz_rect> boxes;
        collectVectors(blocks, boxes);
        const float area = pageW * pageH;
        std::vector<fz_rect> parts;
        for (size_t i = 0; i < boxes.size(); i++) {
            const fz_rect & b = boxes[i];
            const float w = b.x1 - b.x0, h = b.y1 - b.y0;
            if ((w < 1 && h < 1) || w * h > area * 0.5f)
                continue;
            const float cx = (b.x0 + b.x1) * 0.5f, cy = (b.y0 + b.y1) * 0.5f;
            bool inTable = false;
            for (size_t k = first; k < end && !inTable; k++)
                inTable = items[k].kind == K_TABLE && cx >= items[k].x0 - 3 && cx <= items[k].x1 + 3
                       && cy >= items[k].y0 - 3 && cy <= items[k].y1 + 3;
            if (!inTable)
                parts.push_back(b);
        }
        if (parts.size() < 5 || parts.size() > 4000)
            return;
        // groups of parts that touch when grown by a few points
        std::vector<int> group(parts.size());
        for (size_t i = 0; i < parts.size(); i++)
            group[i] = (int)i;
        const float grow = 8;
        for (size_t i = 0; i < parts.size(); i++)
            for (size_t j = i + 1; j < parts.size(); j++) {
                if (parts[i].x0 - grow > parts[j].x1 || parts[j].x0 - grow > parts[i].x1
                        || parts[i].y0 - grow > parts[j].y1 || parts[j].y0 - grow > parts[i].y1)
                    continue;
                int a = (int)i, b = (int)j;
                while (group[a] != a) a = group[a];
                while (group[b] != b) b = group[b];
                if (a != b)
                    group[b] = a;
            }
        for (size_t g = 0; g < parts.size(); g++) {
            if (group[g] != (int)g)
                continue;
            fz_rect region = parts[g];
            int count = 0;
            for (size_t i = 0; i < parts.size(); i++) {
                int root = (int)i;
                while (group[root] != root) root = group[root];
                if (root != (int)g)
                    continue;
                region = fz_union_rect(region, parts[i]);
                count++;
            }
            const float w = region.x1 - region.x0, h = region.y1 - region.y0;
            if (count < 5 || w < 60 || h < 40 || w * h > area * 0.75f || w > 12 * h || h > 12 * w || inMargin(region))
                continue;
            // the text inside: labels, not a paragraph in a frame
            int inside = 0;
            bool paragraph = false;
            for (size_t k = first; k < end; k++) {
                const Item & item = items[k];
                if (item.drop || item.kind == K_IMAGE || item.kind == K_TABLE)
                    continue;
                const float cx = (item.x0 + item.x1) * 0.5f, cy = (item.y0 + item.y1) * 0.5f;
                if (cx < region.x0 || cx > region.x1 || cy < region.y0 || cy > region.y1)
                    continue;
                inside += item.chars;
                if (item.chars > 200)
                    paragraph = true;
            }
            if (paragraph || inside > 400)
                continue;
            // the short lines right around it are its labels and its title: into the picture
            for (int round = 0; round < 2; round++)
                for (size_t k = first; k < end; k++) {
                    const Item & item = items[k];
                    if (item.drop || item.kind != K_PARA || item.lines != 1 || item.chars > 40)
                        continue;
                    const float near = item.size * 1.5f;
                    if (item.x0 > region.x1 + near || item.x1 < region.x0 - near
                            || item.y0 > region.y1 + near || item.y1 < region.y0 - near)
                        continue;
                    region = fz_union_rect(region, fz_make_rect(item.x0, item.y0, item.x1, item.y1));
                }
            if (!takeRegion(p, region, box))
                continue;
            stats.drawings++;
            for (size_t k = first; k < end; k++) {
                Item & item = items[k];
                if (item.kind == K_TABLE)
                    continue;
                const float cx = (item.x0 + item.x1) * 0.5f, cy = (item.y0 + item.y1) * 0.5f;
                if (cx >= region.x0 && cx <= region.x1 && cy >= region.y0 && cy <= region.y1)
                    item.drop = true;
            }
        }
    }

    static void countColumns(fz_stext_block * block, float middle, float narrow, int & last, int & switches,
                             int & left, int & right, int & sideBySide)
    {
        for (; block; block = block->next) {
            if (block->type == FZ_STEXT_BLOCK_STRUCT && block->u.s.down) {
                countColumns(block->u.s.down->first_block, middle, narrow, last, switches, left, right, sideBySide);
                continue;
            }
            if (block->type != FZ_STEXT_BLOCK_TEXT)
                continue;
            // long lines of one row given one after another: the text of two columns read across
            int lines = 0;
            fz_stext_line * before = NULL;
            int beforeChars = 0;
            for (fz_stext_line * line = block->u.t.first_line; line; line = line->next) {
                int chars = 0;
                for (fz_stext_char * ch = line->first_char; ch; ch = ch->next)
                    chars++;
                if (chars == 0)
                    continue;
                lines++;
                const float size = line->first_char->size;
                if (before && chars > 25 && beforeChars > 25 && line->bbox.x0 > before->bbox.x1
                        && fabsf((line->bbox.y0 + line->bbox.y1) - (before->bbox.y0 + before->bbox.y1)) < size * 0.6f)
                    sideBySide++;
                before = line;
                beforeChars = chars;
            }
            if (lines < 2 || block->bbox.x1 - block->bbox.x0 > narrow)
                continue;
            int side = 0;
            if (block->bbox.x1 < middle + 4)
                side = -1;
            else if (block->bbox.x0 > middle - 4)
                side = 1;
            if (side == 0)
                continue;
            if (side < 0) left++; else right++;
            if (last != 0 && side != last)
                switches++;
            last = side;
        }
    }

    /// The file draws the text of two columns mixed: paragraph by paragraph from one column to the
    /// other and back, many times, or line by line across both.
    static bool columnsMixed(fz_stext_block * first, const fz_rect & box)
    {
        int last = 0, switches = 0, left = 0, right = 0, sideBySide = 0;
        countColumns(first, (box.x0 + box.x1) * 0.5f, (box.x1 - box.x0) * 0.55f, last, switches, left, right, sideBySide);
        return (left >= 3 && right >= 3 && switches >= 5) || sideBySide >= 6;
    }

    bool readPage(int number)
    {
        page = number;
        fz_page * p = NULL;
        fz_stext_page * text = NULL;
        fz_rect box = fz_empty_rect;
        bool ok = true;
        double loaded = 0;
        fz_var(p);
        fz_var(text);
        fz_var(box);
        fz_var(loaded);
        const double t0 = nowMs();
        fz_try(ctx) {
            p = fz_load_page(ctx, doc, number);
            box = fz_bound_page(ctx, p);
            loaded = nowMs();
            fz_stext_options options;
            memset(&options, 0, sizeof(options));
            // The lines and shapes of the page are collected too: the table hunt reads the rules
            // of a table off them, and a drawing is found by them.
            // The page segmentation of the library (FZ_STEXT_SEGMENT) puts the regions of a page
            // into reading order. It takes 82 % of the time of the extraction (258 of 303 ms for
            // a book of 161 pages on the PC), and a file nearly always draws its columns one
            // after another, as they are read: the books made with and without it were the same
            // but for a title page. So a page is read without it, and read again with it only
            // when its columns come mixed (columnsMixed).
            options.flags = FZ_STEXT_PRESERVE_IMAGES | FZ_STEXT_DEHYPHENATE | FZ_STEXT_COLLECT_STRUCTURE
                | FZ_STEXT_COLLECT_VECTORS | FZ_STEXT_PARAGRAPH_BREAK | FZ_STEXT_TABLE_HUNT;
            // for the PC tool: options taken away, or added, to see what each costs
            if (getenv("PB_PDFTEXT_WITHOUT"))
                options.flags &= ~(int)strtol(getenv("PB_PDFTEXT_WITHOUT"), NULL, 0);
            if (getenv("PB_PDFTEXT_WITH"))
                options.flags |= (int)strtol(getenv("PB_PDFTEXT_WITH"), NULL, 0);
            text = fz_new_stext_page_from_page(ctx, p, &options);
            if ((options.flags & FZ_STEXT_SEGMENT) == 0 && columnsMixed(text->first_block, box)) {
                fz_drop_stext_page(ctx, text);
                text = NULL;
                options.flags |= FZ_STEXT_SEGMENT;
                text = fz_new_stext_page_from_page(ctx, p, &options);
                stats.segmented++;
            }
        }
        fz_catch(ctx) {
            ok = false;
        }
        const double extracted = nowMs();
        if (loaded > 0) {
            stats.loadMs += loaded - t0;
            stats.extractMs += extracted - loaded;
        }
        PageInfo info;
        info.width = pageW = box.x1 - box.x0;
        info.height = pageH = box.y1 - box.y0;
        pageTop = box.y0;
        info.chars = 0;
        info.hanging = false;
        info.firstItem = items.size();
        const double imagesBefore = stats.imageMs;
        if (ok && text) {
            curPage = p;
            findStacked(text->first_block);
            walk(text->first_block, Tag());
            curPage = NULL;
            info.hanging = pageHanging;
            pageHanging = false;
            stacked.clear();
            stacks.clear();
            takeFigures(p, text->first_block, box, info.firstItem);
            readingOrder(info.firstItem);
            // the items are placed by the top left corner of the page
            for (size_t i = info.firstItem; i < items.size(); i++) {
                items[i].x0 -= box.x0; items[i].x1 -= box.x0;
                items[i].y0 -= box.y0; items[i].y1 -= box.y0;
                if (items[i].kind != K_IMAGE)
                    info.chars += items[i].chars;
            }
        }
        if (text)
            fz_drop_stext_page(ctx, text);
        fz_drop_page(ctx, p);
        // what was dropped while the page was read (the labels of a drawing) leaves the list
        size_t kept = info.firstItem;
        for (size_t i = info.firstItem; i < items.size(); i++)
            if (!items[i].drop) {
                if (kept != i)
                    items[kept] = items[i];
                kept++;
            }
        items.resize(kept);
        info.endItem = items.size();
        pages.push_back(info);
        stats.textMs += nowMs() - t0 - (stats.imageMs - imagesBefore);
        return ok;
    }

    /// For the PC tool: the items of a page (PB_PDFTEXT_DUMP=<page from 1>) as they are now.
    void dump(const char * when)
    {
        const char * asked = getenv("PB_PDFTEXT_DUMP");
        const int number = asked ? atoi(asked) - 1 : -1;
        if (number < 0 || number >= (int)pages.size())
            return;
        fprintf(stderr, "--- page %d, %s\n", number + 1, when);
        for (size_t i = pages[number].firstItem; i < pages[number].endItem; i++) {
            const Item & item = items[i];
            static const char * const kinds[] = { "para", "head", "image", "table", "note" };
            fprintf(stderr, "%s%-5s x %.0f-%.0f y %.0f-%.0f last %.0f size %.1f lines %d cells %d chars %d math %d+%d: %.60s\n",
                    item.drop ? "  (dropped) " : "  ", kinds[item.kind], item.x0, item.x1, item.y0, item.y1, item.lastX1,
                    item.size, item.lines, (int)item.cellX0.size(), item.chars, item.mathChars, item.weakMath,
                    textOf(item.runs).c_str());
        }
    }

    // -------------------------------------------------------------------------------- pass 2
    float bodySize;
    std::vector<OutlineEntry> outline;

    void findBodySize()
    {
        long best = 0;
        bodySize = 10;
        for (std::map<int, long>::const_iterator it = sizes.begin(); it != sizes.end(); ++it)
            if (it->second > best) {
                best = it->second;
                bodySize = it->first * 0.5f;
            }
    }

    /// What stands at the top or the bottom of three pages or more, in a font no larger than the
    /// text, with only its numbers changing, is a running title; a number alone there is a page
    /// number. The heading of a chapter also starts a page and repeats ("Chapter 2"), but it is
    /// larger than the text.
    void dropRunning()
    {
        std::map<std::string, int> seen;
        std::vector<std::string> keys(items.size());
        for (size_t p = 0; p < pages.size(); p++) {
            const PageInfo & info = pages[p];
            for (size_t i = info.firstItem; i < info.endItem; i++) {
                Item & item = items[i];
                if ((item.kind != K_PARA && item.kind != K_HEADING) || item.tagged || item.chars > 100)
                    continue;
                const bool top = item.y1 <= info.height * 0.11f, bottom = item.y0 >= info.height * 0.89f;
                if (!top && !bottom)
                    continue;
                if (item.size > bodySize * 1.12f)
                    continue;
                // "Title 12" on one page and "Title" with its number apart on another are one title
                std::string core = normalized(textOf(item.runs));
                size_t from = 0, to = core.size();
                while (from < to && (core[from] == '#' || core[from] == ' ' || core[from] == '.' || core[from] == '-' || core[from] == '|' || core[from] == '\t'))
                    from++;
                while (to > from && (core[to - 1] == '#' || core[to - 1] == ' ' || core[to - 1] == '.' || core[to - 1] == '-' || core[to - 1] == '|' || core[to - 1] == '\t'))
                    to--;
                keys[i] = (top ? "t|" : "b|") + core.substr(from, to - from);
                seen[keys[i]]++;
            }
        }
        // A number in Roman numerals is a page number when two pages or more have one: reported
        // by a tester, the pages xvi, xx, xxiii of a preface came as headings of chapters, each
        // with an empty page. One such line in a whole book may be the heading "II".
        int romans = 0;
        for (size_t i = 0; i < items.size(); i++)
            if (!keys[i].empty() && romanNumber(keys[i].substr(2)))
                romans++;
        for (size_t i = 0; i < items.size(); i++) {
            if (keys[i].empty())
                continue;
            const std::string text = keys[i].substr(2);
            if (seen[keys[i]] >= 3 || text.empty() || onlyNumber(text) || (romans >= 2 && romanNumber(text))) {
                items[i].drop = true;
                stats.running++;
            }
        }
    }

    /// A picture under the text of its page (the paper of a designed page, the scan under a
    /// recognised text) and a picture that is the same on three pages or more (a logo, a rule)
    /// are not part of the book.
    void dropDecoration()
    {
        std::map<std::string, int> seen;
        for (size_t i = 0; i < pictures.size(); i++)
            seen[pictures[i].digest]++;
        for (size_t i = 0; i < items.size(); i++) {
            Item & item = items[i];
            if (item.kind != K_IMAGE)
                continue;
            const Picture & picture = pictures[item.image];
            if (seen[picture.digest] >= 3 || (picture.large && pages[item.page].chars > 300))
                item.drop = true;
        }
    }

    static void splitCells(const Runs & runs, std::vector<Runs> & cells)
    {
        cells.clear();
        cells.push_back(Runs());
        for (size_t r = 0; r < runs.size(); r++) {
            const std::string & t = runs[r].text;
            size_t from = 0;
            for (size_t i = 0; i <= t.size(); i++) {
                if (i < t.size() && t[i] != '\t')
                    continue;
                if (i > from) {
                    Run run;
                    run.style = runs[r].style;
                    run.text = t.substr(from, i - from);
                    cells.back().push_back(run);
                }
                if (i < t.size())
                    cells.push_back(Runs());
                from = i + 1;
            }
        }
    }

    struct RowCell {
        float x0, x1;
        Runs runs;
    };
    struct Row {
        float y0, y1, size;
        std::vector<size_t> sources;
        std::vector<RowCell> cells;
    };

    static bool cellBefore(const RowCell & a, const RowCell & b) { return a.x0 < b.x0; }

    /// A table without rules: its columns are seen only by their alignment. The lines of a page
    /// that are a paragraph each are put into rows by their height on the page; a row has the
    /// pieces of its lines that stand apart as cells. Three rows or more one under another with
    /// the same number of cells, two at least, and clear room between their columns from top to
    /// bottom, are a table. A text in two columns is not taken for one: its lines are parts of
    /// paragraphs, not paragraphs of a line.
    void plainTables()
    {
        for (size_t p = 0; p < pages.size(); p++) {
            const PageInfo & info = pages[p];
            std::vector<Row> rows;
            for (size_t i = info.firstItem; i < info.endItem; i++) {
                const Item & item = items[i];
                // (a header cell may be tagged a heading by the file)
                if (item.drop || (item.kind != K_PARA && item.kind != K_HEADING) || item.lines != 1 || item.verse
                        || item.monoChars * 2 > item.chars || item.cellX0.size() != item.cellX1.size()
                        || item.size > bodySize * 1.15f)
                    continue;
                std::vector<Runs> cells;
                splitCells(item.runs, cells);
                if (cells.size() != item.cellX0.size())
                    continue;
                const float middle = (item.y0 + item.y1) * 0.5f;
                size_t at = rows.size();
                for (size_t r = 0; r < rows.size(); r++)
                    if (fabsf((rows[r].y0 + rows[r].y1) * 0.5f - middle) < item.size * 0.5f) {
                        at = r;
                        break;
                    }
                if (at == rows.size()) {
                    Row row;
                    row.y0 = item.y0;
                    row.y1 = item.y1;
                    row.size = item.size;
                    rows.push_back(row);
                }
                Row & row = rows[at];
                row.sources.push_back(i);
                for (size_t c = 0; c < cells.size(); c++) {
                    RowCell cell;
                    cell.x0 = item.cellX0[c] - items[i].x0 + item.x0;
                    cell.x1 = item.cellX1[c];
                    cell.runs = cells[c];
                    row.cells.push_back(cell);
                }
            }
            // top to bottom, the cells of a row left to right
            for (size_t a = 0; a < rows.size(); a++) {
                for (size_t b = a + 1; b < rows.size(); b++)
                    if (rows[b].y0 < rows[a].y0) {
                        Row t = rows[a];
                        rows[a] = rows[b];
                        rows[b] = t;
                    }
                for (size_t c = 1; c < rows[a].cells.size(); c++)
                    for (size_t d = c; d > 0 && cellBefore(rows[a].cells[d], rows[a].cells[d - 1]); d--) {
                        RowCell t = rows[a].cells[d];
                        rows[a].cells[d] = rows[a].cells[d - 1];
                        rows[a].cells[d - 1] = t;
                    }
            }
            for (size_t a = 0; a < rows.size(); ) {
                const size_t n = rows[a].cells.size();
                size_t b = a + 1;
                if (n >= 2)
                    while (b < rows.size() && rows[b].cells.size() == n && rows[b].y0 - rows[b - 1].y1 < rows[a].size * 1.8f)
                        b++;
                bool table = n >= 2 && b - a >= 3;
                // clear room between the columns: every cell of a column ends before any cell of the next starts
                for (size_t c = 0; table && c + 1 < n; c++) {
                    float end = -1e9f, start = 1e9f;
                    for (size_t r = a; r < b; r++) {
                        if (rows[r].cells[c].x1 > end) end = rows[r].cells[c].x1;
                        if (rows[r].cells[c + 1].x0 < start) start = rows[r].cells[c + 1].x0;
                    }
                    if (end > start - rows[a].size * 0.3f)
                        table = false;
                }
                if (!table) {
                    a++;
                    continue;
                }
                Item made;
                made.kind = K_TABLE;
                made.page = (int)p;
                made.x0 = made.y0 = 1e9f;
                made.x1 = made.y1 = -1e9f;
                size_t place = rows[a].sources[0];
                for (size_t r = a; r < b; r++) {
                    std::vector<Cell> cells(n);
                    for (size_t c = 0; c < n; c++)
                        cells[c].runs = rows[r].cells[c].runs;
                    made.rows.push_back(cells);
                    for (size_t s = 0; s < rows[r].sources.size(); s++) {
                        Item & source = items[rows[r].sources[s]];
                        if (source.x0 < made.x0) made.x0 = source.x0;
                        if (source.y0 < made.y0) made.y0 = source.y0;
                        if (source.x1 > made.x1) made.x1 = source.x1;
                        if (source.y1 > made.y1) made.y1 = source.y1;
                        made.chars += source.chars;
                        source.drop = true;
                        if (rows[r].sources[s] < place)
                            place = rows[r].sources[s];
                    }
                }
                items[place] = made;
                a = b;
            }
        }
    }

    /// The library ends a paragraph at a line shorter than the others. In a text set with a ragged
    /// right edge, or with the first line of an entry standing out to the left, such lines are
    /// everywhere. A paragraph that does not end a sentence goes on in the next one, a line
    /// below it, when that one starts with a small letter, or when its first word would not have
    /// fitted at the end of the last line of the first (the line was broken because it was full;
    /// the width of the word is taken as half the font size a character).
    void mendParagraphs()
    {
        for (size_t p = 0; p < pages.size(); p++) {
            size_t last = (size_t)-1;
            float left = 1e9f, right = -1e9f;
            for (size_t i = pages[p].firstItem; i < pages[p].endItem; i++)
                if (!items[i].drop && (items[i].kind == K_PARA || items[i].kind == K_NOTE) && items[i].chars > 40) {
                    if (items[i].x0 < left) left = items[i].x0;
                    if (items[i].x1 > right) right = items[i].x1;
                }
            // A page of entries whose first lines hang out to the left (a glossary, a list of
            // books): more paragraphs with the first line standing out than with it set in.
            int hung = 0, indented = 0;
            for (size_t i = pages[p].firstItem; i < pages[p].endItem; i++) {
                const Item & item = items[i];
                if (item.drop || item.kind != K_PARA || item.restX > 1e8f)
                    continue;
                if (item.restX > item.firstX + item.size * 0.8f)
                    hung++;
                else if (item.firstX > item.restX + item.size * 0.8f)
                    indented++;
            }
            const bool hanging = pages[p].hanging || (hung >= 3 && hung > indented * 2);
            for (size_t i = pages[p].firstItem; i < pages[p].endItem; i++) {
                Item & b = items[i];
                if (b.drop)
                    continue;
                if (last != (size_t)-1) {
                    Item & a = items[last];
                    const float gap = b.y0 - a.y1;
                    // on such a page a paragraph all set in goes on the entry above it, whatever
                    // the entry ends with (the library ends a paragraph at a full stop)
                    if (hanging && a.kind == K_PARA && b.kind == K_PARA && !a.tagged && !b.tagged && !a.verse && !b.verse
                            && a.cellX0.size() < 2 && b.cellX0.size() < 2 && gap < a.size * 0.7f && gap > -a.size * 0.5f
                            && fabsf(a.size - b.size) < a.size * 0.06f && b.firstX > a.firstX + a.size * 0.8f
                            && (b.restX > 1e8f || b.restX > a.firstX + a.size * 0.8f)) {
                        join(a, b);
                        a.y1 = b.y1;
                        a.lastX1 = b.lastX1;
                        a.lines += b.lines;
                        if (b.x1 > a.x1)
                            a.x1 = b.x1;
                        if (a.restX > b.firstX)
                            a.restX = b.firstX;
                        b.drop = true;
                        stats.mended++;
                        continue;
                    }
                    // the rest of a line whose fraction is a picture: the library gives it as a
                    // block of its own, on the row of the picture and to its right
                    if (a.kind == b.kind && a.kind == K_PARA && endsWithPicture(a.runs) && b.x0 > a.lastX1 - a.size
                            && b.y0 < a.y1 && b.y0 > a.y1 - a.size * 2.2f && fabsf(a.size - b.size) < a.size * 0.12f) {
                        join(a, b);
                        if (b.y1 > a.y1)
                            a.y1 = b.y1;
                        a.lastX1 = b.lastX1;
                        if (b.x1 > a.x1)
                            a.x1 = b.x1;
                        b.drop = true;
                        continue;
                    }
                    const bool full = right > left && a.chars > 20
                        && a.lastX1 + a.size * (0.25f + 0.48f * firstWord(b.runs)) > right - a.size * 0.5f;
                    if (a.kind == b.kind && (a.kind == K_PARA || a.kind == K_NOTE) && !a.tagged && !b.tagged
                            && !a.verse && !b.verse && a.cellX0.size() < 2 && b.cellX0.size() < 2
                            && gap < a.size * 0.7f && gap > -a.size * 0.5f && fabsf(b.x0 - a.x0) < a.size * 4
                            && fabsf(a.size - b.size) < a.size * 0.06f && a.monoChars * 2 <= a.chars
                            && ((isLower(firstChar(b.runs)) && !startsEntry(b.runs) && !endsFully(lastChar(a.runs)))
                                || (full && !endsSentence(lastChar(a.runs))) || isHyphen(lastChar(a.runs)))) {
                        join(a, b);
                        a.y1 = b.y1;
                        a.lastX1 = b.lastX1;
                        a.lines += b.lines;
                        if (b.x1 > a.x1)
                            a.x1 = b.x1;
                        b.drop = true;
                        stats.mended++;
                        continue;
                    }
                }
                last = i;
            }
        }
    }

    /// Headings of a file without structure tags: larger than the text and short; bold and short;
    /// or a short line of the size of the text with the room of an empty line above and below it
    /// and a paragraph after it (a book may set its chapter titles so). And the small paragraphs
    /// at the bottom of a page, the first of which starts with a number or a star, are its
    /// footnotes.
    void classify()
    {
        for (size_t p = 0; p < pages.size(); p++) {
            const PageInfo & info = pages[p];
            // from the bottom up: a heading stands before a text, or before a heading that does
            // (the title of a part, then of its first chapter); the lines of a title page do not
            bool textBelow = false, headingBelow = false;
            float belowY0 = 0;
            for (size_t i = info.endItem; i > info.firstItem; i--) {
                Item & item = items[i - 1];
                if (item.drop)
                    continue;
                size_t before = i - 1;
                while (before > info.firstItem && items[before - 1].drop)
                    before--;
                const bool firstOfPage = before == info.firstItem;
                const int last = lastChar(item.runs), start = firstChar(item.runs);
                bool heading = false;
                if (item.kind == K_PARA && !item.tagged && !item.verse && item.chars >= 3 && item.chars <= 100
                        && item.lines <= 2 && fabsf(item.size - bodySize) < bodySize * 0.1f && item.monoChars == 0
                        && item.cellX0.size() <= 2 && !endsSentence(last) && last != ',' && !isLower(start)
                        && (isLetter(start) || start == 0xA7 || (start >= '0' && start <= '9'))
                        && (textBelow || headingBelow) && belowY0 - item.y1 > item.size * 0.9f
                        && (firstOfPage || item.y0 - items[before - 1].y1 > item.size * 0.9f)) {
                    item.kind = K_HEADING;
                    item.level = 2;
                    heading = true;
                }
                headingBelow = heading;
                textBelow = !heading && item.kind == K_PARA && !item.verse && (item.chars >= 150 || endsFully(last));
                belowY0 = item.y0;
            }
        }
        for (size_t i = 0; i < items.size(); i++) {
            Item & item = items[i];
            if (item.drop || item.kind != K_PARA || item.tagged)
                continue;
            const int last = lastChar(item.runs);
            if (item.size >= bodySize * 1.18f && item.chars <= 160) {
                item.kind = K_HEADING;
                const float ratio = item.size / bodySize;
                item.level = ratio >= 1.7f ? 1 : ratio >= 1.35f ? 2 : 3;
            } else if (item.boldChars * 10 >= item.chars * 9 && item.chars <= 90 && item.chars >= 2
                       && item.size >= bodySize * 0.97f && last != '.' && last != ',' && last != ';') {
                item.kind = K_HEADING;
                item.level = 4;
            }
        }
        for (size_t p = 0; p < pages.size(); p++) {
            const PageInfo & info = pages[p];
            size_t first = info.endItem;
            bool marked = false;
            for (size_t i = info.endItem; i > info.firstItem; i--) {
                Item & item = items[i - 1];
                if (item.drop)
                    continue;
                if (item.kind != K_PARA || item.tagged || item.size > bodySize * 0.92f || item.y0 < info.height * 0.5f)
                    break;
                first = i - 1;
                const int c = firstChar(item.runs);
                if ((c >= '0' && c <= '9') || c == '*' || c == 0x2020 || c == 0x2021)
                    marked = true;
            }
            if (!marked)
                continue;
            for (size_t i = first; i < info.endItem; i++)
                if (!items[i].drop && items[i].kind == K_PARA) {
                    items[i].kind = K_NOTE;
                    stats.footnotes++;
                }
        }
    }

    void readOutline()
    {
        fz_outline * root = NULL;
        fz_var(root);
        fz_try(ctx) {
            root = fz_load_outline(ctx, doc);
        }
        fz_catch(ctx) {
            root = NULL;
        }
        if (!root)
            return;
        flatten(root, 1);
        fz_drop_outline(ctx, root);
    }

    void flatten(fz_outline * node, int level)
    {
        for (; node; node = node->next) {
            int number = -1;
            fz_try(ctx) {
                number = fz_page_number_from_location(ctx, doc, node->page);
            }
            fz_catch(ctx) {
                number = -1;
            }
            if (number >= 0 && number < (int)pages.size() && node->title && node->title[0]) {
                OutlineEntry entry;
                entry.level = level > 6 ? 6 : level;
                entry.page = number;
                entry.title = node->title;
                outline.push_back(entry);
            }
            if (node->down)
                flatten(node->down, level + 1);
        }
    }

    // -------------------------------------------------------------------------------- writing
    FILE * out;
    std::vector<int> open;      // levels of the sections open; 100: the one before the first heading
    int anchor;                 // the page whose id the next element carries, -1: none waits

    static void escaped(FILE * f, const std::string & s)
    {
        size_t from = 0;
        for (size_t i = 0; i < s.size(); i++) {
            const char * entity = s[i] == '&' ? "&amp;" : s[i] == '<' ? "&lt;" : s[i] == '>' ? "&gt;" : NULL;
            if (!entity)
                continue;
            fwrite(s.data() + from, 1, i - from, f);
            fputs(entity, f);
            from = i + 1;
        }
        fwrite(s.data() + from, 1, s.size() - from, f);
    }

    void writeRuns(const Runs & runs, bool plain)
    {
        for (size_t i = 0; i < runs.size(); i++) {
            if (runs[i].style & S_IMAGE) {
                fprintf(out, "<image l:href=\"#i%s\"/>", runs[i].text.c_str());
                continue;
            }
            const unsigned style = plain ? (runs[i].style & (S_SUP | S_SUB)) : runs[i].style;
            if (style & S_BOLD) fputs("<strong>", out);
            if (style & S_ITALIC) fputs("<emphasis>", out);
            if (style & S_MONO) fputs("<code>", out);
            if (style & S_SUP) fputs("<sup>", out);
            if (style & S_SUB) fputs("<sub>", out);
            escaped(out, runs[i].text);
            if (style & S_SUB) fputs("</sub>", out);
            if (style & S_SUP) fputs("</sup>", out);
            if (style & S_MONO) fputs("</code>", out);
            if (style & S_ITALIC) fputs("</emphasis>", out);
            if (style & S_BOLD) fputs("</strong>", out);
        }
    }

    /// The id of the page for the element being written, once for a page.
    void idAttr(int ofPage)
    {
        if (anchor < 0 || ofPage < anchor)
            return;
        fprintf(out, " id=\"pg%d\"", anchor + 1);
        anchor = -1;
    }

    void closeTo(int level)
    {
        endPoem();
        while (!open.empty() && open.back() >= level) {
            fputs("</section>\n", out);
            open.pop_back();
        }
    }

    void openSection(int level, const Runs * title, const std::string * plainTitle, int ofPage)
    {
        closeTo(level);
        fputs("<section>\n", out);
        open.push_back(level);
        if (title || plainTitle) {
            fputs("<title><p", out);
            idAttr(ofPage);
            fputs(">", out);
            if (title)
                writeRuns(*title, true);
            else
                escaped(out, *plainTitle);
            fputs("</p></title>\n", out);
            stats.sections++;
        }
    }

    void needSection()
    {
        if (open.empty())
            openSection(100, NULL, NULL, 0);
    }

    void writePara(const Item & item, const char * wrapOpen, const char * wrapClose)
    {
        needSection();
        if (wrapOpen)
            fputs(wrapOpen, out);
        fputs("<p", out);
        idAttr(item.page);
        fputs(">", out);
        // a paragraph that is bold all through is a style of the original, not an emphasis
        writeRuns(item.runs, item.boldChars * 10 >= item.chars * 9 && item.chars > 200);
        fputs("</p>", out);
        if (wrapClose)
            fputs(wrapClose, out);
        fputc('\n', out);
    }

    bool inPoem;

    void endPoem()
    {
        if (inPoem)
            fputs("</stanza></poem>\n", out);
        inPoem = false;
    }

    void writeItem(const Item & item)
    {
        if (!(item.kind == K_PARA && item.verse))
            endPoem();
        switch (item.kind) {
        case K_PARA:
            if (item.verse) {
                // the lines of a block come one after another: one stanza
                needSection();
                if (!inPoem)
                    fputs("<poem><stanza>\n", out);
                inPoem = true;
                fputs("<v", out);
                idAttr(item.page);
                fputs(">", out);
                writeRuns(item.runs, false);
                fputs("</v>\n", out);
                break;
            }
            writePara(item, item.quote ? "<cite>" : NULL, item.quote ? "</cite>" : NULL);
            stats.paragraphs++;
            break;
        case K_NOTE:
            writePara(item, "<cite>", "</cite>");
            break;
        case K_HEADING:
            stats.headings++;
            // with contents in the file the sections are those of the contents
            if (outline.empty() && item.level <= 2) {
                openSection(item.level, &item.runs, NULL, item.page);
            } else {
                needSection();
                fputs("<subtitle", out);
                idAttr(item.page);
                fputs(">", out);
                writeRuns(item.runs, true);
                fputs("</subtitle>\n", out);
            }
            break;
        case K_IMAGE:
            needSection();
            fputs("<image", out);
            idAttr(item.page);
            fprintf(out, " l:href=\"#i%d\"/>\n", item.image);
            stats.images++;
            break;
        case K_TABLE:
            needSection();
            fputs("<table", out);
            idAttr(item.page);
            fputs(">\n", out);
            for (size_t r = 0; r < item.rows.size(); r++) {
                fputs("<tr>", out);
                for (size_t c = 0; c < item.rows[r].size(); c++) {
                    const Cell & cell = item.rows[r][c];
                    fputs("<td", out);
                    if (cell.colspan > 1)
                        fprintf(out, " colspan=\"%d\"", cell.colspan);
                    if (cell.rowspan > 1)
                        fprintf(out, " rowspan=\"%d\"", cell.rowspan);
                    fputs(">", out);
                    writeRuns(cell.runs, false);
                    fputs("</td>", out);
                }
                fputs("</tr>\n", out);
            }
            fputs("</table>\n", out);
            stats.tables++;
            break;
        }
    }

    /// A paragraph cut by the end of a page goes on at the top of the next one: it ends without
    /// the end of a sentence, or with the hyphen of a divided word, and the next one starts with
    /// a small letter.
    bool goesOn(const Item & a, const Item & b)
    {
        if (a.kind != K_PARA || b.kind != K_PARA || b.page <= a.page || a.quote != b.quote || a.verse || b.verse
                || a.cellX0.size() > 1 || b.cellX0.size() > 1)
            return false;
        if (a.monoChars * 2 > a.chars || b.monoChars * 2 > b.chars)
            return false;
        if (fabsf(a.size - b.size) > a.size * 0.06f)
            return false;
        const int last = lastChar(a.runs), first = firstChar(b.runs);
        if (!isLower(first))
            return false;
        return isHyphen(last) || !endsSentence(last);
    }

    static bool endsWithPicture(const Runs & runs)
    {
        for (size_t i = runs.size(); i > 0; i--) {
            if (runs[i - 1].style & S_IMAGE)
                return true;
            if (runs[i - 1].text.find_first_not_of(" \t") != std::string::npos)
                return false;
        }
        return false;
    }

    static void join(Item & a, const Item & b)
    {
        std::string & tail = a.runs.back().text;
        const int last = lastChar(a.runs);
        if (isHyphen(last) && !tail.empty()) {
            // the two halves of a divided word
            size_t cut = tail.size();
            while (cut > 0 && (tail[cut - 1] & 0xC0) == 0x80)
                cut--;
            tail.erase(cut > 0 ? cut - 1 : 0);
        } else if (a.runs.back().style & S_IMAGE) {
            Run space;
            space.style = 0;
            space.text = " ";
            a.runs.push_back(space);
        } else {
            tail += ' ';
        }
        for (size_t i = 0; i < b.runs.size(); i++)
            if (!a.runs.empty() && a.runs.back().style == b.runs[i].style && !(b.runs[i].style & S_IMAGE))
                a.runs.back().text += b.runs[i].text;
            else
                a.runs.push_back(b.runs[i]);
        a.chars += b.chars;
        a.boldChars += b.boldChars;
        a.monoChars += b.monoChars;
    }

    static bool sameTitle(const std::string & a, const std::string & b)
    {
        if (a.empty() || b.empty())
            return false;
        const size_t n = a.size() < b.size() ? a.size() : b.size();
        return n >= 3 && a.compare(0, n, b, 0, n) == 0;
    }

    void writeBody()
    {
        Item held;
        bool holding = false;
        std::vector<size_t> notes;      // the footnotes of the pages gone by, not written yet
        size_t nextEntry = 0;
        anchor = -1;
        inPoem = false;
        for (size_t p = 0; p < pages.size(); p++) {
            const PageInfo & info = pages[p];
            // the entries of the contents that point to this page: each opens its section at the
            // paragraph that is its title; one with no such paragraph, at the top of the page
            std::vector<int> entryAt(info.endItem - info.firstItem, -1);
            std::vector<size_t> atTop;
            for (; nextEntry < outline.size() && outline[nextEntry].page <= (int)p; nextEntry++) {
                if (outline[nextEntry].page < (int)p) {
                    atTop.push_back(nextEntry);
                    continue;
                }
                const std::string title = normalized(outline[nextEntry].title);
                bool placed = false;
                for (size_t i = info.firstItem; i < info.endItem && !placed; i++) {
                    const Item & item = items[i];
                    if (item.drop || (item.kind != K_PARA && item.kind != K_HEADING) || item.chars > 200
                            || entryAt[i - info.firstItem] >= 0)
                        continue;
                    if (sameTitle(normalized(textOf(item.runs)), title)) {
                        entryAt[i - info.firstItem] = (int)nextEntry;
                        placed = true;
                    }
                }
                if (!placed)
                    atTop.push_back(nextEntry);
            }
            bool pageStarted = false;
            for (size_t i = info.firstItem; i <= info.endItem; i++) {
                const bool end = i == info.endItem;
                if (!end && items[i].drop)
                    continue;
                if (!pageStarted && (!end || !atTop.empty())) {
                    pageStarted = true;
                    // a paragraph that goes on from the page before takes this page's first one
                    if (!end && holding && atTop.empty() && entryAt[i - info.firstItem] < 0 && goesOn(held, items[i])) {
                        join(held, items[i]);
                        stats.joined++;
                        if (anchor < 0)
                            anchor = (int)p;
                        continue;
                    }
                    flush(held, holding, notes);
                    if (anchor < 0)
                        anchor = (int)p;
                    for (size_t k = 0; k < atTop.size(); k++)
                        openSection(outline[atTop[k]].level, NULL, &outline[atTop[k]].title, (int)p);
                }
                if (end)
                    break;
                const Item & item = items[i];
                const int entry = entryAt[i - info.firstItem];
                if (entry >= 0) {
                    flush(held, holding, notes);
                    openSection(outline[entry].level, NULL, &outline[entry].title, (int)p);
                    continue;
                }
                if (item.kind == K_NOTE) {
                    notes.push_back(i);
                    continue;
                }
                if (item.kind == K_PARA && !item.verse) {
                    flush(held, holding, notes);
                    held = item;
                    holding = true;
                    continue;
                }
                flush(held, holding, notes);
                writeItem(item);
            }
        }
        flush(held, holding, notes);
        // footnotes of the last page
        for (size_t k = 0; k < notes.size(); k++)
            writeItem(items[notes[k]]);
        needSection();
        closeTo(0);
    }

    /// The paragraph held back is written, and after it the footnotes of the pages it started on.
    void flush(Item & held, bool & holding, std::vector<size_t> & notes)
    {
        if (!holding)
            return;
        writeItem(held);
        holding = false;
        size_t kept = 0;
        for (size_t k = 0; k < notes.size(); k++)
            if (items[notes[k]].page <= held.page || true) {
                writeItem(items[notes[k]]);
            } else {
                notes[kept++] = notes[k];
            }
        notes.resize(kept);
    }

    std::string metadata(const char * key)
    {
        char buffer[512];
        buffer[0] = 0;
        fz_try(ctx) {
            if (fz_lookup_metadata(ctx, doc, key, buffer, sizeof(buffer)) <= 0)
                buffer[0] = 0;
        }
        fz_catch(ctx) {
            buffer[0] = 0;
        }
        buffer[sizeof(buffer) - 1] = 0;
        return std::string(buffer);
    }

    /// The first page as the cover of the book: the program shows it where it shows covers.
    bool writeCover()
    {
        fz_page * p = NULL;
        fz_pixmap * pix = NULL;
        fz_buffer * packed = NULL;
        unsigned char * data = NULL;
        size_t length = 0;
        fz_var(p);
        fz_var(pix);
        fz_var(packed);
        fz_var(data);
        fz_var(length);
        fz_try(ctx) {
            p = fz_load_page(ctx, doc, 0);
            const fz_rect box = fz_bound_page(ctx, p);
            const float zoom = 600.0f / (box.x1 - box.x0 > 1 ? box.x1 - box.x0 : 1);
            pix = fz_new_pixmap_from_page(ctx, p, fz_scale(zoom, zoom), fz_device_gray(ctx), 0);
            packed = fz_new_buffer_from_pixmap_as_jpeg(ctx, pix, fz_default_color_params, 75, 0);
            length = fz_buffer_storage(ctx, packed, &data);
        }
        fz_always(ctx) {
            fz_drop_pixmap(ctx, pix);
            fz_drop_page(ctx, p);
        }
        fz_catch(ctx) {
            data = NULL;
        }
        bool ok = data != NULL && length > 0 && binaries != NULL;
        if (ok) {
            fputs("<binary id=\"cover\" content-type=\"image/jpeg\">", binaries);
            writeBase64(binaries, data, length);
            fputs("</binary>\n", binaries);
        }
        fz_drop_buffer(ctx, packed);
        return ok;
    }

    bool write(const char * pdfPath, const char * bookPath, const std::string & binPath, std::string * error)
    {
        const std::string temp = std::string(bookPath) + ".tmp";
        out = fopen(temp.c_str(), "wb");
        if (!out) {
            if (error)
                *error = "the book cannot be written";
            return false;
        }
        static char buffer[1 << 16];
        setvbuf(out, buffer, _IOFBF, sizeof(buffer));
        std::string title = metadata(FZ_META_INFO_TITLE), author = metadata(FZ_META_INFO_AUTHOR);
        if (title.empty()) {
            // the name of the file without its folder and its extension
            title = pdfPath;
            size_t slash = title.find_last_of("/\\");
            if (slash != std::string::npos)
                title.erase(0, slash + 1);
            size_t dot = title.rfind('.');
            if (dot != std::string::npos && dot > 0)
                title.erase(dot);
        }
        const bool cover = writeCover();
        const char * lang = cyrillic > latin ? (ukrainian * 40 > cyrillic ? "uk" : "ru") : "en";
        fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\" xmlns:l=\"http://www.w3.org/1999/xlink\">\n"
              "<description><title-info><genre>nonfiction</genre><author><nickname>", out);
        escaped(out, author);
        fputs("</nickname></author><book-title>", out);
        escaped(out, title);
        fputs("</book-title>", out);
        if (cover)
            fputs("<coverpage><image l:href=\"#cover\"/></coverpage>", out);
        fprintf(out, "<lang>%s</lang></title-info><document-info><program-used>CoolReader, PDF as a book, version %d"
                     "</program-used></document-info></description>\n<body>\n", lang, PB_PDFTEXT_VERSION);
        writeBody();
        fputs("</body>\n", out);
        // the pictures, written while the pages were read
        if (binaries) {
            fflush(binaries);
            rewind(binaries);
            char block[1 << 15];
            size_t got;
            while ((got = fread(block, 1, sizeof(block), binaries)) > 0)
                fwrite(block, 1, got, out);
        }
        fputs("</FictionBook>\n", out);
        stats.bytes = ftell(out);
        const bool ok = ferror(out) == 0;
        if (fclose(out) != 0 || !ok || rename(temp.c_str(), bookPath) != 0) {
            unlink(temp.c_str());
            if (error)
                *error = "the book cannot be written";
            return false;
        }
        (void)binPath;
        return true;
    }
};

fz_context * newContext()
{
    fz_context * ctx = fz_new_context(NULL, NULL, 32 << 20);
    if (!ctx)
        return NULL;
    fz_set_warning_callback(ctx, quiet, NULL);
    fz_set_error_callback(ctx, quiet, NULL);
    fz_disable_icc(ctx);
    return ctx;
}

} // namespace

PbPdfKind pbPdfKind(const char * path)
{
    fz_context * ctx = newContext();
    if (!ctx)
        return PB_PDF_UNREADABLE;
    fz_document * doc = NULL;
    int count = 0;
    fz_var(doc);
    fz_var(count);
    fz_try(ctx) {
        fz_register_document_handlers(ctx);
        doc = fz_open_document(ctx, path);
        if (fz_needs_password(ctx, doc))
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "password");
        count = fz_count_pages(ctx, doc);
    }
    fz_catch(ctx) {
        count = 0;
    }
    int looked = 0, withText = 0;
    const int wanted = count < 10 ? count : 10;
    for (int k = 0; k < wanted; k++) {
        // spread over the file; the first pages of a book are often a cover and a title
        const int number = wanted == count ? k : (int)((long)(2 * k + 1) * count / (2 * wanted));
        fz_page * p = NULL;
        fz_stext_page * text = NULL;
        fz_rect box = fz_empty_rect;
        fz_var(p);
        fz_var(text);
        fz_var(box);
        fz_try(ctx) {
            p = fz_load_page(ctx, doc, number);
            box = fz_bound_page(ctx, p);
            fz_stext_options options;
            memset(&options, 0, sizeof(options));
            options.flags = FZ_STEXT_PRESERVE_IMAGES;
            text = fz_new_stext_page_from_page(ctx, p, &options);
        }
        fz_always(ctx) {
            fz_drop_page(ctx, p);
        }
        fz_catch(ctx) {
            text = NULL;
        }
        if (!text)
            continue;
        looked++;
        long chars = 0;
        bool scan = false;
        const float area = (box.x1 - box.x0) * (box.y1 - box.y0);
        for (fz_stext_block * b = text->first_block; b; b = b->next) {
            if (b->type == FZ_STEXT_BLOCK_TEXT) {
                for (fz_stext_line * line = b->u.t.first_line; line; line = line->next)
                    for (fz_stext_char * ch = line->first_char; ch; ch = ch->next)
                        if (ch->c > 32)
                            chars++;
            } else if (b->type == FZ_STEXT_BLOCK_IMAGE) {
                // a picture over most of the page: the page is a scan, whatever text lies under it
                if ((b->bbox.x1 - b->bbox.x0) * (b->bbox.y1 - b->bbox.y0) > area * 0.65f)
                    scan = true;
            }
        }
        if (chars >= 150 && !scan)
            withText++;
        fz_drop_stext_page(ctx, text);
    }
    if (doc)
        fz_drop_document(ctx, doc);
    fz_drop_context(ctx);
    if (count <= 0 || looked == 0)
        return PB_PDF_UNREADABLE;
    return withText * 2 >= looked ? PB_PDF_TEXT : PB_PDF_PICTURES;
}

bool pbPdfToBook(const char * pdfPath, const char * bookPath, PbPdfTextProgress progress, void * context,
                 PbPdfTextStats * stats, std::string * error)
{
    Translator t;
    t.ctx = newContext();
    if (!t.ctx) {
        if (error)
            *error = "no memory";
        return false;
    }
    int count = 0;
    bool ok = true;
    char message[256];
    message[0] = 0;
    fz_var(count);
    fz_var(ok);
    fz_try(t.ctx) {
        fz_register_document_handlers(t.ctx);
        t.doc = fz_open_document(t.ctx, pdfPath);
        if (fz_needs_password(t.ctx, t.doc))
            fz_throw(t.ctx, FZ_ERROR_ARGUMENT, "the file is protected by a password");
        count = fz_count_pages(t.ctx, t.doc);
    }
    fz_catch(t.ctx) {
        ok = false;
        snprintf(message, sizeof(message), "%s", fz_caught_message(t.ctx));
    }
    const std::string binPath = std::string(bookPath) + ".bin";
    if (ok && count > 0) {
        t.binaries = fopen(binPath.c_str(), "w+b");
        for (int n = 0; n < count && ok; n++) {
            if (progress && !progress(context, n, count)) {
                ok = false;
                snprintf(message, sizeof(message), "stopped");
                break;
            }
            t.readPage(n);
        }
    } else if (ok) {
        ok = false;
        snprintf(message, sizeof(message), "the file has no pages");
    }
    if (ok) {
        const double t0 = nowMs();
        t.stats.pages = count;
        t.findBodySize();
        t.dump("as read");
        t.dropRunning();
        t.dropDecoration();
        t.plainTables();
        t.mendParagraphs();
        t.classify();
        t.dump("as written");
        t.readOutline();
        std::string reason;
        ok = t.write(pdfPath, bookPath, binPath, &reason);
        if (!ok)
            snprintf(message, sizeof(message), "%s", reason.c_str());
        t.stats.writeMs = nowMs() - t0;
    }
    if (t.binaries)
        fclose(t.binaries);
    unlink(binPath.c_str());
    if (t.doc)
        fz_drop_document(t.ctx, t.doc);
    fz_drop_context(t.ctx);
    if (stats)
        *stats = t.stats;
    if (!ok && error)
        *error = message;
    return ok;
}
