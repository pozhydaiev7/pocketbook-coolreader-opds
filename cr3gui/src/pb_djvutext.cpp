/*
 * A DjVu with a text layer as a book: see pb_djvutext.h.
 *
 * The work goes in passes over the whole file, since what a line is shows only beside the other
 * pages: the text layer of every page is read into lines (reading it does not decode the page);
 * the size of the body text and the place of the text block are found; what repeats at the top
 * and the bottom of the pages goes; the lines make paragraphs; the pages that have a place with
 * no text in the text block are decoded and that place, when something is drawn there, is cut
 * out as a picture; the paragraphs cut by a page break are joined; the book is written.
 */
#include "pb_djvutext.h"

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
#include "libdjvu/ddjvuapi.h"
#include "libdjvu/miniexp.h"

namespace {

double nowMs()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// ---------------------------------------------------------------------------- characters
/// The character at i (UTF-8); i goes past it.
int charAt(const std::string & s, size_t & i)
{
    const unsigned char c = (unsigned char)s[i++];
    if (c < 0x80)
        return c;
    int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    int value = c & (0x3F >> more);
    while (more-- > 0 && i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80)
        value = (value << 6) | ((unsigned char)s[i++] & 0x3F);
    return value;
}

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

int firstChar(const std::string & s)
{
    size_t i = 0;
    return s.empty() ? 0 : charAt(s, i);
}

/// The place of the last character.
size_t lastAt(const std::string & s)
{
    size_t i = s.empty() ? 0 : s.size() - 1;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80)
        i--;
    return i;
}

int lastChar(const std::string & s)
{
    size_t i = lastAt(s);
    return s.empty() ? 0 : charAt(s, i);
}

bool isLower(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 0x430 && c <= 0x45F) || (c >= 0xDF && c <= 0xFF && c != 0xF7) || c == 0x491;
}

bool isUpper(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 0x400 && c <= 0x42F) || (c >= 0xC0 && c <= 0xDE && c != 0xD7) || c == 0x490;
}

bool isLetter(int c)
{
    return isLower(c) || isUpper(c) || (c >= 0x100 && c < 0x250) || (c >= 0x370 && c < 0x530);
}

int lowerOf(int c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0x410 && c <= 0x42F) || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
        return c + 0x20;
    if (c >= 0x400 && c <= 0x40F)
        return c + 0x50;
    return c == 0x490 ? 0x491 : c;
}

/// A hyphen at the end of a line; a recognition program writes it as "not" at times.
bool isHyphen(int c)
{
    return c == '-' || c == 0x2010 || c == 0x2011 || c == 0xAD || c == 0xAC;
}

bool endsSentence(int c)
{
    return c == '.' || c == '!' || c == '?' || c == ':' || c == ';' || c == 0x2026 || c == '"' || c == 0xBB
           || c == 0x201D || c == ')';
}

/// The letters of a text in lower case: what stays of a running title when its page number and
/// the marks around it are taken away.
std::string keyOf(const std::string & s)
{
    std::string key;
    for (size_t i = 0; i < s.size();) {
        const int c = charAt(s, i);
        if (isLetter(c))
            appendUtf8(key, lowerOf(c));
    }
    return key;
}

/// A page number: digits with marks around them and nothing else.
bool onlyNumber(const std::string & s)
{
    bool digit = false;
    if (s.size() > 12)
        return false;
    for (size_t i = 0; i < s.size();) {
        const int c = charAt(s, i);
        if (c >= '0' && c <= '9')
            digit = true;
        else if (!(c == ' ' || c == '-' || c == '.' || c == '[' || c == ']' || c == '(' || c == ')' || c == '|'
                   || c == 0x2013 || c == 0x2014))
            return false;
    }
    return digit;
}

/// The value of a page number written with Roman numerals, as the pages of a preface are: "xvi",
/// "XXIII", alone or with the marks a page number has around it; 0 when the text is not one. Up
/// to 399 (no D and no M): a longer front matter is not met, and words made of these letters
/// ("dim", "mix") are.
int romanValue(const std::string & s)
{
    std::string letters;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (c == 'i' || c == 'v' || c == 'x' || c == 'l' || c == 'c')
            letters += c;
        else if (!(c == ' ' || c == '-' || c == '.' || c == '[' || c == ']' || c == '(' || c == ')' || c == '|'))
            return 0;
    }
    if (letters.empty() || letters.size() > 9)
        return 0;
    // hundreds, tens, units, each written the one way a number is written
    static const char * const parts[3][10] = {
        { "", "c", "cc", "ccc", "", "", "", "", "", "" },
        { "", "x", "xx", "xxx", "xl", "l", "lx", "lxx", "lxxx", "xc" },
        { "", "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix" }
    };
    size_t at = 0;
    int value = 0;
    for (int rank = 0, unit = 100; rank < 3; rank++, unit /= 10) {
        size_t best = 0;
        int digit = 0;
        for (int d = 1; d < 10; d++) {
            const size_t n = strlen(parts[rank][d]);
            if (n > best && letters.compare(at, n, parts[rank][d]) == 0) {
                best = n;
                digit = d;
            }
        }
        at += best;
        value += digit * unit;
    }
    return at == letters.size() ? value : 0;
}

/// The value of the Roman number a line begins or ends with, words beside it ("xiv Preface to the
/// Fourth Edition", "Contents vii", and as well "the reign of Louis XIV"); 0 when it has none, or
/// is the number alone.
int romanAtEdge(const std::string & s)
{
    size_t from = 0, to = s.size();
    while (from < to && s[from] == ' ')
        from++;
    while (to > from && s[to - 1] == ' ')
        to--;
    const size_t firstEnd = s.find(' ', from);
    if (firstEnd == std::string::npos || firstEnd >= to)
        return 0;
    const int first = romanValue(s.substr(from, firstEnd - from));
    if (first)
        return first;
    const size_t lastStart = s.rfind(' ', to - 1);
    return romanValue(s.substr(lastStart + 1, to - lastStart - 1));
}

/// The text of a zone without the line ends and other control characters it carries, with single
/// spaces.
std::string clean(const char * raw, size_t length)
{
    std::string text;
    bool space = false;
    for (size_t i = 0; i < length; i++) {
        const unsigned char c = (unsigned char)raw[i];
        if (c <= ' ' || c == 0x7F) {
            space = !text.empty();
            continue;
        }
        size_t more = 0;
        if (c >= 0x80) {
            // a whole character of UTF-8 or nothing: a text layer holds stray bytes at times
            more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
            bool good = c >= 0xC2 && c <= 0xF4 && i + more < length;
            for (size_t k = 1; good && k <= more; k++)
                good = ((unsigned char)raw[i + k] & 0xC0) == 0x80;
            if (good && more == 2) {
                const int value = ((c & 0x0F) << 12) | (((unsigned char)raw[i + 1] & 0x3F) << 6)
                                  | ((unsigned char)raw[i + 2] & 0x3F);
                good = value >= 0x800 && !(value >= 0xD800 && value <= 0xDFFF) && value < 0xFFFE;
            }
            if (!good)
                continue;
        }
        if (space)
            text += ' ';
        space = false;
        text.append(raw + i, more + 1);
        i += more;
    }
    return text;
}

/// A row of dots between the text of a line and a number (a contents page) becomes one mark.
/// True when the line had one.
bool takeLeaders(std::string & text)
{
    std::string made;
    bool found = false;
    for (size_t i = 0; i < text.size();) {
        size_t j = i;
        int dots = 0;
        for (;;) {
            if (j < text.size() && (text[j] == '.' || text[j] == ' ')) {
                dots += text[j] == '.' ? 1 : 0;
                j++;
            } else if (j + 2 < text.size() && text.compare(j, 3, "\xE2\x80\xA6") == 0) {
                dots += 3;
                j += 3;
            } else if (j + 1 < text.size() && text.compare(j, 2, "\xC2\xB7") == 0) {
                dots++;
                j += 2;
            } else {
                break;
            }
        }
        // a number, Arabic or Roman, and nothing else after the dots
        bool number = dots >= 4 && j < text.size() && text.size() - j <= 8;
        for (size_t k = j; number && k < text.size(); k++)
            number = strchr("0123456789ivxlcIVXLC", text[k]) != NULL;
        if (number) {
            while (!made.empty() && made[made.size() - 1] == ' ')
                made.erase(made.size() - 1);
            made += " \xE2\x80\xA6 ";
            found = true;
            i = j;
        } else if (j > i) {
            made.append(text, i, j - i);
            i = j;
        } else {
            made += text[i++];
        }
    }
    if (found) {
        while (!made.empty() && made[made.size() - 1] == ' ')
            made.erase(made.size() - 1);
        text = made;
    }
    return found;
}

/// Adds the next line to a text: a word cut at the end of the line is put together.
void addLine(std::string & text, const std::string & next)
{
    if (text.empty()) {
        text = next;
        return;
    }
    const size_t at = lastAt(text);
    size_t i = at;
    if (at > 0 && isHyphen(charAt(text, i)) && isLower(firstChar(next))) {
        size_t before = at - 1;
        while (before > 0 && ((unsigned char)text[before] & 0xC0) == 0x80)
            before--;
        size_t k = before;
        if (isLetter(charAt(text, k))) {
            text.erase(at);
            text += next;
            return;
        }
    }
    text += ' ';
    text += next;
}

/// A line that begins an item of a list: a bullet, or a number with a dot or a bracket.
bool startsListItem(const std::string & text, bool afterSentence)
{
    size_t i = 0;
    if (text.empty())
        return false;
    const int c = charAt(text, i);
    if (c == 0x2022 || c == 0x25E6 || c == 0x25AA || c == 0x25A0 || c == 0x25CF || c == 0x25CB || c == 0x2023
            || c == 0xB7 || c == '*')
        return i < text.size() && text[i] == ' ';
    if (!afterSentence || c < '0' || c > '9')
        return false;
    size_t k = 1;
    while (k < text.size() && k < 3 && text[k] >= '0' && text[k] <= '9')
        k++;
    return k + 1 < text.size() && (text[k] == '.' || text[k] == ')') && text[k + 1] == ' ';
}

/// How many words of four letters and more a text has: a formula has none or one.
int longWords(const std::string & text)
{
    int words = 0, letters = 0;
    for (size_t i = 0; i <= text.size();) {
        const int c = i < text.size() ? charAt(text, i) : (i++, 0);
        if (isLetter(c)) {
            letters++;
        } else {
            if (letters >= 4)
                words++;
            letters = 0;
        }
    }
    return words;
}

/// A sign that stands in formulas and hardly in a text.
bool hasMathSign(const std::string & text)
{
    for (size_t i = 0; i < text.size();) {
        const int c = charAt(text, i);
        if (c == '=' || c == 0xB1 || c == 0xD7 || c == 0xF7 || (c >= 0x2200 && c <= 0x22FF))
            return true;
    }
    return false;
}

float medianOf(std::vector<float> & values, float share = 0.5f)
{
    if (values.empty())
        return 0;
    size_t at = (size_t)(values.size() * share);
    if (at >= values.size())
        at = values.size() - 1;
    std::nth_element(values.begin(), values.begin() + at, values.end());
    return values[at];
}

// ---------------------------------------------------------------------------- the text layer
struct Word {
    float x0, y0, x1, y1;
    std::string text;
};

/// A formula inside a line of text that is shown as a picture in the line. The text of the line
/// carries a mark in its place: the byte 1, the number of the formula, the byte 2.
struct Inline {
    int page;
    float x0, y0, x1, y1;
    std::string text;       // as the file gives it: written when the picture could not be made
    int picture;            // -1: not made
};

struct Line {
    float x0, y0, x1, y1;   // pixels of the page, y down
    std::string text;
    std::vector<Word> words;    // as read; made into the text, then dropped (finishPage)
    int para;               // the paragraph zone of the file it stands in, -1: none
    bool entry;             // a line of a contents page: a text, a row of dots, a number
    bool out;               // a running title or a page number: left out
    int cells;              // pieces of the row set wide apart, with a tab between them in the text
    std::vector<float> starts;  // where its pieces after the first begin
    float firstWidth;       // of its first word, by its share of the letters
    Line() : x0(0), y0(0), x1(0), y1(0), para(-1), entry(false), out(false), cells(1), firstWidth(0) { }
    float height() const { return y1 - y0; }
};

