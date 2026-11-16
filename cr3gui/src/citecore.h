#ifndef CITECORE_H
#define CITECORE_H 1

#include "lvstyles.h"
#include "lvtinydom.h"
#include "lvdocview.h"
#include "crtrace.h"


enum granularity {
    grn_char,
    grn_word,
    grn_block
};

//const lString32 phrase_bounds(U".?!");


enum last_move_type {
    not_moved,
    moved_up,
    moved_down
};

void
point_to_end(ldomXPointerEx& xp) {
    ldomNode * p = xp.getNode();
    if(p->isText()) {
        lString32 text = p->getText();
        xp.setOffset(text.length());
    };
    if(p->isElement()) {
        xp.lastChild();
        if (!xp.isText())
            xp.nextVisibleText();
        if (xp.isText()) {
            ldomNode * p = xp.getNode();
            lString32 text = p->getText();
            xp.setOffset(text.length());
        }
    }
}

void
point_to_begin(ldomXPointerEx& xp) {
    ldomNode * p = xp.getNode();
    if(p->isText())
        xp.setOffset(0);
}

class CiteSelection {
    last_move_type last_move_;
    LVDocView& view_;
    ldomXPointerEx start_;
    ldomXPointerEx end_;
public:
    CiteSelection(LVDocView& view) : 
        last_move_(not_moved),
        view_(view) {
            ldomXPointerEx middle = view_.getCurrentPageMiddleParagraph();
            middle.prevVisibleText();
            start_ = middle;
            point_to_begin(start_);
            end_ = middle;
            point_to_end(end_);
            highlight();
        };

    ldomXRange
    select() const {
        return ldomXRange(start_, end_);
    }

    void getRange( ldomXRange & range )
    {
            range.setStart( start_ );
            range.setEnd( end_ );
    }

    /// Selects a given range and opens the page of its start (editing of a saved citation).
    void setRange( const ldomXPointerEx & start, const ldomXPointerEx & end ) {
        start_ = start;
        end_ = end;
        last_move_ = not_moved;
        view_.goToBookmark( start_ );
        highlight();
    }

    /// The word of the open page at a point of the page image (selection by touch).
    /// A word is the text between two spaces, with its punctuation. False: no text at the point.
    bool wordAt( lvPoint pt, ldomXPointerEx & wordStart, ldomXPointerEx & wordEnd ) {
        ldomXPointer p = view_.getNodeByPoint( pt );
        if ( p.isNull() )
            return false;
        ldomXRange word;
        if ( !ldomXRange::getWordRange( word, p ) )
            return false;
        wordStart = word.getStart();
        wordEnd = word.getEnd();
        return true;
    }

    /// Selects the text from one word to another, whichever of them is the first in the book.
    void selectWords( const ldomXPointerEx & start1, const ldomXPointerEx & end1,
                      const ldomXPointerEx & start2, const ldomXPointerEx & end2 ) {
        start_ = start1.compare( start2 ) <= 0 ? start1 : start2;
        end_ = end1.compare( end2 ) >= 0 ? end1 : end2;
        highlight();
    }

    /// Moves a bound of the selection to a word: a word before the selection becomes its first
    /// word; a word inside or after the selection becomes its last word.
    void moveBoundTo( const ldomXPointerEx & wordStart, const ldomXPointerEx & wordEnd ) {
        if ( wordStart.compare( start_ ) < 0 )
            start_ = wordStart;
        else
            end_ = wordEnd;
        highlight();
    }

    void
    highlight() {
        // show what we try to highlight
        crtrace trace("highlight: \n");
        LVRef<ldomXRange> range = view_.getPageDocumentRange(-1);
        trace << "\tscreen: \n\t\t [ " << range->getStart().toString() <<
            " :\n\t\t" << range->getEnd().toString() << " ] / \n\tselection: [ " <<
            start_.toString() << " :\n\t\t" << 
            end_.toString() << " ]";
        view_.clearSelection();
        ldomXRange selected(start_, end_, 1);
        view_.selectRange(selected); 
    }

