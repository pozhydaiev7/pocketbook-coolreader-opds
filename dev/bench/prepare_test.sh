#!/bin/bash
# The image of the next page prepared ahead (LVDocView::preparePageImage), inside WSL:
#   bash prepare_test.sh <tree name> [books...]
# Every book is read by runs of consecutive pages, forward and back, in several settings of the
# page header and the margins. The images must be the same as those of pages drawn when shown;
# the time of a page turn is printed for both ways.
set -u
T=${1:-prep}; shift || true
BENCH=$HOME/PBDEV/bench
B=$BENCH/build-ng-$T; D=$BENCH/src-ng-$T
BOOKS=${*:-war-and-peace.fb2.zip crime.epub se-war-and-peace.epub moby-dick.epub pg10-bible.epub}
export BENCH_TIMEOUT=300
CLOCK="window.status.clock=1;window.status.pos.percent=1;window.status.battery=1"
USER="crengine.interline.space=85;crengine.style.floating.punctuation.enabled=1;font.hinting.mode=0;font.kerning.enabled=0;crengine.style.tracking=-10;font.face.base.weight=500"
fail=0
run() { # label size kerning props
    local label=$1 size=$2 kern=$3 props=$4
    echo "=== $label"
    for book in $BOOKS; do
        local css=$D/data/common/system/share/cr3/fb2.css
        case "$book" in *.epub) [ -f "$D/data/common/system/share/cr3/epub.css" ] && css=$D/data/common/system/share/cr3/epub.css ;; esac
        if [ -n "$props" ]; then export BENCH_PROPS="$props"; else unset BENCH_PROPS; fi
        # BPP: bits per pixel of the page image (the program takes the depth of the screen: 2, 3, 4, 8);
        # BENCH_W, BENCH_H: the size of the screen
        BENCH_PREPARE=1 "$B/cr3bench" "$BENCH/fonts" "$D/cr3gui/data/hyph" "$css" "$BENCH/books/$book" "${BPP:-8}" "$size" "$kern" 40 \
            2>> "$BENCH/prepare-time.txt" | grep '^prepare' | cut -c1-220 >> "$BENCH/prepare-out.txt"
        rc=${PIPESTATUS[0]}
        [ "$rc" -ne 0 ] && { echo "exit $rc $book"; fail=1; }
    done
}
: > "$BENCH/prepare-time.txt"; : > "$BENCH/prepare-out.txt"
# the bench is built as the program is: window.status.line is 0 top, 1 bottom, 2 off
TOP="window.status.line=0"; BOTTOM="window.status.line=1"; OFF="window.status.line=2"
M0="crengine.page.margin.top=0;crengine.page.margin.bottom=0;crengine.page.margin.left=0;crengine.page.margin.right=0"
run "settings of the bench: header at the top, no clock"   44 1 ""
run "header at the top, clock, percent, battery"           44 1 "$TOP;$CLOCK"
run "header at the bottom, clock, percent, battery"        44 1 "$BOTTOM;$CLOCK"
run "no header"                                            44 1 "$OFF"
run "header at the top, clock, all margins 0"              44 1 "$TOP;$CLOCK;$M0"
run "header at the top, clock, top margin 4, the others 0" 44 1 "$TOP;$CLOCK;$M0;crengine.page.margin.top=4"
run "header at the bottom, clock, all margins 0"           44 1 "$BOTTOM;$CLOCK;$M0"
run "header at the bottom, clock, margins 2, interline 70" 44 1 "$BOTTOM;$CLOCK;crengine.page.margin.top=2;crengine.page.margin.bottom=2;crengine.interline.space=70"
run "settings of the user (size 48), header at the top, clock" 48 0 "$TOP;$CLOCK;$USER"
# a short report: the lines of the differences, the totals, the time of a page turn
grep -c 'differs' "$BENCH/prepare-out.txt" | sed 's/^/lines about a difference: /'
grep 'differ' "$BENCH/prepare-out.txt" | grep -v ' 0 images differ' | head -${SHOW:-12}
awk '/^prepare .*page turns/ { for (i = 1; i <= NF; i++) { if ($i == "page" && $(i+1) == "turns,") t += $(i-1); if ($i == "images" && $(i+1) == "ready") r += $(i-1); if ($i == "refused,") f += $(i-1); if ($i == "images" && $(i+1) == "differ") d += $(i-1) } }
     END { printf "total: %d page turns, %d images ready at the turn, %d refused, %d images differ\n", t, r, f, d }' "$BENCH/prepare-out.txt"
awk '{ for (i = 1; i <= NF; i++) if ($i == "turn") { a += $(i+1); b += $(i+6); n++ } }
     END { if (n) printf "page turn, mean of %d runs: %.2f ms drawn when shown, %.2f ms prepared ahead\n", n, a / n, b / n }' "$BENCH/prepare-time.txt"
[ $fail -eq 0 ] && echo "ALL THE SAME" || echo "FAILED"