struct Page {
    float width, height;    // of the page zone of the text layer; 0: the page has no text
    int dpi;                // pixels of the page in an inch
    std::vector<Line> lines;
    long chars;
    Page() : width(0), height(0), dpi(300), chars(0) { }
};

struct Walk {
    Page * page;
    int paras;
    int line;               // the line being filled, -1: none
};

void addText(Walk & w, const int * box, const char * raw, int para, bool inLine)
{
    if (raw == NULL)
        return;
    Page & p = *w.page;
    if (inLine && w.line >= 0) {
        Word word;
        word.text = clean(raw, strlen(raw));
        if (word.text.empty())
            return;
        word.x0 = (float)box[0];
        word.x1 = (float)box[2];
        word.y0 = p.height - box[3];
        word.y1 = p.height - box[1];
        p.lines[w.line].words.push_back(word);
        return;
    }
    // a text given above the lines (a paragraph or a page as one string): its lines share the box
    std::vector<std::string> parts;
    for (const char * from = raw; *from;) {
        const char * end = strchr(from, '\n');
        const size_t length = end ? (size_t)(end - from) : strlen(from);
        const std::string text = clean(from, length);
        if (!text.empty())
            parts.push_back(text);
        from += length + (end ? 1 : 0);
    }
    const float top = p.height - box[3], bottom = p.height - box[1];
    for (size_t k = 0; k < parts.size(); k++) {
        Line line;
        line.x0 = (float)box[0];
        line.x1 = (float)box[2];
        line.y0 = top + (bottom - top) * k / parts.size();
        line.y1 = top + (bottom - top) * (k + 1) / parts.size();
        line.text = parts[k];
        line.para = para;
        p.lines.push_back(line);
    }
}

/// A zone of the text layer: (type x0 y0 x1 y1 then zones, or the text), y up from the bottom.
void walk(Walk & w, miniexp_t zone, int para, bool inLine, int depth)
{
    if (!miniexp_consp(zone) || !miniexp_symbolp(miniexp_car(zone)) || depth > 12)
        return;
    const char * name = miniexp_to_name(miniexp_car(zone));
    miniexp_t rest = miniexp_cdr(zone);
    int box[4];
    for (int i = 0; i < 4; i++) {
        if (!miniexp_consp(rest) || !miniexp_numberp(miniexp_car(rest)))
            return;
        box[i] = miniexp_to_int(miniexp_car(rest));
        rest = miniexp_cdr(rest);
    }
    Page & p = *w.page;
    if (p.height <= 0) {
        // the first zone is the page
        p.width = (float)box[2];
        p.height = (float)box[3];
    }
    if (strcmp(name, "para") == 0)
        para = w.paras++;
    const bool opens = strcmp(name, "line") == 0 && !inLine;
    if (opens) {
        Line line;
        line.x0 = (float)box[0];
        line.x1 = (float)box[2];
        line.y0 = p.height - box[3];
        line.y1 = p.height - box[1];
        line.para = para;
        p.lines.push_back(line);
        w.line = (int)p.lines.size() - 1;
    }
    for (; miniexp_consp(rest); rest = miniexp_cdr(rest)) {
        miniexp_t item = miniexp_car(rest);
        if (miniexp_stringp(item))
            addText(w, box, miniexp_to_str(item), para, inLine || opens);
        else
            walk(w, item, para, inLine || opens, depth + 1);
    }
    if (opens)
        w.line = -1;
}

/// True when two rows of pieces have their pieces at the same places: every piece of the row
/// with fewer of them begins where a piece of the other does.
bool sameColumns(const Line & a, const Line & b)
{
    if (a.cells < 2 || b.cells < 2)
        return false;
    const Line & few = a.cells <= b.cells ? a : b;
    const Line & many = a.cells <= b.cells ? b : a;
    const float near = 0.5f * std::min(a.height(), b.height());
    for (size_t i = 0; i < few.starts.size(); i++) {
        bool found = false;
        for (size_t k = 0; k < many.starts.size() && !found; k++)
            found = fabsf(few.starts[i] - many.starts[k]) < near;
        if (!found)
            return false;
    }
    return true;
}

/// A sign whose formula the characters of a line cannot tell: a root, a sum, a product, an integral.
bool hasBigSign(const std::string & text)
{
    for (size_t i = 0; i < text.size();) {
        const int c = charAt(text, i);
        if (c == 0x221A || c == 0x221B || c == 0x2211 || c == 0x220F || (c >= 0x222B && c <= 0x2233))
            return true;
    }
    return false;
}

/// A word that is part of a formula and not a word of the text: short, without three letters in
/// a row, with a digit, a bracket or a sign in it, or a single Latin or Greek letter.
bool isFormulaWord(const std::string & text)
{
    int letters = 0, count = 0, only = 0;
    bool mark = false;
    for (size_t i = 0; i < text.size();) {
        const int c = charAt(text, i);
        only = c;
        count++;
        letters = isLetter(c) ? letters + 1 : 0;
        if (letters >= 3 || count > 8)
            return false;
        if ((c >= '0' && c <= '9') || (c < 128 && strchr("=+-/()[]{}^<>|*", c)) || c == 0xB1 || c == 0xD7 || c == 0xB7
                || (c >= 0x2200 && c <= 0x22FF))
            mark = true;
    }
    return mark || (count == 1 && ((only >= 'a' && only <= 'z') || (only >= 'A' && only <= 'Z')
                                   || (only >= 0x391 && only <= 0x3C9)));
}

/// The words of a line make its text. A formula inside the line that has a root, a sum or the
/// like (from the first word with such a sign to the last one, with the words of a formula
/// around them) is cut out of the page later and leaves a mark in the text; a line that is all
/// formula is left to the rule for formulas on lines of their own. Words set wide apart may be
/// the cells of a row of a table: a tab between them, and where each begins.
void makeText(Line & line, int page, std::vector<Inline> * inlines)
{
    std::vector<Word> & words = line.words;
    if (words.empty())
        return;
    size_t from = words.size(), to = 0;
    for (size_t i = 0; i < words.size(); i++)
        if (hasBigSign(words[i].text)) {
            from = std::min(from, i);
            to = i + 1;
        }
    if (inlines != NULL && from < to) {
        while (from > 0 && isFormulaWord(words[from - 1].text))
            from--;
        while (to < words.size() && isFormulaWord(words[to].text))
            to++;
        // the mark of the sentence after the formula stays in the text
        std::string & last = words[to - 1].text;
        std::string tail;
        while (last.size() > 1 && strchr(".,;:", last[last.size() - 1])) {
            tail.insert(tail.begin(), last[last.size() - 1]);
            last.erase(last.size() - 1);
        }
        if (to - from < words.size()) {
            Inline formula;
            formula.page = page;
            formula.picture = -1;
            formula.x0 = words[from].x0;
            formula.x1 = words[to - 1].x1;
            formula.y0 = line.y0;
            formula.y1 = line.y1;
            for (size_t i = from; i < to; i++) {
                formula.text += i > from ? " " : "";
                formula.text += words[i].text;
            }
            char mark[24];
            snprintf(mark, sizeof(mark), "\x01%d\x02", (int)inlines->size());
            inlines->push_back(formula);
            Word made = words[from];
            made.x1 = words[to - 1].x1;
            made.text = mark + tail;
            words.erase(words.begin() + from, words.begin() + to);
            words.insert(words.begin() + from, made);
        } else {
            last += tail;
        }
    }
    const float height = line.height();
    for (size_t i = 0; i < words.size(); i++) {
        if (i > 0) {
            const bool cell = words[i].x0 - words[i - 1].x1 > 1.3f * height;
            line.text += cell ? '\t' : ' ';
            if (cell) {
                line.cells++;
                line.starts.push_back(words[i].x0);
            }
        }
        line.text += words[i].text;
    }
    std::vector<Word>().swap(words);
}

/// The lines as they will be used: the empty ones go; the pieces of one row that the file gives
/// as lines of their own (the words of a justified line, the cells of a row) make one line.
void finishPage(Page & p, int number, std::vector<Inline> * inlines)
{
    std::vector<Line> lines;
    for (size_t i = 0; i < p.lines.size(); i++) {
        Line & line = p.lines[i];
        makeText(line, number, inlines);
        if (line.text.empty() || line.y1 <= line.y0)
            continue;
        if (!lines.empty()) {
            Line & last = lines.back();
            const float overlap = std::min(last.y1, line.y1) - std::max(last.y0, line.y0);
            const float least = std::min(last.height(), line.height());
            if (last.para == line.para && overlap > 0.6f * least && line.x0 >= last.x1 - 0.2f * last.height()) {
                // set apart: may be the cells of a row of a table
                const bool cell = line.x0 - last.x1 > 1.3f * least;
                last.text += cell ? '\t' : ' ';
                if (cell) {
                    last.cells++;
                    last.starts.push_back(line.x0);
                }
                // (the pieces of the piece itself, found by its words)
                last.cells += line.cells - 1;
                last.starts.insert(last.starts.end(), line.starts.begin(), line.starts.end());
                last.text += line.text;
                last.x1 = line.x1;
                last.y0 = std::min(last.y0, line.y0);
                last.y1 = std::max(last.y1, line.y1);
                continue;
            }
        }
        lines.push_back(line);
    }
    p.lines.swap(lines);
    // the pieces are cells when the row beside has its cells at the same places; else they are
    // the words of a line spread wide (a narrow justified column)
    std::vector<bool> row(p.lines.size(), false);
    for (size_t i = 0; i + 1 < p.lines.size(); i++)
        if (sameColumns(p.lines[i], p.lines[i + 1]))
            row[i] = row[i + 1] = true;
    // a row beside rows is one too, its pieces standing elsewhere (the head of a table)
    for (size_t i = 0; i < p.lines.size(); i++)
        if (!row[i] && p.lines[i].cells > 1 && ((i > 0 && row[i - 1]) || (i + 1 < p.lines.size() && row[i + 1])))
            row[i] = true;
    // In a file without paragraphs nothing tells a row from a line of a narrow justified column
    // whose words stand wide apart: there a table is three rows and more, or rows of three
    // cells and more.
    for (size_t i = 0; i < p.lines.size();) {
        size_t j = i;
        int least = 1000;
        while (j < p.lines.size() && row[j] && p.lines[j].para < 0) {
            least = std::min(least, p.lines[j].cells);
            j++;
        }
        if (j > i && j - i < 3 && least < 3)
            for (size_t k = i; k < j; k++)
                row[k] = false;
        i = j > i ? j : i + 1;
    }
    // a paragraph of the file that has a usual line of several words is a text, all of it
    for (size_t i = 0; i < p.lines.size();) {
        size_t j = i;
        bool plain = false;
        while (j < p.lines.size() && p.lines[j].para == p.lines[i].para) {
            plain = plain || (p.lines[j].cells == 1 && p.lines[j].text.find(' ') != std::string::npos);
            j++;
        }
        if (plain && p.lines[i].para >= 0)
            for (size_t k = i; k < j; k++)
                row[k] = false;
        i = j;
    }
    p.chars = 0;
    for (size_t i = 0; i < p.lines.size(); i++) {
        Line & line = p.lines[i];
        if (line.cells > 1 && !row[i]) {
            std::replace(line.text.begin(), line.text.end(), '\t', ' ');
            line.cells = 1;
        }
        line.entry = line.cells == 1 && takeLeaders(line.text);
        const size_t space = line.text.find_first_of(" \t");
        line.firstWidth = (line.x1 - line.x0) * (space == std::string::npos ? 1.0f : (float)space / line.text.size());
        p.chars += (long)line.text.size();
    }
}