    void
    move_to_upper_bound() {
        LVRef<ldomXRange> range = view_.getPageDocumentRange(-1);
        if(!range->isInside(start_)) {
                CRLog::info("move_to_upper_bound(): Scroll Up\n");
                view_.goToBookmark(start_);
        }
        highlight();
    }

    void
    move_to_lower_bound() {
        LVRef<ldomXRange> range = view_.getPageDocumentRange(-1);
        if(!range->isInside(end_)) {
                CRLog::info("move_to_lower_bound(): Scroll Down\n");
                view_.goToBookmark(end_);
        };
        highlight();
    }

    void growUp() {
        CRLog::info("grow up\n");
        ldomXPointerEx ptr(start_);
        if(prevPara(ptr)){
            start_ = ptr;
            point_to_begin(start_);
            move_to_upper_bound();
        };
    }

    /// The first paragraph leaves the selection. Refused if nothing would be left.
    /// Before, a refused step still moved the start to the beginning of its text node.
    void shrinkDown() {
        CRLog::info("shrink down\n");
        ldomXPointerEx ptr(start_);
        if (!nextPara(ptr))
            return;
        point_to_begin(ptr);
        if (ptr.compare(end_) >= 0)
            return;
        start_ = ptr;
        move_to_upper_bound();
    }

    /// The last paragraph leaves the selection. Refused if nothing would be left.
    /// Before, the step compared the start with the element of the previous paragraph: a start
    /// inside that paragraph (the selection the window opens with, or one made by touch) is
    /// after its element, so the step was refused and only moved the end to the end of its
    /// text node.
    void shrinkUp() {
        CRLog::info("shrink up\n");
        ldomXPointerEx ptr(end_);
        if (!prevPara(ptr))
            return;
        point_to_end(ptr);
        if (start_.compare(ptr) >= 0)
            return;
        end_ = ptr;
        move_to_lower_bound();
    }

    /// Makes the selection whole paragraphs: from the beginning of the paragraph of its first
    /// word to the end of the paragraph of its last word. False if it was that already.
    bool selectWholeParagraphs() {
        ldomXPointerEx s(start_);
        ldomXPointerEx e(end_);
        if (!s.isVisibleFinal() && !s.ensureFinal())
            return false;
        if (e.isVisibleFinal() || !e.ensureFinal())
            return false;
        point_to_begin(s);
        point_to_end(e);
        if (s.compare(e) >= 0)
            return false;
        if (ldomXRange(s, e).getRangeText() == ldomXRange(start_, end_).getRangeText())
            return false;
        start_ = s;
        end_ = e;
        move_to_upper_bound();
        return true;
    }

    /// True if the text between two positions (a before b) has something to read: a letter or
    /// a digit.
    static bool hasWords(const ldomXPointerEx & a, const ldomXPointerEx & b) {
        lString32 text = ldomXRange(a, b).getRangeText();
        for (int i = 0; i < text.length(); i++)
            if (lGetCharProps(text[i]) & (CH_PROP_ALPHA | CH_PROP_DIGIT))
                return true;
        return false;
    }

    /// A step by a sentence that the reader sees. The step of the engine functions alone
    /// (rawStep) has two faults:
    ///   - it can move a bound over spaces and punctuation only. A text node of an EPUB often
    ///     ends with a space or a line break, the position after it is a start of an (empty)
    ///     sentence for the engine, and "remove the last sentence" of a whole paragraph removed
    ///     that one character. Such a step is repeated, up to three times, until the bound has
    ///     passed a word;
    ///   - a shrink step of a short selection (one word chosen by touch) moved the start behind
    ///     the end: nothing was highlighted any more and such a citation could be saved. A step
    ///     that leaves no text is undone.
    void phraseStep( void (CiteSelection::*rawStep)() ) {
        ldomXPointerEx start0(start_);
        ldomXPointerEx end0(end_);
        for (int i = 0; i < 3; i++) {
            ldomXPointerEx s(start_);
            ldomXPointerEx e(end_);
            (this->*rawStep)();
            if (start_.compare(end_) >= 0) {
                start_ = s;
                end_ = e;
                move_to_upper_bound();
                return;
            }
            if (start_.compare(s) == 0 && end_.compare(e) == 0)
                return;
            int ds = start_.compare(start0);
            int de = end_.compare(end0);
            if (ds < 0 && hasWords(start_, start0))
                return;
            if (ds > 0 && hasWords(start0, start_))
                return;
            if (de < 0 && hasWords(end_, end0))
                return;
            if (de > 0 && hasWords(end0, end_))
                return;
        }
    }

