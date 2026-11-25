/*
 * The search in the text of a page of a PDF or a DjVu shown as it is (pb_pageview.h).
 *
 * The text of a page is its words in the order of reading, each with the rectangle it takes on the
 * page (PbPageDoc::words: a PDF with text, a DjVu with a text layer). What is looked for is found
 * in the words put together, and the answer is the rectangles of the words it lies in: they are
 * shown on the picture of the page. There is no recognition of text: a page of pictures alone has
 * nothing to find.
 *
 * Compared without the case of the letters and without hyphens, so that a word carried over to the
 * next line ("exam-" and "ple") is found as "example". A hyphen typed is left out the same way:
 * "well-known" finds "well-known" and "wellknown".
 *
 * Depends on the strings of the engine alone: the PC bench builds it without the reader.
 */
#ifndef PB_PAGESEARCH_H
#define PB_PAGESEARCH_H

#include <lvstring.h>
#include <vector>
#include "pb_pagedoc.h"

class PbPageSearch
{
    static bool hyphen(lChar32 c) { return c == '-' || c == 0x00AD || c == 0x2010 || c == 0x2011; }
    static bool space(lChar32 c) { return c <= 32 || c == 0x00A0 || (c >= 0x2000 && c <= 0x200B) || c == 0x202F; }
    /// True when the word b does not go on the line of the word a: it starts to the left of a, or
    /// lies above or under the middle of a.
    static bool newLine(const PbPageBox & a, const PbPageBox & b)
    {
        const float middle = (a.y0 + a.y1) * 0.5f;
        return b.x0 < a.x0 || b.y0 > middle || b.y1 < middle;
    }
    /// The rectangle of the letters from..to of a zone of the text. A zone of one word is taken
    /// whole. A line given as one zone (a DjVu whose text layer is not cut into words) gives the
    /// part its words take, by the count of the letters: the layer has no place of a letter.
    static PbPageBox partOf(const PbPageBox & box, const lString32 & text, int from, int to)
    {
        const int len = text.length();
        bool spaced = false;
        for (int i = 0; i < len && !spaced; i++)
            spaced = space(text[i]);
        if (!spaced || len < 1)
            return box;
        while (from > 0 && !space(text[from - 1]))
            from--;
        while (to + 1 < len && !space(text[to + 1]))
            to++;
        // half a letter more to each side: the letters are not of one width
        const float w = box.x1 - box.x0;
        float x0 = box.x0 + w * (from - 0.5f) / len, x1 = box.x0 + w * (to + 1.5f) / len;
        if (x0 < box.x0)
            x0 = box.x0;
        if (x1 > box.x1)
            x1 = box.x1;
        return PbPageBox(x0, box.y0, x1, box.y1);
    }

public:
    /// A text as it is compared: lower case, no hyphens, one space between its words, none around.
    static lString32 fold(const lString32 & text)
    {
        lString32 t = text;
        t.lowercase();
        lString32 out;
        bool gap = false;
        for (int i = 0; i < t.length(); i++) {
            const lChar32 c = t[i];
            if (hyphen(c))
                continue;
            if (space(c)) {
                gap = true;
                continue;
            }
            if (gap && !out.empty())
                out << (lChar32)' ';
            gap = false;
            out << c;
        }
        return out;
    }

    /// Looks for the pattern (folded by fold) in the words of a page. boxes: the rectangles of the
    /// words every place found lies in (page units), in the order of the text. The number of
    /// places found.
    static int find(const std::vector<PbPageWord> & words, const lString32 & pattern, std::vector<PbPageBox> & boxes)
    {
        boxes.clear();
        if (pattern.empty() || words.empty())
            return 0;
        // The text of the page folded, and for each of its letters the word it is of and its place
        // in the text of that word (-1: the space between two words).
        lString32 page;
        std::vector<int> wordOf, charOf;
        std::vector<lString32> texts(words.size());
        bool gap = false;
        for (size_t i = 0; i < words.size(); i++) {
            lString32 & t = texts[i];
            t = Utf8ToUnicode(words[i].text.c_str());
            t.lowercase();
            for (int k = 0; k < t.length(); k++) {
                const lChar32 c = t[k];
                if (hyphen(c))
                    continue;
                if (space(c)) {
                    gap = true;
                    continue;
                }
                if (gap && !page.empty()) {
                    page << (lChar32)' ';
                    wordOf.push_back(-1);
                    charOf.push_back(0);
                }
                gap = false;
                page << c;
                wordOf.push_back((int)i);
                charOf.push_back(k);
            }
            // The word is over: a space follows, unless the word is carried over to the next line
            // (it ends with a hyphen and the next word starts another line). A dash that stands
            // alone carries nothing over.
            const bool carried = t.length() > 1 && hyphen(t[t.length() - 1]) && i + 1 < words.size()
                                 && newLine(words[i].box, words[i + 1].box);
            if (!carried)
                gap = true;
        }
        const int n = page.length(), m = pattern.length();
        int found = 0;
        for (int at = 0; at + m <= n;) {
            int same = 0;
            while (same < m && page[at + same] == pattern[same])
                same++;
            if (same < m) {
                at++;
                continue;
            }
            found++;
            for (int k = at; k < at + m;) {
                const int w = wordOf[k];
                if (w < 0) {
                    k++;
                    continue;
                }
                int end = k;
                while (end + 1 < at + m && wordOf[end + 1] == w)
                    end++;
                // (a word that has the text twice, "ma" in "mama", is one rectangle)
                const PbPageBox box = partOf(words[w].box, texts[w], charOf[k], charOf[end]);
                if (boxes.empty() || boxes.back().x0 != box.x0 || boxes.back().y0 != box.y0 || boxes.back().x1 != box.x1
                    || boxes.back().y1 != box.y1)
                    boxes.push_back(box);
                k = end + 1;
            }
            at += m;
        }
        return found;
    }
};