struct Djvu {
    ddjvu_context_t * ctx;
    ddjvu_document_t * doc;
    ddjvu_format_t * format;
    int count;
    Djvu() : ctx(NULL), doc(NULL), format(NULL), count(0) { }
    ~Djvu() {
        if (format)
            ddjvu_format_release(format);
        if (doc)
            ddjvu_document_release(doc);
        if (ctx)
            ddjvu_context_release(ctx);
    }
    void pump(bool wait) {
        if (wait)
            ddjvu_message_wait(ctx);
        while (ddjvu_message_peek(ctx))
            ddjvu_message_pop(ctx);
    }
    bool open(const char * path) {
        // (the library looks for the program along PATH when the context is made: 60 ms under WSL
        // with the folders of Windows in PATH, half a millisecond with a short one)
        ctx = ddjvu_context_create("cr3");
        doc = ctx ? ddjvu_document_create_by_filename_utf8(ctx, path, TRUE) : NULL;
        if (!doc)
            return false;
        while (!ddjvu_document_decoding_done(doc))
            pump(true);
        if (ddjvu_document_decoding_error(doc))
            return false;
        count = ddjvu_document_get_pagenum(doc);
        format = ddjvu_format_create(DDJVU_FORMAT_GREY8, 0, NULL);
        ddjvu_format_set_row_order(format, 1);
        ddjvu_format_set_y_direction(format, 1);
        return count > 0;
    }
    /// The text layer of a page: read from the file without decoding the page. inlines: where
    /// the formulas found inside its lines go; NULL: they are left as text.
    void text(int number, Page & page, std::vector<Inline> * inlines) {
        // the header of the page tells its resolution
        ddjvu_pageinfo_t info;
        ddjvu_status_t status;
        while ((status = ddjvu_document_get_pageinfo(doc, number, &info)) < DDJVU_JOB_OK)
            pump(true);
        if (status == DDJVU_JOB_OK && info.dpi >= 50 && info.dpi <= 2400)
            page.dpi = info.dpi;
        miniexp_t layer;
        while ((layer = ddjvu_document_get_pagetext(doc, number, "word")) == miniexp_dummy)
            pump(true);
        if (miniexp_consp(layer)) {
            Walk w;
            w.page = &page;
            w.paras = 0;
            w.line = -1;
            walk(w, layer, -1, false, 0);
        }
        if (layer != miniexp_nil)
            ddjvu_miniexp_release(doc, layer);
        finishPage(page, number, inlines);
    }
};

// ---------------------------------------------------------------------------- the book
enum { ITEM_TEXT = 0, ITEM_HEADING, ITEM_SUBTITLE, ITEM_ENTRY, ITEM_PICTURE, ITEM_VERSE, ITEM_ROW };

struct Item {
    int kind;
    int page, endPage;      // where it begins and ends
    float y0, y1;           // on its first page
    std::string text;
    float height;           // the mean height of its lines
    int lines;
    bool centred;
    int picture;            // ITEM_PICTURE: its number
    bool dropped;           // a heading that became the title of a section
    float x0;               // of its first line
    int formula;            // the place it is cut out of as a picture when it is a formula, -1: none
    Item() : kind(ITEM_TEXT), page(0), endPage(0), y0(0), y1(0), height(0), lines(0), centred(false), picture(-1),
             dropped(false), x0(0), formula(-1) { }
};

struct Place {              // a part of a page with no text in it
    int page;
    float y0, y1;
    bool whole;             // the page has no text at all
    int formula;            // a formula given as text: its number, -1: a place with no text
    float x0, x1;           // x1 > x0: only this part of the width (beside the text, or a formula in a line)
    int inlined;            // a formula inside a line: its number in the list of them, -1: none
    Place() : page(0), y0(0), y1(0), whole(false), formula(-1), x0(0), x1(0), inlined(-1) { }
};

struct Cut {                // a picture cut out of a page
    int x0, y0, x1, y1;     // pixels of the page
    int width;              // of the page
    long from, to;          // where it lies in the file of the pictures
    bool free;              // from a place with no text: may be an ornament of the pages
    bool dropped;
    unsigned char look[64]; // the picture made 8 by 8: tells the same from the alike
};

struct Piece {              // what is drawn in a place: pixels of the page
    size_t place;
    float x0, y0, x1, y1;
    bool atTop, atBottom;   // reaches the top, the bottom edge of the place
    int limitY0, limitY1;   // the place: the cut does not go over it
    bool joined;            // put together of pieces over lines of text
};

bool placeBefore(const Place & a, const Place & b)
{
    return a.page < b.page;
}

struct Section {
    size_t item;           // the first item of the section
    std::vector<std::string> titles;
};

struct Entry {              // of the contents of the file
    int page;
    std::string title;
};

void quiet(void *, const char *) { }

void writeBase64(FILE * f, const unsigned char * data, size_t length)
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

void escaped(FILE * f, const std::string & s)
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

class Translator {
public:
    Djvu djvu;
    std::vector<Page> pages;
    std::vector<Item> items;
    std::vector<Section> sections;
    PbDjvuTextStats stats;
    FILE * binaries;
    fz_context * fz;
    float body;             // the height of a line of the body text
    float leading;          // from a line to the next
    float blockTop, blockBottom; // of the text block of a usual page
    bool cover;
    int pictures;
    int formulas;           // places of formulas given as text
    std::vector<bool> formulaCut;   // by the number of the place: the picture is made
    std::vector<Cut> cuts;  // by the number of the picture
    std::vector<Inline> inlines;    // the formulas inside lines, by the number in their marks
    ddjvu_page_t * deck;    // the page being decoded ahead
    int deckNo;
    bool dumping;

    Translator() : binaries(NULL), fz(NULL), body(0), leading(0), blockTop(0), blockBottom(0), cover(false),
                   pictures(0), formulas(0), deck(NULL), deckNo(-1), dumping(getenv("PB_DJVUTEXT_DUMP") != NULL)
    {
        memset(&stats, 0, sizeof(stats));
    }
    ~Translator()
    {
        // before the document goes (the member djvu is released after this)
        if (deck)
            ddjvu_page_release(deck);
        if (fz)
            fz_drop_context(fz);
    }

    /// The height of a body line and the step of the lines: the middle ones of the file. The long
    /// lines are taken for the height: they have both tall letters and letters with tails.
    void findSizes()
    {
        std::vector<float> heights, all, steps;
        long inZones = 0, total = 0, zones = 0;
        for (size_t n = 0; n < pages.size(); n++) {
            const std::vector<Line> & lines = pages[n].lines;
            int last = -1;
            for (size_t i = 0; i < lines.size(); i++) {
                all.push_back(lines[i].height());
                if (lines[i].text.size() >= 40)
                    heights.push_back(lines[i].height());
                total++;
                if (lines[i].para >= 0) {
                    inZones++;
                    if (lines[i].para != last)
                        zones++;
                    last = lines[i].para;
                }
            }
        }
        body = medianOf(heights.size() >= 20 ? heights : all);
        for (size_t n = 0; n < pages.size(); n++) {
            const std::vector<Line> & lines = pages[n].lines;
            for (size_t i = 1; i < lines.size(); i++) {
                const float step = lines[i].y0 - lines[i - 1].y0;
                if (step > 0.6f * body && step < 3 * body)
                    steps.push_back(step);
            }
        }
        leading = steps.empty() ? body * 1.25f : medianOf(steps);
        // the paragraphs of the file are taken when they look like paragraphs: not one for a line
        // and not one for a page
        const float perZone = zones > 0 ? (float)inZones / zones : 0;
        stats.zones = zones > 0 && inZones * 10 >= total * 9 && perZone >= 1.4f && perZone <= 14;
    }

    /// A picture of text the recognition could not read (a sample of another script, the small
    /// print of a drawing) comes out as a cloud of lines of a mark or two, well under half as
    /// tall as the text. Five of them on a page and more are not text: they leave the layer,
    /// and the place they stood in is then looked at as a picture. (In the book they were a
    /// column of stray quotes and commas, a paragraph each.) Fewer are left alone: a footnote
    /// mark may be such a line.
    void dropNoise()
    {
        std::vector<float> heights, all;
        for (size_t n = 0; n < pages.size(); n++)
            for (size_t i = 0; i < pages[n].lines.size(); i++) {
                all.push_back(pages[n].lines[i].height());
                if (pages[n].lines[i].text.size() >= 40)
                    heights.push_back(pages[n].lines[i].height());
            }
        const float usual = medianOf(heights.size() >= 20 ? heights : all);
        if (usual <= 0)
            return;
        for (size_t n = 0; n < pages.size(); n++) {
            Page & p = pages[n];
            std::vector<bool> stray(p.lines.size(), false), marks(p.lines.size(), false);
            int count = 0;
            for (size_t i = 0; i < p.lines.size(); i++) {
                const std::string & text = p.lines[i].text;
                int letters = 0, digits = 0;
                for (size_t k = 0; k < text.size();) {
                    const int c = charAt(text, k);
                    letters += isLetter(c) ? 1 : 0;
                    digits += c >= '0' && c <= '9' ? 1 : 0;
                }
                stray[i] = p.lines[i].height() < 0.4f * usual && (text.size() <= 6 || letters < 3);
                // (on such a page a line of a mark or two is of the cloud whatever its height)
                marks[i] = letters == 0 && digits == 0 && text.size() <= 3;
                count += stray[i] ? 1 : 0;
            }
            if (count < 5)
                continue;
            std::vector<Line> kept;
            p.chars = 0;
            for (size_t i = 0; i < p.lines.size(); i++)
                if (!stray[i] && !marks[i]) {
                    kept.push_back(p.lines[i]);
                    p.chars += (long)p.lines[i].text.size();
                }
            p.lines.swap(kept);
            stats.stray += count;
        }
    }

    /// The Roman number of a line at the top or the bottom of a page: the line is the number, or
    /// begins or ends with it (beside: a line of the size of the text or smaller, not a long one).
    int romanOf(const Line & line, bool & beside) const
    {
        beside = false;
        int number = romanValue(line.text);
        if (number == 0 && line.text.size() <= 100 && line.height() <= body * 1.2f) {
            number = romanAtEdge(line.text);
            beside = true;
        }
        return number;
    }

    /// What repeats at the top and at the bottom of the pages goes, and the page numbers there.
    void dropRunning()
    {
        for (int round = 0; round < 2; round++) {
            std::vector<int> tops(pages.size(), -1), bottoms(pages.size(), -1);
            std::map<std::string, int> seen[2];
            for (size_t n = 0; n < pages.size(); n++) {
                const std::vector<Line> & lines = pages[n].lines;
                for (size_t i = 0; i < lines.size(); i++) {
                    if (lines[i].out)
                        continue;
                    if (tops[n] < 0 || lines[i].y0 < lines[tops[n]].y0)
                        tops[n] = (int)i;
                    if (bottoms[n] < 0 || lines[i].y1 > lines[bottoms[n]].y1)
                        bottoms[n] = (int)i;
                }
                if (tops[n] >= 0 && tops[n] != bottoms[n]) {
                    seen[0][keyOf(lines[tops[n]].text)]++;
                    seen[1][keyOf(lines[bottoms[n]].text)]++;
                }
            }
            // A number in Roman numerals is a page number when it goes with the pages (the pages
            // of a preface): the next page has the next number, so the number less the page is
            // the same on all of them. Two lines that are such a number alone make page numbers,
            // and three that begin or end with one (the running title of a front matter, "xiv
            // Preface": the title may stand on a page or two and does not repeat thrice). A count
            // of such lines without the pages would take the headings "II", "III" of chapters and
            // lines of the text that end with a name: "Louis XIV", "World War II".
            std::map<int, int> inStep;      // the number less the page -> lines
            for (size_t n = 0; n < pages.size(); n++) {
                if (tops[n] < 0 || tops[n] == bottoms[n])
                    continue;
                for (int side = 0; side < 2; side++) {
                    bool beside;
                    const int number = romanOf(pages[n].lines[side == 0 ? tops[n] : bottoms[n]], beside);
                    if (number)
                        inStep[number - (int)n]++;
                }
            }
            for (size_t n = 0; n < pages.size(); n++) {
                if (tops[n] < 0 || tops[n] == bottoms[n])
                    continue;
                for (int side = 0; side < 2; side++) {
                    Line & line = pages[n].lines[side == 0 ? tops[n] : bottoms[n]];
                    const std::string key = keyOf(line.text);
                    // a running title is of the size of the text or smaller; a title of a slide
                    // that repeats is larger and stays
                    const int times = seen[side][key];
                    const bool repeats = line.text.size() <= 160
                                         && ((key.size() >= 3 && times >= 3 && line.height() <= body * 1.2f)
                                             || (key.size() >= 2 && times >= 4 && times * 2 >= stats.textPages));
                    bool beside;
                    const int number = romanOf(line, beside);
                    const bool pageNumber = number && inStep[number - (int)n] >= (beside ? 3 : 2);
                    if (!line.out && (onlyNumber(line.text) || repeats || pageNumber)) {
                        line.out = true;
                        stats.running++;
                    }
                }
            }
        }
    }