    void growUpPhrase() { phraseStep(&CiteSelection::rawGrowUpPhrase); }
    void shrinkDownPhrase() { phraseStep(&CiteSelection::rawShrinkDownPhrase); }
    void shrinkUpPhrase() { phraseStep(&CiteSelection::rawShrinkUpPhrase); }
    void growDownPhrase() { phraseStep(&CiteSelection::rawGrowDownPhrase); }

    void growDown() {
        CRLog::info("grow down\n");
        ldomXPointerEx ptr(end_);
        if (nextPara(ptr)) {
            end_ = ptr;
            point_to_end(end_);
            move_to_lower_bound();
        }
    }

    void rawGrowUpPhrase() {
        ldomXPointerEx xp = start_;
        if (!xp.isFirstVisibleTextInBlock()) {
            ldomXPointerEx pptr(start_);
            bool pp = prevPara(pptr);
            if (xp.prevSentenceStart()) {
                if (pp && xp.compare(pptr) < 0)
                    start_ = pptr;
                else
                    start_ = xp;
                move_to_upper_bound();
            }
        } else if (xp.prevSentenceStart()) {
            start_ = xp;
            move_to_upper_bound();
        }
    }

    void rawShrinkDownPhrase() {
        ldomXPointerEx pptr(start_);
        bool np = nextPara(pptr);
        ldomXPointerEx xp = start_;
        if (!xp.nextSentenceStart())
            return;
        if (np && xp.compare(pptr) > 0)
            start_ = pptr;
        else
            start_ = xp;
        if (end_.compare(xp) >= 0) {
            start_ = xp;
        };
        move_to_upper_bound();
    }


    void rawShrinkUpPhrase() {
        ldomXPointerEx xp(end_);
        if (!xp.prevSentenceEnd())
            return;
        if (start_.compare(xp) <= 0) {
            end_ = xp;
        };
        move_to_lower_bound();
    }

    void rawGrowDownPhrase() {
        ldomXPointerEx xp(end_);
        if (!xp.isLastVisibleTextInBlock()) {
            ldomXPointerEx pptr(xp);
            bool np = nextPara(pptr);
            if (xp.nextSentenceEnd()) {
                if (np && xp.compare(pptr) > 0)
                    end_ = pptr;
                else
                    end_ = xp;
                move_to_upper_bound();
            }
        } else if (xp.nextSentenceEnd()) {
            end_ = xp;
            move_to_lower_bound();
        }
    }


    bool prevPara(ldomXPointerEx &ptr) {
        if (ptr.isVisibleFinal())
            return ptr.prevVisibleFinal();
        return (ptr.ensureFinal() && ptr.prevVisibleFinal());
    }

    bool nextPara(ldomXPointerEx &ptr) {
        if (ptr.isVisibleFinal())
            return ptr.nextVisibleFinal();
        return (ptr.ensureFinal() && ptr.nextVisibleFinal());
    }

    void moveUp() {
        ldomXPointerEx ptr(start_);
        if (prevPara(ptr)) {
            start_ = ptr;
            end_ = start_;
            point_to_begin(start_);
            point_to_end(end_);
            move_to_upper_bound();
        };
    }

    void moveDown() {
        ldomXPointerEx ptr(start_);
        if (nextPara(ptr)) {
            start_ = ptr;
            end_ = start_;
            point_to_begin(start_);
            point_to_end(end_);
            move_to_lower_bound();
        }
    }

};


#endif
