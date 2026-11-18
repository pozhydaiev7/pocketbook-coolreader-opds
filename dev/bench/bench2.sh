#!/bin/bash
# Test bench of both engines on a PC (inside WSL), second edition: real books, the settings of the
# user, the layout after a font size change, the cache file.
#   bench2.sh books                    copy the test books to WSL
#   bench2.sh sync ng <name>           tree <name> of the new engine: copy of ~/PBDEV/sources/cr3-ng
#   bench2.sh sync old <name>          tree <name> of the old engine: copy of ~/PBDEV/sources/cr3-fork-66
#   bench2.sh build ng|old <name>      build the bench of that tree (4 jobs, nice)
#   bench2.sh run ng|old <name> std|user [books...]   pages and timing: out2-..., time2-...
#   bench2.sh cache ng|old <name> std|user [books...] cache file: save and open
#   bench2.sh prof ng <name> std|user parse|load|relayout|draw <book> [repeat]   gprof
#   bench2.sh compare <kind> <a> <b> std|user          compare the pages of two trees
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BENCH=$HOME/PBDEV/bench
export CMAKE_POLICY_VERSION_MINIMUM=3.5
# no book may hang the whole run: seconds for one run of the bench program
export BENCH_TIMEOUT=${TIMEOUT:-120}
ALLBOOKS="war-and-peace.fb2.zip war-and-peace.epub crime.fb2.zip crime.epub se-war-and-peace.epub
accessible_epub_3.epub moby-dick.epub childrens-literature.epub wasteland.epub pg10-bible.epub pg1342-pride.epub big.fb2
lab.fb2 lab.epub u1.epub u2.fb2 u3.fb2 u4.fb2 u5.fb2 u6.fb2 u7.fb2 u8.fb2 u9.fb2"
# lab.*: the laboratory books of make_lab_books.py (every element the engine lays out);
# u*: the books of the reader of the user (books/user, copied from the device 2026-11-17)

tree() { # kind name -> source tree
    if [ "$1" = ng ]; then echo "$BENCH/src-ng-$2"; else echo "$BENCH/src-old-$2"; fi
}
build() { # kind name -> build folder
    if [ "$1" = ng ]; then echo "$BENCH/build-ng-$2"; else echo "$BENCH/build-old-$2"; fi
}
cssdir() {
    echo "$(tree "$1" "$2")/data/common/system/share/cr3"
}
# settings of a configuration: sets BPP SIZE KERN and BENCH_PROPS
config() { # kind std|user
    BPP=8
    if [ "$2" = user ]; then
        SIZE=48; KERN=0
        P="crengine.interline.space=85;crengine.style.floating.punctuation.enabled=1;font.hinting.mode=0"
        P="$P;font.kerning.enabled=0;crengine.style.tracking=-10"
        if [ "$1" = ng ]; then P="$P;font.face.base.weight=500"; else P="$P;font.face.weight.embolden=6"; fi
        export BENCH_PROPS="$P"
    else
        SIZE=44; KERN=1
        unset BENCH_PROPS
    fi
}
cssof() { # kind name book -> css file of the format, as V3DocViewWin::OnLoadFileFormatDetected takes it
    local d; d=$(cssdir "$1" "$2")
    case "$3" in
    *.epub) [ -f "$d/epub.css" ] && { echo "$d/epub.css"; return; } ;;
    *.htm|*.html) [ -f "$d/htm.css" ] && { echo "$d/htm.css"; return; } ;;
    esac
    echo "$d/fb2.css"
}

