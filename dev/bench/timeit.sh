#!/bin/bash
# Precise timing: timeit.sh <load|relayout|draw> <repeat> <tree> [<tree> ...]
# Runs the bench of every tree in the profiling mode (no gprof) 3 times, prints the best time.
# The style sheet is the one of the format of the book, as the program takes it (the runs before
# 2026-11-17 night named a file that is not in the trees and went without any style sheet).
#   BOOK=<file>   book of ~/PBDEV/bench/books (big.fb2)
#   CONF=user|std settings of the reader of the user (default) or the defaults of the engine
BENCH=$HOME/PBDEV/bench
MODE=$1; REPEAT=$2; shift 2
BOOK=${BOOK:-big.fb2}
CONF=${CONF:-user}
if [ "$CONF" = user ]; then
    SIZE=48; KERN=0
    P="crengine.interline.space=85;crengine.style.floating.punctuation.enabled=1;font.hinting.mode=0"
    export BENCH_PROPS="$P;font.kerning.enabled=0;crengine.style.tracking=-10;font.face.base.weight=500"
else
    SIZE=44; KERN=1
    unset BENCH_PROPS
fi
for T in "$@"; do
    D="$BENCH/src-ng-$T/data/common/system/share/cr3"
    CSS="$D/fb2.css"
    case "$BOOK" in
    *.epub) [ -f "$D/epub.css" ] && CSS="$D/epub.css" ;;
    *.htm|*.html) [ -f "$D/htm.css" ] && CSS="$D/htm.css" ;;
    esac
    [ -f "$CSS" ] || { echo "$T: no style sheet $CSS"; continue; }
    best=999999
    for run in 1 2 3; do
        a=$(date +%s%N)
        BENCH_PROF=$MODE BENCH_REPEAT=$REPEAT "$BENCH/build-ng-$T/cr3bench" "$BENCH/fonts" "$BENCH/src-ng-$T/cr3gui/data/hyph" \
            "$CSS" "$BENCH/books/$BOOK" ${BPP:-8} $SIZE $KERN 60 > /dev/null 2>&1
        b=$(date +%s%N)
        ms=$(( (b - a) / 1000000 ))
        [ $ms -lt $best ] && best=$ms
    done
    echo "$T: $MODE x$REPEAT $BOOK $CONF: best of 3 = $best ms"
done