    /// Where the text block of a usual page begins and ends.
    void findBlock()
    {
        std::vector<float> tops, bottoms;
        for (size_t n = 0; n < pages.size(); n++) {
            const std::vector<Line> & lines = pages[n].lines;
            float top = 0, bottom = 0;
            int kept = 0;
            for (size_t i = 0; i < lines.size(); i++) {
                if (lines[i].out)
                    continue;
                if (kept == 0 || lines[i].y0 < top)
                    top = lines[i].y0;
                if (kept == 0 || lines[i].y1 > bottom)
                    bottom = lines[i].y1;
                kept++;
            }
            if (kept >= 8) {
                tops.push_back(top);
                bottoms.push_back(bottom);
            }
        }
        // the usual full page: a page that ends higher (the end of a chapter) is in the smaller part
        blockTop = medianOf(tops, 0.3f);
        blockBottom = medianOf(bottoms, 0.7f);
    }

    void pushItem(Item & item, int linesIn, float heights)
    {
        if (item.text.empty())
            return;
        item.lines = linesIn;
        item.height = linesIn > 0 ? heights / linesIn : 0;
        items.push_back(item);
    }

    /// The lines of a page make paragraphs: by the paragraphs of the file, or by indents and gaps.
    void takePage(int number)
    {
        const Page & p = pages[number];
        std::vector<const Line *> lines;
        for (size_t i = 0; i < p.lines.size(); i++)
            if (!p.lines[i].out)
                lines.push_back(&p.lines[i]);
        if (lines.empty())
            return;
        // the edges of the text block of the page, by its long lines
        float widest = 0;
        for (size_t i = 0; i < lines.size(); i++)
            widest = std::max(widest, lines[i]->x1 - lines[i]->x0);
        std::vector<float> lefts, rights;
        for (size_t i = 0; i < lines.size(); i++)
            if (lines[i]->x1 - lines[i]->x0 >= 0.6f * widest) {
                lefts.push_back(lines[i]->x0);
                rights.push_back(lines[i]->x1);
            }
        const float left = medianOf(lefts, 0.1f), right = medianOf(rights, 0.9f);
        // The right edge of the block a line stands in: the longest of the few lines around it
        // that begin about where it does. The margin of the page is no measure: a page has blocks
        // set in (quotations, lists) and blocks beside each other (columns).
        std::vector<float> edge(lines.size(), 0);
        for (size_t i = 0; i < lines.size(); i++) {
            const size_t from = i >= 4 ? i - 4 : 0, to = std::min(lines.size(), i + 5);
            for (size_t j = from; j < to; j++)
                if (fabsf(lines[j]->x0 - lines[i]->x0) < 2.5f * body && fabsf(lines[j]->y0 - lines[i]->y0) < 6 * leading)
                    edge[i] = std::max(edge[i], lines[j]->x1);
        }

        // Verse, and other text whose lines are ends of lines on purpose (a listing): three lines
        // and more of one size, where the first word of a line would mostly have fitted at the end
        // of the line before. In a file with paragraphs the lines of one paragraph are looked at;
        // else the lines that begin one under another, set in from the margin further than the
        // first line of a paragraph (so that a talk of short paragraphs is not taken for verse).
        std::vector<bool> verse(lines.size(), false);
        for (size_t i = 0; i < lines.size();) {
            size_t j = i;
            float runRight = 0;
            while (j < lines.size() && lines[j]->cells == 1 && !lines[j]->entry
                   && lines[j]->height() < 1.35f * body
                   && (stats.zones ? lines[j]->para >= 0 && lines[j]->para == lines[i]->para
                                   : lines[j]->x0 >= left + 3.5f * body && fabsf(lines[j]->x0 - lines[i]->x0) < 0.8f * body)
                   && (j == i || (lines[j]->y0 > lines[j - 1]->y0 && lines[j]->y0 - lines[j - 1]->y0 < 2.6f * leading))) {
                runRight = std::max(runRight, lines[j]->x1);
                j++;
            }
            int fitted = 0;
            for (size_t k = i + 1; k < j; k++)
                if (lines[k - 1]->x1 + lines[k]->firstWidth + 0.5f * body < std::max(runRight, std::max(edge[k - 1], edge[k])))
                    fitted++;
            if (j - i >= 3 && fitted * 10 >= (int)(j - i - 1) * 6)
                for (size_t k = i; k < j; k++)
                    verse[k] = true;
            i = j > i ? j : i + 1;
        }

        Item item;
        int count = 0;
        float heights = 0, itemRight = 0;
        for (size_t i = 0; i < lines.size(); i++) {
            const Line & b = *lines[i];
            // the line after b, when it stands right under it
            const Line * c = i + 1 < lines.size() ? lines[i + 1] : NULL;
            if (c && (c->y0 <= b.y0 || c->y0 - b.y0 > 1.7f * leading))
                c = NULL;
            bool breaks = i == 0;
            bool stanza = false;
            if (i > 0) {
                const Line & a = *lines[i - 1];
                const float tall = std::max(a.height(), b.height()), low = std::min(a.height(), b.height());
                if (verse[i] || verse[i - 1]) {
                    breaks = verse[i] != verse[i - 1];
                    stanza = !breaks && b.y0 - a.y0 > 1.5f * leading;
                } else if (a.entry || b.entry || a.cells > 1 || b.cells > 1) {
                    breaks = true;
                } else if (startsListItem(b.text, endsSentence(lastChar(a.text)))) {
                    breaks = true;
                } else if (tall > 1.6f * low) {
                    breaks = true;      // another size
                } else if (stats.zones) {
                    breaks = a.para != b.para || b.para < 0;
                } else if (b.y0 < a.y0 - 0.5f * body) {
                    breaks = true;      // goes up: another column
                } else if (b.y0 - a.y0 > std::max(1.7f * leading, 1.5f * tall)) {
                    breaks = true;      // a gap
                } else {
                    // by the shift of the line against the one before: the margin of a page is no
                    // measure, a page has blocks set in (footnotes, quotations, lists)
                    // The line before ends its paragraph when the first word of this one would
                    // have fitted after it: true for a ragged right edge as for an even one.
                    const float shift = b.x0 - a.x0;
                    const float localRight = std::max(std::max(itemRight, edge[i - 1]), edge[i]);
                    const bool shortA = a.x1 + b.firstWidth + 0.5f * body < localRight;
                    if (shift > 0.8f * body) {
                        // begins further right: the first line of a paragraph; or the second line
                        // of a paragraph whose first line hangs out (a footnote, a list of books)
                        const bool hanging = count == 1 && !shortA && c != NULL && fabsf(c->x0 - b.x0) < 0.8f * body;
                        breaks = !hanging;
                    } else if (shift < -0.8f * body) {
                        // comes back to the margin: the paragraph goes on after its first line
                        breaks = !(count == 1 && !shortA);
                    } else {
                        // paragraphs without an indent
                        breaks = shortA;
                    }
                }
            }
            if (breaks) {
                pushItem(item, count, heights);
                item = Item();
                item.kind = verse[i] ? ITEM_VERSE : b.cells > 1 ? ITEM_ROW : b.entry ? ITEM_ENTRY : ITEM_TEXT;
                item.page = item.endPage = number;
                item.y0 = b.y0;
                item.x0 = b.x0 - left;
                item.centred = b.x0 > left + 2 * body && b.x1 < right - 2 * body
                               && fabsf((b.x0 + b.x1) - (left + right)) < 3 * body;
                count = 0;
                heights = 0;
                itemRight = 0;
            }
            if (item.kind == ITEM_VERSE) {
                // the lines stay lines; an empty one between stanzas
                if (!item.text.empty())
                    item.text += stanza ? "\n\n" : "\n";
                item.text += b.text;
            } else {
                addLine(item.text, b.text);
            }
            item.y1 = b.y1;
            itemRight = std::max(itemRight, b.x1);
            count++;
            heights += b.height();
        }
        pushItem(item, count, heights);
    }

    // ------------------------------------------------------------------------ pictures
    /// A formula set on a line of its own comes out of the recognition as a row of signs. It is
    /// cut out of the page as a picture instead, with the short pieces right above and under it
    /// (the parts of a fraction). The items stay until the picture is made.
    void markFormulas(std::vector<Place> & places)
    {
        for (size_t i = 0; i < items.size(); i++) {
            Item & item = items[i];
            if (item.kind != ITEM_TEXT || item.lines > 3 || item.text.size() > 160
                    || !hasMathSign(item.text) || longWords(item.text) > 1)
                continue;
            Place place;
            place.page = item.page;
            place.whole = false;
            place.formula = formulas++;
            size_t first = i, last = i;
            while (first > 0 && isFormulaPiece(items[first - 1], items[first]))
                first--;
            while (last + 1 < items.size() && isFormulaPiece(items[last + 1], items[last]))
                last++;
            place.y0 = items[first].y0 - 0.25f * body;
            place.y1 = items[last].y1 + 0.25f * body;
            for (size_t k = first; k <= last; k++)
                items[k].formula = place.formula;
            places.push_back(place);
            i = last;
        }
    }

    /// A short piece without words right above or under a formula, on the same page.
    bool isFormulaPiece(const Item & piece, const Item & beside) const
    {
        if (piece.kind != ITEM_TEXT || piece.formula >= 0 || piece.page != beside.page || piece.text.size() > 12
                || longWords(piece.text) > 0)
            return false;
        const float gap = piece.y0 > beside.y0 ? piece.y0 - beside.y1 : beside.y0 - piece.y1;
        return gap < leading;
    }