K=${2:-}; T=${3:-}
case "${1:-}" in
books)
    mkdir -p "$BENCH/books"
    mkdir -p "$HERE/books/lab"
    python3 "$HERE/make_lab_books.py" "$HERE/books/lab"
    cp "$HERE"/books/*/*.epub "$HERE"/books/*/*.fb2.zip "$HERE"/books/*/*.fb2 "$BENCH/books/"
    ls -la "$BENCH/books"
    ;;
sync)
    D=$(tree "$K" "$T")
    if [ "$K" = ng ]; then
        # the build tree of a line of versions (dev/build/build_package.sh); BENCH_SRC names another
        SRC=${BENCH_SRC:-$HOME/PBDEV/sources/cr3-line-master}
        mkdir -p "$D/bench" "$D/cr3gui/data" "$D/data/common/system/share"
        for d in crengine thirdparty tools tinydict; do rsync -a --delete "$SRC/$d/" "$D/$d/"; done
        # SRCNG=clone: the engine of the working copy on the Windows side, with the changes not
        # built for the device yet (the libraries have CRLF there and stay those of the WSL tree)
        if [ "${SRCNG:-}" = clone ]; then
            rsync -a --delete "$(cd "$HERE/../.." && pwd)/crengine/" "$D/crengine/"
            grep -rlI $'\r' "$D/crengine" | while read -r f; do sed -i 's/\r$//' "$f"; done
        fi
        rsync -a "$SRC/cr3gui/data/hyph" "$D/cr3gui/data/"
        rsync -a --delete "$SRC/data/common/system/share/cr3/" "$D/data/common/system/share/cr3/"
        cp "$HERE/CMakeLists.txt" "$D/CMakeLists.txt"
        cp "$HERE/bench2.cpp" "$D/bench/bench.cpp"
    else
        SRC=$HOME/PBDEV/sources/cr3-fork-66
        mkdir -p "$D/cr3gui/data" "$D/data/common/system/share" "$D/harness"
        for d in crengine thirdparty tinydict; do rsync -a --delete "$SRC/$d/" "$D/$d/"; done
        # SRCOLD=wt: the engine of the master worktree on the Windows side, with the changes not
        # built for the device yet
        if [ "${SRCOLD:-}" = wt ]; then
            rsync -a --delete /mnt/c/Temp/cr3-master-wt/crengine/ "$D/crengine/"
            grep -rlI $'\r' "$D/crengine" | while read -r f; do sed -i 's/\r$//' "$f"; done
        fi
        rsync -a "$SRC/cr3gui/data/hyph" "$D/cr3gui/data/"
        rsync -a --delete "$SRC/data/common/system/share/cr3/" "$D/data/common/system/share/cr3/"
        cp -r "$HERE/../bench/CMakeLists.txt" "$HERE/../bench/stub" "$D/harness/"
        cp "$HERE/bench2.cpp" "$D/harness/bench.cpp"
    fi
    echo "tree $K-$T synced from $SRC"
    ;;
build)
    D=$(tree "$K" "$T"); B=$(build "$K" "$T")
    if [ "$K" = ng ]; then
        cmake -S "$D" -B "$B" > "$BENCH/cmake2-$K-$T.log" 2>&1
    else
        cmake -S "$D/harness" -B "$B" -DSRC="$D" -DDOC_BUFFER_SIZE=0xA00000 -DCMAKE_CXX_FLAGS=-DBENCH_ENGINE_OLD \
            > "$BENCH/cmake2-$K-$T.log" 2>&1
    fi
    echo "cmake exit: $?"
    nice -n 15 make -C "$B" -j4 > "$BENCH/make2-$K-$T.log" 2>&1
    echo "make exit: $?"
    grep -n -E "error|Error [0-9]" "$BENCH/cmake2-$K-$T.log" "$BENCH/make2-$K-$T.log" | cut -c1-240 | head -30
    ls -la "$B/cr3bench" 2>/dev/null
    ;;
run|cache)
    C=${4:-std}
    if [ $# -ge 4 ]; then shift 4; else shift $#; fi
    LIST=${*:-$ALLBOOKS}
    config "$K" "$C"
    B=$(build "$K" "$T"); D=$(tree "$K" "$T")
    OUT="$BENCH/out2-$K-$T-$C.txt"; TIME="$BENCH/time2-$K-$T-$C.txt"
    ;;&
run)
    : > "$OUT"; : > "$TIME"
    for book in $LIST; do
        BENCH_RELAYOUT=1 "$B/cr3bench" "$BENCH/fonts" "$D/cr3gui/data/hyph" "$(cssof "$K" "$T" "$book")" \
            "$BENCH/books/$book" $BPP $SIZE $KERN ${PAGES:-40} >> "$OUT" 2>> "$TIME"
        rc=$?
        [ $rc -ne 0 ] && echo "exit $rc $book" >> "$TIME"
    done
    cat "$TIME"
    ;;
cache)
    TIME="$BENCH/time2-$K-$T-$C-cache.txt"
    : > "$TIME"
    for book in $LIST; do
        rm -rf "$BENCH/cache-tmp"; mkdir -p "$BENCH/cache-tmp"
        BENCH_CACHE="$BENCH/cache-tmp" "$B/cr3bench" "$BENCH/fonts" "$D/cr3gui/data/hyph" \
            "$(cssof "$K" "$T" "$book")" "$BENCH/books/$book" $BPP $SIZE $KERN 1 > /dev/null 2>> "$TIME"
        rc=$?
        [ $rc -ne 0 ] && echo "exit $rc $book" >> "$TIME"
        echo "    cache file: $(du -sk "$BENCH/cache-tmp" | cut -f1) KB" >> "$TIME"
    done
    rm -rf "$BENCH/cache-tmp"
    cat "$TIME"
    ;;
prof)
    C=${4:-std}; MODE=${5:-load}; BOOK=${6:-big.fb2}; REPEAT=${7:-5}
    config "$K" "$C"
    D=$(tree "$K" "$T"); B="$(build "$K" "$T")-prof"
    cmake -S "$D" -B "$B" -DBENCH_PROF=1 > "$BENCH/cmake2-$K-$T-prof.log" 2>&1
    nice -n 15 make -C "$B" -j4 > "$BENCH/make2-$K-$T-prof.log" 2>&1
    echo "make exit: $?"
    mkdir -p "$BENCH/prof2" && cd "$BENCH/prof2" || exit 1
    rm -f gmon.out
    BENCH_PROF=$MODE BENCH_REPEAT=$REPEAT "$B/cr3bench" "$BENCH/fonts" "$D/cr3gui/data/hyph" \
        "$(cssof "$K" "$T" "$BOOK")" "$BENCH/books/$BOOK" $BPP $SIZE $KERN 40 > /dev/null
    echo "run exit: $?"
    gprof -b -p "$B/cr3bench" gmon.out 2>/dev/null | head -${TOP:-40} | cut -c1-200
    ;;
sample)
    # bench2.sh sample ng <name> std|user parse|load|relayout <book> [repeat]: sampling profiler
    # of the bench itself (BENCH_SAMPLE), build with debug information and the usual optimization
    C=${4:-std}; MODE=${5:-relayout}; BOOK=${6:-big.fb2}; REPEAT=${7:-40}
    config "$K" "$C"
    D=$(tree "$K" "$T"); B="$(build "$K" "$T")-g"
    cmake -S "$D" -B "$B" -DBENCH_G=1 > "$BENCH/cmake2-$K-$T-g.log" 2>&1
    nice -n 15 make -C "$B" -j4 > "$BENCH/make2-$K-$T-g.log" 2>&1
    echo "make exit: $?"
    S="$BENCH/sample-$T-$C-$MODE-${BOOK%%.*}.txt"
    BENCH_SAMPLE="$S" BENCH_PROF=$MODE BENCH_REPEAT=$REPEAT "$B/cr3bench" "$BENCH/fonts" "$D/cr3gui/data/hyph" \
        "$(cssof "$K" "$T" "$BOOK")" "$BENCH/books/$BOOK" $BPP $SIZE $KERN 40 > /dev/null
    echo "run exit: $?"
    python3 "$HERE/sample_report.py" "$B/cr3bench" "$S" ${TOP:-45} ${FOCUS:-}
    ;;
compare)
    A=$3; Bn=$4; C=${5:-std}
    if diff -q "$BENCH/out2-$K-$A-$C.txt" "$BENCH/out2-$K-$Bn-$C.txt" > /dev/null; then
        echo "SAME: $(grep -c '^page' "$BENCH/out2-$K-$Bn-$C.txt") pages, every CRC, point and text length equal"
    else
        echo "DIFFERENT:"
        diff "$BENCH/out2-$K-$A-$C.txt" "$BENCH/out2-$K-$Bn-$C.txt" | head -20
    fi
    ;;
*)
    echo "usage: bench2.sh books | sync|build ng|old <name> | run|cache ng|old <name> std|user [books] | prof ... | compare ..."
    ;;
esac
