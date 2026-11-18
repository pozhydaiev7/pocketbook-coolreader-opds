#!/bin/bash
# Stage 0 of PDF and DjVu: MuPDF built as static libraries (run inside WSL).
#   build_mupdf.sh arm    with the toolchain of the reader (SDK_481, gcc 4.8.1, glibc 2.9)
#   build_mupdf.sh host   with the compiler of WSL, with mutool, for the PC bench
# At most 4 jobs, nice. The log: ~/PBDEV/pdfdjvu/mupdf-<kind>.log
set -u
KIND=${1:-arm}
SRC=$HOME/PBDEV/pdfdjvu/mupdf-1.28.6-source
LOG=$HOME/PBDEV/pdfdjvu/mupdf-$KIND.log
P=$HOME/PBDEV/SDK_481/bin/arm-obreey-linux-gnueabi
cd "$SRC" || exit 1

# what the reader does not need: the viewers, the network, the other document formats, JavaScript,
# OCR, bar codes; the fonts of the scripts outside Latin, Greek and Cyrillic (tofu), the CJK fonts
OPTS="build=release shared=no verbose=no HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no HAVE_OBJCOPY=no
html=no xps=no svg=no extract=no mujs=no brotli=no tesseract=no barcode=no tofu=yes tofu_cjk=yes"

ulimit -v 4000000
# CLEAN=1: every object is made again
[ "${CLEAN:-0}" = 1 ] && rm -rf build/pb-arm build/host
if [ "$KIND" = arm ]; then
    # gcc 4.8.1 takes C of 1990 by default: NAN, INFINITY and isfinite need C99
    nice -n 10 make -k -j"${JOBS:-4}" $OPTS OUT=build/pb-arm \
        CC="$P-gcc" CXX="$P-g++" AR="$P-ar" LD="$P-ld" RANLIB="$P-ranlib" \
        XCFLAGS="${XCFLAGS:--std=gnu99}" libs 2>&1 | head -c 20000000 > "$LOG"
    echo "make exit: ${PIPESTATUS[0]}"
    OUT=build/pb-arm
elif [ "$KIND" = gen ]; then
    # the whole mutool of the PC, with the HTML layout, the fonts and JavaScript: it makes the
    # diagnostic files (make_samples.sh); it is not what goes to the reader
    nice -n 10 make -k -j"${JOBS:-4}" build=release shared=no verbose=no HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no \
        OUT=build/gen tools 2>&1 | head -c 20000000 > "$LOG"
    echo "make exit: ${PIPESTATUS[0]}"
    OUT=build/gen
else
    nice -n 10 make -k -j"${JOBS:-4}" $OPTS OUT=build/host libs tools 2>&1 | head -c 20000000 > "$LOG"
    echo "make exit: ${PIPESTATUS[0]}"
    OUT=build/host
fi
echo "errors: $(grep -c -E 'error:|Error [0-9]' "$LOG")  warnings: $(grep -c 'warning:' "$LOG")"
grep -E 'error:|Error [0-9]|undefined reference|No rule to make' "$LOG" | sed -E 's/^.*mupdf-1.28.6-source\///' \
    | sort | uniq -c | sort -rn | head -${ERRS:-40}
echo "--- result"
ls -la "$OUT"/*.a "$OUT"/mutool 2>/dev/null
