#!/bin/bash
# Which font faces the elements of a book are drawn with: the fonts of the book on, then off
# (crengine.doc.embedded.fonts.enabled=0). With them off every element should have the face of the
# settings (Georgia here) or a monospace face.
# exp_elemfaces.sh <tree> [books...]
BENCH=$HOME/PBDEV/bench
T=$1; shift
BOOKS=${*:-"accessible_epub_3.epub u1.epub lab.epub war-and-peace.epub se-war-and-peace.epub moby-dick.epub pg1342-pride.epub crime.epub childrens-literature.epub wasteland.epub pg10-bible.epub"}
B="$BENCH/build-ng-$T/cr3bench"
D="$BENCH/src-ng-$T"
BASE="crengine.interline.space=85;font.hinting.mode=0;font.kerning.enabled=0;font.face.base.weight=500"
for book in $BOOKS; do
    for props in "$BASE" "$BASE;crengine.doc.embedded.fonts.enabled=0"; do
        case "$props" in *fonts.enabled=0) echo -n "OFF " ;; *) echo -n "on  " ;; esac
        BENCH_ELEMFACES=1 BENCH_FACE=Georgia BENCH_PROPS="$props" "$B" "$BENCH/fonts" "$D/cr3gui/data/hyph" \
            "$D/data/common/system/share/cr3/epub.css" "$BENCH/books/$book" 8 48 0 1 2>&1 >/dev/null | grep " faces:" | cut -c1-900
    done
done