/// Where the search stands on the pages of a file. A stop is a number on a page that grows down
/// the page: the frame that shows a place found, or in the ribbon the row of the strip of the
/// page a place lies at. The window of the pages gives the stops; the PC bench gives made ones.
struct PbPageStops {
    virtual ~PbPageStops() { }
    /// The stops of a page, in their order, each once.
    virtual void stopsOf(int page, std::vector<int> & stops) = 0;
};

/// The walk of the search through the stops of a file of `count` pages. A place is a page and a
/// number on it; the screen shown starts at one place and ends right before another.
class PbPageSearchWalk
{
public:
    /// True when the place (page, pos) stands before the other one.
    static bool before(int page, int pos, int otherPage, int otherPos)
    {
        return page < otherPage || (page == otherPage && pos < otherPos);
    }

    /// The first stop at (page, pos) or after it (direction 1), or the last one before it
    /// (direction -1), on at most `pages` pages from that page on.
    static bool scan(PbPageStops & source, int count, int direction, int page, int pos, int pages, int & foundPage,
                     int & foundPos)
    {
        std::vector<int> stops;
        for (int n = 0; n < pages && page >= 0 && page < count; n++, page += direction) {
            source.stopsOf(page, stops);
            if (direction > 0) {
                for (size_t i = 0; i < stops.size(); i++)
                    if (n > 0 || stops[i] >= pos) {
                        foundPage = page;
                        foundPos = stops[i];
                        return true;
                    }
            } else {
                for (size_t i = stops.size(); i-- > 0;)
                    if (n > 0 || stops[i] < pos) {
                        foundPage = page;
                        foundPos = stops[i];
                        return true;
                    }
            }
        }
        return false;
    }

    /// A search begins on the screen from (page, pos) up to (endPage, endPos): the first stop
    /// from the beginning of that screen on, then round the end of the file up to it. here: the
    /// stop is on the screen shown. False when the file has no stop.
    static bool first(PbPageStops & source, int count, int page, int pos, int endPage, int endPos, int & foundPage,
                      int & foundPos, bool & here)
    {
        here = false;
        if (scan(source, count, 1, page, pos, count - page, foundPage, foundPos)) {
            here = before(foundPage, foundPos, endPage, endPos);
            return true;
        }
        return scan(source, count, 1, 0, 0, page + 1, foundPage, foundPos);
    }

    /// The stop of the next screen (direction 1) or of the previous one (-1) from the screen
    /// (page, pos) .. (endPage, endPos), round the end of the file. False when the stops of the
    /// screen shown are the only ones.
    static bool move(PbPageStops & source, int count, int direction, int page, int pos, int endPage, int endPos,
                     int & foundPage, int & foundPos)
    {
        if (direction > 0) {
            if (endPage < count && scan(source, count, 1, endPage, endPos, count - endPage, foundPage, foundPos))
                return true;
            // from the beginning of the file up to the screen shown, which is not a new one
            return scan(source, count, 1, 0, 0, page + 1, foundPage, foundPos) && before(foundPage, foundPos, page, pos);
        }
        if (scan(source, count, -1, page, pos, page + 1, foundPage, foundPos))
            return true;
        // from the end of the file down to the screen shown
        return scan(source, count, -1, count - 1, 0x7FFFFFFF, count - page, foundPage, foundPos)
               && !before(foundPage, foundPos, endPage, endPos);
    }
};

#endif // PB_PAGESEARCH_H