    /// The parts of a page inside the text block where no line of the text layer stands.
    void placesOf(int number, std::vector<Place> & places)
    {
        const Page & p = pages[number];
        const size_t mine = places.size();
        Place place;
        place.page = number;
        if (p.lines.empty()) {
            // the first page is the cover already
            if (number > 0 || pages.size() == 1) {
                place.y0 = 0;
                place.y1 = 0;
                place.whole = true;
                places.push_back(place);
            }
            return;
        }
        if (body <= 0 || blockBottom <= blockTop)
            return;
        std::vector<std::pair<float, float> > spans;
        for (size_t i = 0; i < p.lines.size(); i++)
            spans.push_back(std::make_pair(p.lines[i].y0, p.lines[i].y1));
        std::sort(spans.begin(), spans.end());
        const float least = std::max(3 * leading, 2.5f * body);
        float at = blockTop;
        for (size_t i = 0; i < spans.size(); i++) {
            // inside the text block only: what stands under it (the ornament of a slide, the
            // rule over a page number) is not a picture of the text
            const float to = std::min(spans[i].first, blockBottom);
            if (to - at >= least) {
                place.y0 = at;
                place.y1 = to;
                places.push_back(place);
            }
            at = std::max(at, spans[i].second);
        }
        if (blockBottom - at >= least) {
            place.y0 = at;
            place.y1 = blockBottom;
            places.push_back(place);
        }
        // Beside the text: a picture the lines flow around. The text block is cut into bands a
        // line step high; in a band the room between the lines of the layer and an edge of the
        // block is free. A run of bands with text in three of them and more, with 25 mm free at
        // one side over 20 mm of height, is a place of that side: from the text to the edge of
        // the page. (The lines of verse and of a list leave such room too: the page is decoded
        // and nothing is found there.)
        const float mm = p.dpi / 25.4f;
        float left = 1e9f, right = -1e9f;
        for (size_t i = 0; i < p.lines.size(); i++)
            if (!p.lines[i].out) {
                left = std::min(left, p.lines[i].x0);
                right = std::max(right, p.lines[i].x1);
            }
        const int bands = leading > 0 ? (int)((blockBottom - blockTop) / leading) : 0;
        if (bands < 3 || bands > 500 || right - left < 50 * mm)
            return;
        std::vector<float> freeSide[2];
        freeSide[0].assign(bands, -1);  // at the right; -1: no text in the band
        freeSide[1].assign(bands, -1);  // at the left
        for (size_t i = 0; i < p.lines.size(); i++) {
            const Line & line = p.lines[i];
            const int b0 = std::max(0, (int)((line.y0 - blockTop) / leading));
            const int b1 = std::min(bands - 1, (int)((line.y1 - 1 - blockTop) / leading));
            for (int b = b0; b <= b1; b++) {
                const float atRight = std::max(0.0f, right - line.x1), atLeft = std::max(0.0f, line.x0 - left);
                freeSide[0][b] = freeSide[0][b] < 0 ? atRight : std::min(freeSide[0][b], atRight);
                freeSide[1][b] = freeSide[1][b] < 0 ? atLeft : std::min(freeSide[1][b], atLeft);
            }
        }
        const size_t over = places.size();
        for (int side = 0; side < 2; side++) {
            const std::vector<float> & room = freeSide[side];
            for (int b = 0; b < bands;) {
                int e = b, withText = 0;
                float narrowest = 1e9f;
                while (e < bands && (room[e] < 0 || room[e] >= 25 * mm)) {
                    if (room[e] >= 0) {
                        withText++;
                        narrowest = std::min(narrowest, room[e]);
                    }
                    e++;
                }
                int s = b, t = e;
                while (s < t && room[s] < 0)
                    s++;
                while (t > s && room[t - 1] < 0)
                    t--;
                if (withText >= 3 && (t - s) * leading >= 20 * mm) {
                    Place beside;
                    beside.page = number;
                    beside.y0 = blockTop + s * leading;
                    beside.y1 = blockTop + t * leading;
                    beside.x0 = side == 0 ? right - narrowest + 0.5f * body : 1;
                    beside.x1 = side == 0 ? p.width : left + narrowest - 0.5f * body;
                    // not where a place over the whole width is looked at already
                    bool taken = false;
                    for (size_t k = mine; k < over && !taken; k++)
                        taken = std::min(beside.y1, places[k].y1) - std::max(beside.y0, places[k].y0)
                                > 0.5f * (beside.y1 - beside.y0);
                    if (!taken)
                        places.push_back(beside);
                }
                b = e > b ? e : b + 1;
            }
        }
    }

    bool renderPart(ddjvu_page_t * page, int reduction, int width, int height, int x, int y, int w, int h,
                    std::vector<unsigned char> & pixels)
    {
        ddjvu_rect_t whole, part;
        whole.x = 0;
        whole.y = 0;
        whole.w = (unsigned)((width + reduction - 1) / reduction);
        whole.h = (unsigned)((height + reduction - 1) / reduction);
        if (x < 0 || y < 0 || w < 1 || h < 1 || x + w > (int)whole.w || y + h > (int)whole.h)
            return false;
        part.x = x;
        part.y = y;
        part.w = (unsigned)w;
        part.h = (unsigned)h;
        pixels.assign((size_t)w * h, 255);
        ddjvu_page_render(page, DDJVU_RENDER_COLOR, &whole, &part, djvu.format, w, (char *)&pixels[0]);
        return true;
    }

    /// Packs a gray picture as a JPEG into the binaries of the book.
    bool writePicture(const char * id, const unsigned char * gray, int w, int h, int quality)
    {
        if (!binaries)
            return false;
        if (!fz) {
            fz = fz_new_context(NULL, NULL, 1 << 20);
            if (!fz)
                return false;
            fz_set_warning_callback(fz, quiet, NULL);
            fz_set_error_callback(fz, quiet, NULL);
        }
        fz_pixmap * pix = NULL;
        fz_buffer * packed = NULL;
        unsigned char * data = NULL;
        size_t length = 0;
        fz_var(pix);
        fz_var(packed);
        fz_var(data);
        fz_var(length);
        fz_try(fz) {
            pix = fz_new_pixmap_with_data(fz, fz_device_gray(fz), w, h, NULL, 0, w, (unsigned char *)gray);
            packed = fz_new_buffer_from_pixmap_as_jpeg(fz, pix, fz_default_color_params, quality, 0);
            length = fz_buffer_storage(fz, packed, &data);
        }
        fz_always(fz) {
            fz_drop_pixmap(fz, pix);
        }
        fz_catch(fz) {
            data = NULL;
        }
        const bool ok = data != NULL && length > 0;
        if (ok) {
            fprintf(binaries, "<binary id=\"%s\" content-type=\"image/jpeg\">", id);
            writeBase64(binaries, data, length);
            fputs("</binary>\n", binaries);
        }
        fz_drop_buffer(fz, packed);
        return ok;
    }

