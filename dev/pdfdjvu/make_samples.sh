#!/bin/bash
# Diagnostic files of PDF and DjVu (run inside WSL). They are made with the tools built here:
# the full mutool of the PC (build_mupdf.sh gen) and the encoders of DjVuLibre (build-host/tools).
#
#   diag-elements.pdf        text PDF with every element: headings of three levels, paragraphs, bold and
#                            italic, lists, verse, code, tables (ruled, plain, merged cells), pictures with
#                            captions, formulas, footnotes, two columns, running title, page numbers, contents
#   diag-elements.djvu       the same pages as a DjVu "scan" at 300 dpi with the hidden text layer
#                            (page, column, region, para, line, word); pages with pictures are photo pages
#   diag-book.pdf            plain book text, the source of the photographed pages
#   diag-scan-photo.pdf      the book photographed with a camera: 3000 x 4000 JPEG pages, the page in the
#                            middle of the frame, wide margins, turned a little, spine left and right in turn
#   diag-scan-photo.djvu     the same frames as photo pages (IW44)
#   diag-scan-bw.djvu        the same frames made black and white (JB2)
set -u
HERE=$(dirname "$(readlink -f "$0")")
W=$HOME/PBDEV/pdfdjvu/samples-work
OUT=$HERE/samples
M=$HOME/PBDEV/pdfdjvu/mupdf-1.28.6-source/build/gen/mutool
DJ=$HOME/PBDEV/pdfdjvu/djvulibre-3.5.29/build-host/tools
PY="python3 $HERE/make_samples.py"
# frames of the photographed book: 4 keep the files of the repository small (odd and even pages twice)
PAGES=${PAGES:-4}
rm -rf "$W"; mkdir -p "$W" "$OUT"; cd "$W" || exit 1
step() { echo "=== $*"; }

step "text documents"
$PY html . || exit 1
"$M" convert -o elements-raw.pdf -W 420 -H 595 elements.html || exit 1
"$M" run finish.js elements-raw.pdf diag-elements.pdf || exit 1
"$M" convert -o diag-book.pdf -W 420 -H 595 book.html || exit 1
echo "diag-elements.pdf: $("$M" show diag-elements.pdf trailer/Root/Pages/Count) pages," \
     "diag-book.pdf: $("$M" show diag-book.pdf trailer/Root/Pages/Count) pages"

step "pages photographed with a camera"
"$M" draw -q -r 300 -c gray -o book-%d.png diag-book.pdf 1-$PAGES || exit 1
$PY compose . $PAGES || exit 1
LIST=""; for n in $(seq 1 $PAGES); do LIST="$LIST photo-$n.txt"; done
"$M" create -o photo-compose.pdf $LIST || exit 1
"$M" draw -q -r 300 -c gray -o photo-%d.pgm photo-compose.pdf || exit 1
cat > tojpeg.js <<'EOF'
var pixmap = new Image(scriptArgs[0]).toPixmap();
pixmap.saveAsJPEG(scriptArgs[1], 82);
EOF
LIST=""; PHOTO=""; BW=""
for n in $(seq 1 $PAGES); do
    "$M" run tojpeg.js photo-$n.pgm photo-$n.jpg || exit 1
    printf '%%%%MediaBox 0 0 720 960\n%%%%Image Im photo-%d.jpg\nq 720 0 0 960 0 0 cm /Im Do Q\n' $n > scan-$n.txt
    LIST="$LIST scan-$n.txt"
    "$DJ/c44" -dpi 300 photo-$n.pgm ph-$n.djvu || exit 1
    PHOTO="$PHOTO ph-$n.djvu"
    $PY threshold photo-$n.pgm photo-$n.pbm 150 || exit 1
    "$DJ/cjb2" -dpi 300 photo-$n.pbm bw-$n.djvu || exit 1
    BW="$BW bw-$n.djvu"
done
"$M" create -o diag-scan-photo.pdf $LIST || exit 1
"$DJ/djvm" -c diag-scan-photo.djvu $PHOTO || exit 1
"$DJ/djvm" -c diag-scan-bw.djvu $BW || exit 1

step "DjVu with a text layer"
"$M" convert -F stext -O preserve-images -o elements.xml diag-elements.pdf || exit 1
"$M" draw -q -r 300 -c gray -o el-%d.pgm diag-elements.pdf || exit 1
PICTURES=" $($PY djvutxt elements.xml 300 eltxt) "
echo "pages with pictures:$PICTURES"
COUNT=$(ls el-*.pgm | wc -l)
LIST=""; EDIT=""
for n in $(seq 1 $COUNT); do
    case "$PICTURES" in
    *" $n "*) "$DJ/c44" -dpi 300 el-$n.pgm el-$n.djvu || exit 1 ;;
    *) $PY threshold el-$n.pgm el-$n.pbm 128 && "$DJ/cjb2" -dpi 300 el-$n.pbm el-$n.djvu || exit 1 ;;
    esac
    LIST="$LIST el-$n.djvu"
    EDIT="$EDIT select $n; set-txt eltxt-$n.txt;"
done
"$DJ/djvm" -c diag-elements.djvu $LIST || exit 1
"$DJ/djvused" diag-elements.djvu -e "$EDIT save" || exit 1

step "result"
cp diag-elements.pdf diag-elements.djvu diag-book.pdf diag-scan-photo.pdf diag-scan-photo.djvu diag-scan-bw.djvu "$OUT/"
ls -la "$OUT"
"$M" show diag-elements.pdf outline | head -20