    /// Decodes a page and cuts out of it what is drawn in the places with no text. The page is
    /// looked at on a small picture of it first: most places are the blank end of a chapter.
    void takePictures(int number, int nextNumber, const std::vector<Place> & places, size_t from, size_t to,
                      std::vector<Item> & found)
    {
        const double t0 = nowMs();
        ddjvu_page_t * page = deck != NULL && deckNo == number ? deck : ddjvu_page_create_by_pageno(djvu.doc, number);
        if (deck != NULL && deckNo != number)
            ddjvu_page_release(deck);
        // the page that will be wanted after this one starts being decoded now, in a thread of
        // the library: on the second core of the reader, while this one is looked at
        deck = nextNumber >= 0 ? ddjvu_page_create_by_pageno(djvu.doc, nextNumber) : NULL;
        deckNo = nextNumber;
        if (!page)
            return;
        while (!ddjvu_page_decoding_done(page))
            djvu.pump(true);
        stats.decoded++;
        stats.decodeMs += nowMs() - t0;
        const int width = ddjvu_page_get_width(page), height = ddjvu_page_get_height(page);
        if (ddjvu_page_decoding_error(page) || width < 16 || height < 16) {
            ddjvu_page_release(page);
            return;
        }
        std::vector<unsigned char> pixels;
        if (number == 0 && !cover) {
            const int reduction = std::min(12, std::max(1, (width + 599) / 600));
            const int w = (width + reduction - 1) / reduction, h = (height + reduction - 1) / reduction;
            if (renderPart(page, reduction, width, height, 0, 0, w, h, pixels))
                cover = writePicture("cover", &pixels[0], w, h, 75);
        }
        // the text layer counts in the pixels of the page as it lies in the file; a page the file
        // tells to show turned is left alone
        const Page & p = pages[number];
        const bool turned = ddjvu_page_get_initial_rotation(page) != DDJVU_ROTATE_0;
        const float scale = p.width > 0 ? width / p.width : 1;
        const int reduction = std::min(12, std::max(1, (width + 699) / 700));
        const int sw = (width + reduction - 1) / reduction, sh = (height + reduction - 1) / reduction;
        // pixels of the page in a millimetre: the sizes a picture has to have are those of paper
        int dpi = ddjvu_page_get_resolution(page);
        if (dpi < 50 || dpi > 2400)
            dpi = 300;
        const float mm = dpi / 25.4f;
        // The small picture holds only the rows the places of the page stand in (half of the time
        // of looking at a page was the drawing of rows nobody looked at).
        int base = 0, end = sh;
        if (from < to) {
            bool whole = false;
            float y0 = 1e9f, y1 = -1e9f;
            for (size_t k = from; k < to; k++) {
                whole = whole || places[k].whole;
                y0 = std::min(y0, places[k].y0);
                y1 = std::max(y1, places[k].y1);
            }
            if (!whole) {
                base = std::max(0, (int)(y0 * scale / reduction) - 1);
                end = std::min(sh, (int)(y1 * scale / reduction) + 2);
            }
        }
        std::vector<unsigned char> small;
        if (from < to && (end <= base || !renderPart(page, reduction, width, height, 0, base, sw, end - base, small)))
            to = from;
        int dark = 0;
        if (from < to) {
            // The paper is the most usual light gray of the page, and what is clearly darker is
            // drawn. The page is looked at whole for it, at its coarsest: a place may be all picture.
            const int coarse = 12;
            const int cw = (width + coarse - 1) / coarse, ch = (height + coarse - 1) / coarse;
            std::vector<long> levels(256, 0);
            if (renderPart(page, coarse, width, height, 0, 0, cw, ch, pixels))
                for (size_t i = 0; i < pixels.size(); i++)
                    levels[pixels[i]]++;
            int paper = 255;
            for (int v = 100; v < 256; v++)
                if (levels[v] > levels[paper])
                    paper = v;
            dark = std::max(30, paper - 60);
        }
        std::vector<Piece> pieces;
        for (size_t k = from; k < to; k++) {
            const Place & place = places[k];
            if (turned && !place.whole)
                continue;
            if (place.inlined >= 0) {
                // A formula inside a line: the part of the page its words stand on, drawn so
                // that its letters are about as tall as those of a book on the screen.
                const float tall = (place.y1 - place.y0) * scale;
                const int fine = std::min(12, std::max(1, (int)(tall / 40 + 0.5f)));
                const int x0 = std::max(0, (int)(place.x0 * scale - 0.15f * tall));
                const int x1 = std::min(width, (int)(place.x1 * scale + 0.15f * tall));
                const int y0 = std::max(0, (int)(place.y0 * scale - 0.1f * tall));
                const int y1 = std::min(height, (int)(place.y1 * scale + 0.1f * tall));
                const int fw = (width + fine - 1) / fine, fh = (height + fine - 1) / fine;
                const int px = x0 / fine, py = y0 / fine;
                const int pw = std::min(fw - px, (x1 - x0 + fine - 1) / fine), ph = std::min(fh - py, (y1 - y0 + fine - 1) / fine);
                char id[32];
                snprintf(id, sizeof(id), "pic%d", pictures);
                Cut cut;
                memset(&cut, 0, sizeof(cut));
                cut.from = ftell(binaries);
                if (pw >= 4 && ph >= 4 && renderPart(page, fine, width, height, px, py, pw, ph, pixels)
                        && writePicture(id, &pixels[0], pw, ph, 80)) {
                    cut.to = ftell(binaries);
                    cuts.push_back(cut);
                    inlines[place.inlined].picture = pictures++;
                    stats.formulas++;
                }
                continue;
            }
            const bool beside = place.x1 > place.x0;
            int r0 = 0, r1 = sh;
            if (!place.whole) {
                // a place between lines is taken a little inside, a formula a little outside (done)
                // (the boxes of the lines of a text layer are not exact: a place taken to the very
                // lines brings pieces of letters with it)
                const float inside = place.formula >= 0 ? 0 : 0.35f * body;
                r0 = std::max(0, (int)((place.y0 + inside) * scale / reduction));
                r1 = std::min(sh, (int)((place.y1 - inside) * scale / reduction) + 1);
            }
            // the edges of a scan are often dark
            int c0 = sw * 3 / 100, c1 = sw - c0;
            if (beside) {
                c0 = std::max(c0, (int)(place.x0 * scale / reduction));
                c1 = std::min(c1, (int)(place.x1 * scale / reduction) + 1);
            }
            r0 = std::max(r0, place.whole ? sh * 2 / 100 : base);
            r1 = std::min(r1, place.whole ? sh - sh * 2 / 100 : end);
            if (r1 - r0 < 3 || c1 - c0 < 3)
                continue;
            std::vector<int> rows(r1 - r0, 0), cols(c1 - c0, 0);
            for (int r = r0; r < r1; r++) {
                const unsigned char * row = &small[(size_t)(r - base) * sw];
                for (int c = c0; c < c1; c++)
                    if (row[c] < dark)
                        cols[c - c0]++;
            }
            // A line from the top of the place to its bottom is a frame or a rule between columns,
            // a line across it a rule: neither is a picture, and neither is counted.
            std::vector<bool> rule(cols.size(), false);
            if (place.formula < 0)
                for (size_t c = 0; c < cols.size(); c++)
                    if (cols[c] * 100 >= (r1 - r0) * 85) {
                        rule[c] = true;
                        if (c > 0)
                            rule[c - 1] = true;
                        if (c + 1 < cols.size())
                            rule[c + 1] = true;
                    }
            std::fill(cols.begin(), cols.end(), 0);
            for (int r = r0; r < r1; r++) {
                const unsigned char * row = &small[(size_t)(r - base) * sw];
                int count = 0;
                for (int c = c0; c < c1; c++)
                    if (row[c] < dark && !rule[c - c0])
                        count++;
                if (place.formula < 0 && count * 100 >= (c1 - c0) * 85)
                    continue;
                rows[r - r0] = count;
                for (int c = c0; c < c1; c++)
                    if (row[c] < dark && !rule[c - c0])
                        cols[c - c0]++;
            }
            int top = 0, bottom = (int)rows.size() - 1, left = 0, right = (int)cols.size() - 1;
            while (top <= bottom && rows[top] < 2)
                top++;
            while (bottom >= top && rows[bottom] < 2)
                bottom--;
            while (left <= right && cols[left] < 2)
                left++;
            while (right >= left && cols[right] < 2)
                right--;
            if (top > bottom || left > right)
                continue;
            long ink = 0;
            for (int r = top; r <= bottom; r++)
                ink += rows[r];
            const float bw = (float)(right - left + 1) * reduction, bh = (float)(bottom - top + 1) * reduction;
            const float lineHeight = body > 0 ? body * scale : height * 0.012f;
            // What counts as a picture, by its size on paper, its shape and how much of it is
            // drawn: at least 12 mm wide and 5 mm high (and higher than a line of the text: less
            // is the rest of a line, a rule, a mark), not a narrow strip, and not a few specks.
            // A page with no text has to have something of a size on it.
            // (beside the text 15 mm each way: less there is a mark in the margin)
            const bool large = place.whole ? bw >= 0.2f * width && bh >= 0.08f * height
                               : beside ? bw >= 15 * mm && bh >= 15 * mm
                                        : bw >= 12 * mm && bh >= std::max(5 * mm, 1.2f * lineHeight);
            const bool strip = bw > 12 * bh || bh > 12 * bw;
            // (beside the text more of it: a frame and a few marks are not a picture there)
            const bool drawn = ink * (beside ? 50 : 150) >= (long)(right - left + 1) * (bottom - top + 1);
            if (place.formula < 0 && (!large || strip || !drawn))
                continue;
            // Beside the text a picture stands clear of it. What reaches the edge of the place
            // on the side of the text goes on under the text: a table, a frame around the text,
            // a ground. It is not cut in two.
            if (beside && (place.x0 > 1 ? left <= 1 : right + 2 >= (int)cols.size()))
                continue;
            // what is found, in pixels of the page; cut out when all the places are looked at
            Piece piece;
            piece.place = k;
            piece.x0 = (c0 + left) * reduction;
            piece.x1 = (c0 + right + 1) * reduction;
            piece.y0 = (r0 + top) * reduction;
            piece.y1 = (r0 + bottom + 1) * reduction;
            piece.atTop = top <= 1;
            piece.atBottom = bottom + 2 >= (int)rows.size();
            piece.limitY0 = place.whole ? 0 : (int)(place.y0 * scale);
            piece.limitY1 = place.whole ? height : (int)(place.y1 * scale) + 1;
            piece.joined = false;
            pieces.push_back(piece);
        }
        // A drawing with words in it (a scheme, a table of marks): its words are lines of the
        // text layer, and the drawing is found as a piece that reaches the edge of its place: it
        // goes on under the lines of text there. The piece is grown over those lines, row by row
        // while something is drawn in the rows, to the first white band; the lines are then in
        // the picture. Text set in lines has white bands between its lines, so a picture that
        // only touches a line takes that line and stops. A piece that finds no white band within
        // twelve lines (a rule down the page) is left as it was.
        const int limit = (int)(12 * leading * scale / reduction);
        {
            // the rows above and under the places are wanted for it: the small picture is drawn
            // again with them, on the pages that have such a piece
            int wantBase = base, wantEnd = end;
            for (size_t k = 0; k < pieces.size(); k++) {
                const Place & place = places[pieces[k].place];
                if (place.whole || place.formula >= 0 || place.x1 > place.x0)
                    continue;
                if (pieces[k].atTop)
                    wantBase = std::min(wantBase, std::max(0, (int)(pieces[k].y0 / reduction) - limit));
                if (pieces[k].atBottom)
                    wantEnd = std::max(wantEnd, std::min(sh, (int)(pieces[k].y1 / reduction) + limit));
            }
            if ((wantBase < base || wantEnd > end)
                    && renderPart(page, reduction, width, height, 0, wantBase, sw, wantEnd - wantBase, pixels)) {
                small.swap(pixels);
                base = wantBase;
                end = wantEnd;
            }
        }
        // The lines that run down the rows at hand (the frame of a slide, a rule between columns)
        // are not part of a drawing: not counted.
        const int c0 = sw * 3 / 100, c1 = sw - c0;
        const int blockR0 = (int)(blockTop * scale / reduction), blockR1 = (int)(blockBottom * scale / reduction);
        const int beyond = (blockR1 - blockR0) / 10;
        std::vector<bool> down(sw, false);
        bool growing = false;
        for (size_t k = 0; k < pieces.size(); k++)
            growing = growing || pieces[k].atTop || pieces[k].atBottom;
        if (growing && end > base)
            for (int c = c0; c < c1; c++) {
                int count = 0;
                for (int r = base; r < end; r++)
                    count += small[(size_t)(r - base) * sw + c] < dark ? 1 : 0;
                if (count * 100 >= (end - base) * 70) {
                    down[c] = true;
                    down[c > 0 ? c - 1 : c] = true;
                    down[c + 1 < sw ? c + 1 : c] = true;
                }
            }
        for (size_t k = 0; k < pieces.size(); k++) {
            Piece & piece = pieces[k];
            const Place & place = places[piece.place];
            if (place.whole || place.formula >= 0 || place.x1 > place.x0)
                continue;
            for (int way = -1; way <= 1; way += 2) {
                if (way < 0 ? !piece.atTop : !piece.atBottom)
                    continue;
                int r = way < 0 ? (int)(piece.y0 / reduction) - 1 : (int)(piece.y1 / reduction);
                int reached = r - way, white = 0, steps = 0, x0 = sw, x1 = 0;
                bool closed = false;
                for (; r >= base && r < end && steps < limit; r += way, steps++) {
                    // Not out of the text block: what goes on from a picture into the margin with
                    // no white between (the ground of a slide, the ornament of a running title)
                    // is not the picture. A tenth of the block is allowed for a picture that
                    // stands a little lower than the text of the other pages ends.
                    if (way > 0 ? r > blockR1 + beyond : r < blockR0 - beyond) {
                        closed = true;
                        reached = way > 0 ? std::min(reached, blockR1) : std::max(reached, blockR0);
                        break;
                    }
                    const unsigned char * row = &small[(size_t)(r - base) * sw];
                    int count = 0, rowX0 = sw, rowX1 = 0;
                    for (int c = c0; c < c1; c++)
                        if (row[c] < dark && !down[c]) {
                            count++;
                            rowX0 = std::min(rowX0, c);
                            rowX1 = std::max(rowX1, c);
                        }
                    // (a line across is passed over: the rule of a table has rows under it, the
                    // frame of a slide has white)
                    if (count * 100 >= (c1 - c0) * 85)
                        continue;
                    if (count >= 2) {
                        white = 0;
                        reached = r;
                        x0 = std::min(x0, rowX0);
                        x1 = std::max(x1, rowX1);
                    } else if (++white >= 3) {
                        closed = true;
                        break;
                    }
                }
                if (dumping)
                    fprintf(stderr, "page %d: a piece at %.0f..%.0f grows %s: %d rows looked at, %s, reached %d (rows %d..%d at hand)\n",
                            number + 1, piece.y0, piece.y1, way < 0 ? "up" : "down", steps, closed ? "white found" : "no white",
                            reached * reduction, base * reduction, end * reduction);
                if (!closed || reached == (way < 0 ? (int)(piece.y0 / reduction) : (int)(piece.y1 / reduction) - 1))
                    continue;
                if (way < 0) {
                    piece.y0 = (float)reached * reduction;
                    piece.limitY0 = std::min(piece.limitY0, (int)piece.y0);
                    piece.atTop = false;
                } else {
                    piece.y1 = (float)(reached + 1) * reduction;
                    piece.limitY1 = std::max(piece.limitY1, (int)piece.y1);
                    piece.atBottom = false;
                }
                if (x1 >= x0) {
                    piece.x0 = std::min(piece.x0, (float)x0 * reduction);
                    piece.x1 = std::max(piece.x1, (float)(x1 + 1) * reduction);
                }
                piece.joined = true;
            }
        }
        // Pieces that meet, or reach the edges of their places over a few lines of text, are
        // one picture.
        for (size_t a = 0; a + 1 < pieces.size();) {
            Piece & upper = pieces[a];
            const Piece & lower = pieces[a + 1];
            const Place & pa = places[upper.place];
            const Place & pb = places[lower.place];
            const bool plain = !pa.whole && !pb.whole && pa.formula < 0 && pb.formula < 0 && pa.x1 <= pa.x0 && pb.x1 <= pb.x0;
            if (plain && upper.y1 >= lower.y0 - reduction) {
                upper.x0 = std::min(upper.x0, lower.x0);
                upper.x1 = std::max(upper.x1, lower.x1);
                upper.y1 = std::max(upper.y1, lower.y1);
                upper.limitY1 = std::max(upper.limitY1, lower.limitY1);
                upper.atBottom = lower.atBottom;
                upper.joined = true;
                pieces.erase(pieces.begin() + a + 1);
                continue;
            }
            if (plain && upper.atBottom && lower.atTop && lower.limitY0 >= upper.limitY1
                    && lower.limitY0 - upper.limitY1 <= 3.5f * leading * scale) {
                upper.x0 = std::min(upper.x0, lower.x0);
                upper.x1 = std::max(upper.x1, lower.x1);
                upper.y1 = lower.y1;
                upper.limitY1 = lower.limitY1;
                upper.atBottom = lower.atBottom;
                upper.joined = true;
                pieces.erase(pieces.begin() + a + 1);
                continue;
            }
            a++;
        }
        for (size_t k = 0; k < pieces.size(); k++) {
            const Piece & piece = pieces[k];
            const Place & place = places[piece.place];
            const bool beside = place.x1 > place.x0;
            const float lineHeight = body > 0 ? body * scale : height * 0.012f;
            // the picture itself: no more than 1200 pixels wide, and a scan of 400 dpi and more
            // at half its pixels at least
            const float margin = 0.4f * lineHeight;
            int x0 = (int)(piece.x0 - margin), y0 = (int)(piece.y0 - margin);
            int x1 = (int)(piece.x1 + margin), y1 = (int)(piece.y1 + margin);
            x0 = std::max(0, x0);
            x1 = std::min(width, x1);
            if (beside) {
                // nor into the lines beside it
                x0 = std::max(x0, (int)(place.x0 * scale));
                x1 = std::min(x1, (int)(place.x1 * scale) + 1);
            }
            // the white around it does not reach into the lines above and under the place
            y0 = std::max(piece.limitY0, y0);
            y1 = std::min(piece.limitY1, y1);
            y0 = std::max(0, y0);
            y1 = std::min(height, y1);
            int fine = (x1 - x0 + 1199) / 1200;
            if (ddjvu_page_get_resolution(page) >= 400)
                fine = std::max(fine, 2);
            fine = std::min(12, std::max(1, fine));
            const int fw = (width + fine - 1) / fine, fh = (height + fine - 1) / fine;
            const int px = x0 / fine, py = y0 / fine;
            const int pw = std::min(fw - px, (x1 - x0 + fine - 1) / fine), ph = std::min(fh - py, (y1 - y0 + fine - 1) / fine);
            const double t1 = nowMs();
            if (pw < 8 || ph < 8 || !renderPart(page, fine, width, height, px, py, pw, ph, pixels))
                continue;
            char id[32];
            snprintf(id, sizeof(id), "pic%d", pictures);
            Cut cut;
            cut.from = ftell(binaries);
            const bool written = writePicture(id, &pixels[0], pw, ph, 70);
            stats.cutMs += nowMs() - t1;
            if (!written)
                continue;
            cut.to = ftell(binaries);
            cut.x0 = x0;
            cut.y0 = y0;
            cut.x1 = x1;
            cut.y1 = y1;
            cut.width = width;
            cut.free = place.formula < 0 && !place.whole;
            cut.dropped = false;
            // how it looks, 8 by 8: the mean gray of the parts of what was found
            const int ra0 = (int)(piece.y0 / reduction), ra1 = (int)(piece.y1 / reduction);
            const int ca0 = (int)(piece.x0 / reduction), ca1 = (int)(piece.x1 / reduction);
            for (int gy = 0; gy < 8; gy++)
                for (int gx = 0; gx < 8; gx++) {
                    const int ra = ra0 + (ra1 - ra0) * gy / 8, rb = ra0 + (ra1 - ra0) * (gy + 1) / 8;
                    const int ca = ca0 + (ca1 - ca0) * gx / 8, cb = ca0 + (ca1 - ca0) * (gx + 1) / 8;
                    long sum = 0, n = 0;
                    for (int r = std::max(ra, base); r < std::max(rb, ra + 1) && r < end; r++)
                        for (int c = ca; c < std::max(cb, ca + 1) && c < sw; c++) {
                            sum += small[(size_t)(r - base) * sw + c];
                            n++;
                        }
                    cut.look[gy * 8 + gx] = (unsigned char)(n > 0 ? sum / n : 255);
                }
            cuts.push_back(cut);
            Item item;
            item.kind = ITEM_PICTURE;
            item.page = item.endPage = number;
            item.y0 = y0 / scale;
            item.y1 = y1 / scale;
            item.picture = pictures++;
            // (a picture put together over lines of text has those lines in it)
            item.dropped = piece.joined;
            found.push_back(item);
            if (place.formula >= 0) {
                if ((int)formulaCut.size() <= place.formula)
                    formulaCut.resize(place.formula + 1, false);
                formulaCut[place.formula] = true;
                stats.formulas++;
            }
        }
        ddjvu_page_release(page);
    }

    static bool before(const Item & a, const Item & b)
    {
        return a.page != b.page ? a.page < b.page : a.y0 < b.y0;
    }

    /// A picture that stands at the same place of three pages and more and looks the same there
    /// is an ornament of the pages (a vignette, the mark of a publisher, a frame). It is kept
    /// where it stands first and left out after that: an ornament once does no harm, and a
    /// drawing a book really repeats is not lost.
    void dropOrnaments(std::vector<Item> & found)
    {
        if (cuts.size() > 3000)
            return;
        std::vector<bool> same(cuts.size(), false);
        for (size_t i = 0; i < cuts.size(); i++) {
            const Cut & a = cuts[i];
            if (!a.free)
                continue;
            int alike = 0;
            bool first = true;
            for (size_t j = 0; j < cuts.size(); j++) {
                const Cut & b = cuts[j];
                const int near = a.width * 15 / 1000;
                if (j == i || !b.free || abs(a.x0 - b.x0) > near || abs(a.x1 - b.x1) > near || abs(a.y0 - b.y0) > near
                        || abs(a.y1 - b.y1) > near)
                    continue;
                long difference = 0;
                for (int k = 0; k < 64; k++)
                    difference += abs((int)a.look[k] - (int)b.look[k]);
                if (difference < 64 * 16) {
                    alike++;
                    first = first && j > i;
                }
            }
            same[i] = alike >= 2 && !first;
        }
        std::vector<Item> kept;
        for (size_t i = 0; i < found.size(); i++) {
            const int number = found[i].picture;
            if (number >= 0 && number < (int)same.size() && same[number]) {
                cuts[number].dropped = true;
                stats.ornaments++;
            } else {
                kept.push_back(found[i]);
            }
        }
        found.swap(kept);
    }

    /// The pictures take their places among the paragraphs: by the page and the height on it.
    void placePictures(std::vector<Item> & found)
    {
        dropOrnaments(found);
        if (found.empty())
            return;
        std::sort(found.begin(), found.end(), before);
        std::vector<Item> all;
        all.reserve(items.size() + found.size());
        size_t next = 0;
        for (size_t i = 0; i < items.size(); i++) {
            const int formula = items[i].formula;
            if (formula >= 0 && formula < (int)formulaCut.size() && formulaCut[formula])
                continue;
            // the words of a drawing that is one picture now are in the picture
            bool inside = false;
            for (size_t k = 0; k < found.size() && !inside; k++)
                inside = found[k].dropped && found[k].page == items[i].page && items[i].endPage == items[i].page
                         && items[i].y0 >= found[k].y0 && items[i].y1 <= found[k].y1;
            if (inside)
                continue;
            while (next < found.size() && (found[next].page < items[i].page
                    || (found[next].page == items[i].page && found[next].y0 < items[i].y0))) {
                all.push_back(found[next++]);
                all.back().dropped = false;
            }
            all.push_back(items[i]);
        }
        while (next < found.size()) {
            all.push_back(found[next++]);
            all.back().dropped = false;
        }
        items.swap(all);
    }

    // ------------------------------------------------------------------------ the whole
    void classify()
    {
        for (size_t i = 0; i < items.size(); i++) {
            Item & item = items[i];
            if (item.kind == ITEM_ROW) {
                const bool beside = (i > 0 && items[i - 1].kind == ITEM_ROW && items[i - 1].page == item.page)
                                    || (i + 1 < items.size() && items[i + 1].kind == ITEM_ROW
                                        && items[i + 1].page == item.page);
                if (!beside) {
                    std::replace(item.text.begin(), item.text.end(), '\t', ' ');
                    item.kind = ITEM_TEXT;
                }
            }
            if (item.kind != ITEM_TEXT)
                continue;
            const int first = firstChar(item.text), last = lastChar(item.text);
            if (keyOf(item.text).size() < 2 || isLower(first))
                continue;
            if (body > 0 && item.lines <= 3 && item.text.size() <= 300 && item.height >= 1.35f * body)
                item.kind = ITEM_HEADING;
            else if (item.lines == 1 && item.centred && item.text.size() <= 200 && last != '.' && last != ',')
                item.kind = ITEM_SUBTITLE;
        }
    }

    /// A paragraph cut by the end of a page goes on with the first one of the next page.
    void joinPages()
    {
        for (size_t i = 0; i + 1 < items.size();) {
            Item & a = items[i];
            const Item & b = items[i + 1];
            if (a.kind == ITEM_TEXT && b.kind == ITEM_TEXT && b.page == a.endPage + 1
                    && !endsSentence(lastChar(a.text)) && isLower(firstChar(b.text))) {
                addLine(a.text, b.text);
                a.endPage = b.endPage;
                a.lines += b.lines;
                items.erase(items.begin() + i + 1);
                stats.joined++;
                continue;
            }
            i++;
        }
    }

    void readOutline(miniexp_t list, std::vector<Entry> & entries, int depth)
    {
        for (; miniexp_consp(list) && depth < 8; list = miniexp_cdr(list)) {
            miniexp_t node = miniexp_car(list);
            if (!miniexp_consp(node) || !miniexp_stringp(miniexp_car(node)))
                continue;
            Entry entry;
            entry.page = -1;
            const char * title = miniexp_to_str(miniexp_car(node));
            entry.title = clean(title, strlen(title));
            miniexp_t rest = miniexp_cdr(node);
            if (miniexp_consp(rest) && miniexp_stringp(miniexp_car(rest))) {
                const char * url = miniexp_to_str(miniexp_car(rest));
                if (url && url[0] == '#') {
                    char * end = NULL;
                    const long number = strtol(url + 1, &end, 10);
                    if (end != url + 1 && *end == 0)
                        entry.page = (int)number - 1;
                    else
                        entry.page = ddjvu_document_search_pageno(djvu.doc, url + 1);
                }
                rest = miniexp_cdr(rest);
            }
            if (entry.page >= 0 && entry.page < (int)pages.size() && !entry.title.empty())
                entries.push_back(entry);
            readOutline(rest, entries, depth + 1);
        }
    }

    /// The sections of the book: by the contents of the file, else by the headings found.
    void findSections()
    {
        std::vector<Entry> entries;
        miniexp_t outline;
        while ((outline = ddjvu_document_get_outline(djvu.doc)) == miniexp_dummy)
            djvu.pump(true);
        if (miniexp_consp(outline) && miniexp_symbolp(miniexp_car(outline)))
            readOutline(miniexp_cdr(outline), entries, 0);
        if (outline != miniexp_nil)
            ddjvu_miniexp_release(djvu.doc, outline);
        size_t at = 0;
        for (size_t e = 0; e < entries.size(); e++) {
            // the first item of the page of the entry; the heading with the words of the entry,
            // when it stands among the first items of the page, begins the section and goes
            while (at < items.size() && items[at].endPage < entries[e].page)
                at++;
            if (at >= items.size())
                break;
            size_t start = at;
            const std::string key = keyOf(entries[e].title);
            for (size_t i = at; i < items.size() && i < at + 6 && items[i].page <= entries[e].page; i++) {
                if (items[i].kind == ITEM_PICTURE || items[i].dropped || items[i].text.size() > 300)
                    continue;
                const std::string have = keyOf(items[i].text);
                const size_t n = std::min(have.size(), key.size());
                if (n >= 4 && have.compare(0, n, key, 0, n) == 0 && n * 2 >= std::max(have.size(), key.size())) {
                    start = i;
                    items[i].dropped = true;
                    break;
                }
            }
            if (!sections.empty() && sections.back().item == start) {
                sections.back().titles.push_back(entries[e].title);
            } else if (sections.empty() || sections.back().item < start) {
                Section section;
                section.item = start;
                section.titles.push_back(entries[e].title);
                sections.push_back(section);
            }
        }
        if (!sections.empty())
            return;
        for (size_t i = 0; i < items.size(); i++)
            if (items[i].kind == ITEM_HEADING) {
                Section section;
                section.item = i;
                section.titles.push_back(items[i].text);
                // headings one after another are the lines of one title
                size_t j = i;
                while (j + 1 < items.size() && items[j + 1].kind == ITEM_HEADING && j - i < 3) {
                    j++;
                    section.titles.push_back(items[j].text);
                }
                for (size_t k = i; k <= j; k++)
                    items[k].dropped = true;
                sections.push_back(section);
                i = j;
            }
    }

    void dump()
    {
        if (!dumping)
            return;
        static const char * const names[] = { "text", "HEAD", "sub ", "entr", "PICT", "vers", "row " };
        fprintf(stderr, "body %.1f, leading %.1f, block %.0f..%.0f, zones %d\n", body, leading, blockTop, blockBottom,
                stats.zones ? 1 : 0);
        for (size_t i = 0; i < items.size(); i++)
        {
            std::string text = items[i].text.substr(0, 110);
            std::replace(text.begin(), text.end(), '\n', '/');
            std::replace(text.begin(), text.end(), '\t', '|');
            fprintf(stderr, "p%-4d %s h%-5.1f x%-4.0f n%-2d %s%s\n", items[i].page + 1, names[items[i].kind],
                    items[i].height, items[i].x0, items[i].lines, items[i].dropped ? "(title) " : "", text.c_str());
        }
    }

    /// A text into the book: the marks of the formulas inside lines become their pictures, or,
    /// when a picture was not made or pictures are not wanted here (a title), their text.
    void written(FILE * out, const std::string & text, bool pictures) const
    {
        size_t from = 0;
        for (;;) {
            const size_t mark = text.find('\x01', from);
            const size_t end = mark == std::string::npos ? mark : text.find('\x02', mark);
            if (end == std::string::npos) {
                escaped(out, text.substr(from));
                return;
            }
            escaped(out, text.substr(from, mark - from));
            const size_t number = (size_t)atoi(text.c_str() + mark + 1);
            if (number < inlines.size()) {
                if (pictures && inlines[number].picture >= 0)
                    fprintf(out, "<image l:href=\"#pic%d\"/>", inlines[number].picture);
                else
                    escaped(out, inlines[number].text);
            }
            from = end + 1;
        }
    }

    void writeBody(FILE * out)
    {
        size_t next = 0;
        bool open = false;
        int idPage = -1;    // the last page whose id is written
        if (items.empty())
            fputs("<section><p></p></section>\n", out);
        for (size_t i = 0; i < items.size(); i++) {
            const Item & item = items[i];
            char id[32];
            id[0] = 0;
            if (item.page > idPage) {
                snprintf(id, sizeof(id), " id=\"pg%d\"", item.page + 1);
                idPage = item.page;
            }
            const bool starts = next < sections.size() && sections[next].item == i;
            if (starts || !open) {
                if (open)
                    fputs("</section>\n", out);
                // the id of a heading that became the title stands on the section
                fprintf(out, "<section%s>", item.dropped ? id : "");
                if (item.dropped)
                    id[0] = 0;
                if (starts) {
                    fputs("<title>", out);
                    for (size_t t = 0; t < sections[next].titles.size(); t++) {
                        fputs("<p>", out);
                        written(out, sections[next].titles[t], false);
                        fputs("</p>", out);
                    }
                    fputs("</title>\n", out);
                    next++;
                    stats.sections++;
                }
                open = true;
            }
            if (item.dropped) {
                // a title of several lines: its other lines are in the title too
                if (id[0])
                    idPage = item.page - 1;
                continue;
            }
            switch (item.kind) {
            case ITEM_VERSE: {
                fprintf(out, "<poem%s><stanza>", id);
                for (size_t from = 0; from <= item.text.size();) {
                    size_t end = item.text.find('\n', from);
                    if (end == std::string::npos)
                        end = item.text.size();
                    if (end == from) {
                        fputs("</stanza><stanza>", out);
                    } else {
                        fputs("<v>", out);
                        written(out, item.text.substr(from, end - from), true);
                        fputs("</v>", out);
                    }
                    from = end + 1;
                }
                fputs("</stanza></poem>\n", out);
                stats.paragraphs++;
                break;
            }
            case ITEM_ROW: {
                // the rows one after another are one table
                const bool first = i == 0 || items[i - 1].kind != ITEM_ROW || items[i - 1].page != item.page
                                   || (next > 0 && sections[next - 1].item == i);
                const bool last = i + 1 >= items.size() || items[i + 1].kind != ITEM_ROW || items[i + 1].page != item.page
                                  || (next < sections.size() && sections[next].item == i + 1);
                if (first) {
                    fprintf(out, "<table%s>", id);
                    stats.tables++;
                }
                fputs("<tr>", out);
                for (size_t from = 0; from <= item.text.size();) {
                    size_t end = item.text.find('\t', from);
                    if (end == std::string::npos)
                        end = item.text.size();
                    fputs("<td>", out);
                    written(out, item.text.substr(from, end - from), true);
                    fputs("</td>", out);
                    from = end + 1;
                }
                fputs(last ? "</tr></table>\n" : "</tr>\n", out);
                break;
            }
            case ITEM_PICTURE:
                fprintf(out, "<image%s l:href=\"#pic%d\"/>\n", id, item.picture);
                stats.images++;
                break;
            case ITEM_HEADING:
            case ITEM_SUBTITLE:
                fprintf(out, "<subtitle%s>", id);
                written(out, item.text, true);
                fputs("</subtitle>\n", out);
                stats.headings++;
                break;
            default:
                fprintf(out, "<p%s>", id);
                written(out, item.text, true);
                fputs("</p>\n", out);
                if (item.kind == ITEM_ENTRY)
                    stats.entries++;
                else
                    stats.paragraphs++;
            }
        }
        if (open)
            fputs("</section>\n", out);
    }

    const char * language()
    {
        long cyrillic = 0, latin = 0, ukrainian = 0;
        for (size_t i = 0; i < items.size(); i += 1 + items.size() / 400) {
            const std::string & s = items[i].text;
            for (size_t k = 0; k < s.size();) {
                const int c = charAt(s, k);
                if (c >= 0x400 && c < 0x530) {
                    cyrillic++;
                    if (c == 0x456 || c == 0x457 || c == 0x454 || c == 0x491)
                        ukrainian++;
                } else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') {
                    latin++;
                }
            }
        }
        return cyrillic > latin ? (ukrainian * 40 > cyrillic ? "uk" : "ru") : "en";
    }

    bool write(const char * djvuPath, const char * bookPath)
    {
        const std::string temp = std::string(bookPath) + ".tmp";
        FILE * out = fopen(temp.c_str(), "wb");
        if (!out)
            return false;
        static char buffer[1 << 16];
        setvbuf(out, buffer, _IOFBF, sizeof(buffer));
        // the name of the file without its folder and its extension
        std::string title = djvuPath;
        const size_t slash = title.find_last_of("/\\");
        if (slash != std::string::npos)
            title.erase(0, slash + 1);
        const size_t dot = title.rfind('.');
        if (dot != std::string::npos && dot > 0)
            title.erase(dot);
        fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              "<FictionBook xmlns=\"http://www.gribuser.ru/xml/fictionbook/2.0\" xmlns:l=\"http://www.w3.org/1999/xlink\">\n"
              "<description><title-info><genre>nonfiction</genre><author><nickname></nickname></author><book-title>", out);
        escaped(out, title);
        fputs("</book-title>", out);
        if (cover)
            fputs("<coverpage><image l:href=\"#cover\"/></coverpage>", out);
        fprintf(out, "<lang>%s</lang></title-info><document-info><program-used>CoolReader, DjVu as a book, version %d"
                     "</program-used></document-info></description>\n<body>\n", language(), PB_DJVUTEXT_VERSION);
        writeBody(out);
        fputs("</body>\n", out);
        if (binaries) {
            // the pictures as they were written while the pages were read, without the ornaments
            fflush(binaries);
            const long end = ftell(binaries);
            long at = 0;
            for (size_t i = 0; i <= cuts.size(); i++) {
                if (i < cuts.size() && !cuts[i].dropped)
                    continue;
                const long to = i < cuts.size() ? cuts[i].from : end;
                fseek(binaries, at, SEEK_SET);
                char block[1 << 15];
                while (at < to) {
                    const size_t want = (size_t)std::min((long)sizeof(block), to - at);
                    const size_t got = fread(block, 1, want, binaries);
                    if (got == 0)
                        break;
                    fwrite(block, 1, got, out);
                    at += (long)got;
                }
                if (i < cuts.size())
                    at = cuts[i].to;
            }
        }
        fputs("</FictionBook>\n", out);
        stats.bytes = ftell(out);
        const bool ok = ferror(out) == 0;
        if (fclose(out) != 0 || !ok || rename(temp.c_str(), bookPath) != 0) {
            unlink(temp.c_str());
            return false;
        }
        return true;
    }
};

} // namespace

PbDjvuKind pbDjvuKind(const char * path)
{
    Djvu djvu;
    if (!djvu.open(path))
        return PB_DJVU_UNREADABLE;
    const int wanted = djvu.count < 10 ? djvu.count : 10;
    int withText = 0;
    for (int k = 0; k < wanted; k++) {
        // spread over the file; the first pages of a book are often a cover and a title
        const int number = wanted == djvu.count ? k : (int)((long)(2 * k + 1) * djvu.count / (2 * wanted));
        Page page;
        djvu.text(number, page, NULL);
        if (page.chars >= 100)
            withText++;
    }
    return withText * 2 >= wanted ? PB_DJVU_TEXT : PB_DJVU_PICTURES;
}

bool pbDjvuToBook(const char * djvuPath, const char * bookPath, PbDjvuTextProgress progress, void * context,
                  PbDjvuTextStats * stats, std::string * error)
{
    Translator t;
    const char * reason = NULL;
    if (!t.djvu.open(djvuPath))
        reason = "the file is not a DjVu document or is damaged";
    const int count = t.djvu.count;
    // the work is counted in parts of a thousand: the text is a fifth, the pictures the rest
    double t0 = nowMs();
    if (!reason) {
        t.pages.resize(count);
        for (int n = 0; n < count && !reason; n++) {
            if (progress && (n & 7) == 0 && !progress(context, (int)((long)n * 200 / count), 1000))
                reason = "stopped";
            else
                t.djvu.text(n, t.pages[n], &t.inlines);
        }
    }
    const std::string binPath = std::string(bookPath) + ".bin";
    if (!reason) {
        t.stats.pages = count;
        for (int n = 0; n < count; n++)
            if (!t.pages[n].lines.empty())
                t.stats.textPages++;
        t.dropNoise();
        t.findSizes();
        // a first page with a few words is a cover: its picture is shown, its text is not
        if (count > 1 && t.pages[0].chars < 400)
            for (size_t i = 0; i < t.pages[0].lines.size(); i++)
                t.pages[0].lines[i].out = true;
        t.dropRunning();
        t.findBlock();
        for (int n = 0; n < count; n++)
            t.takePage(n);
        t.stats.textMs = nowMs() - t0;

        t0 = nowMs();
        t.binaries = fopen(binPath.c_str(), "w+b");
        std::vector<Place> places;
        for (int n = 0; n < count; n++)
            t.placesOf(n, places);
        t.markFormulas(places);
        // the formulas inside lines: the place of each is known (the words of the layer)
        for (size_t i = 0; i < t.inlines.size(); i++) {
            Place place;
            place.page = t.inlines[i].page;
            place.x0 = t.inlines[i].x0;
            place.x1 = t.inlines[i].x1;
            place.y0 = t.inlines[i].y0;
            place.y1 = t.inlines[i].y1;
            place.inlined = (int)i;
            places.push_back(place);
        }
        std::stable_sort(places.begin(), places.end(), placeBefore);
        std::vector<Item> found;
        size_t at = 0;
        // the first page is decoded for the cover whether it has such a place or not
        for (int n = 0; n < count && !reason; n++) {
            size_t to = at;
            while (to < places.size() && places[to].page == n)
                to++;
            if (to == at && n > 0)
                continue;
            if (progress && !progress(context, 200 + (int)((long)at * 800 / (places.size() + 1)), 1000))
                reason = "stopped";
            else
                t.takePictures(n, to < places.size() ? places[to].page : -1, places, at, to, found);
            at = to;
        }
        t.placePictures(found);
        t.stats.imageMs = nowMs() - t0;
    }
    if (!reason) {
        t0 = nowMs();
        t.classify();
        t.joinPages();
        t.findSections();
        t.dump();
        if (!t.write(djvuPath, bookPath))
            reason = "the book cannot be written";
        t.stats.writeMs = nowMs() - t0;
    }
    if (t.binaries)
        fclose(t.binaries);
    unlink(binPath.c_str());
    if (stats)
        *stats = t.stats;
    if (reason && error)
        *error = reason;
    return reason == NULL;
}
