#!/bin/bash
# PDF and DjVu: the two libraries as the program links them, built with the toolchain of the
# reader against the FreeType, zlib and libjpeg of the program (thirdparty/ of the repository),
# so that no library is in the program twice. Run inside WSL:
#
#   JOBS=4 bash build_libs_arm.sh [line]
#
# [line] names the build tree whose headers are used (default "v8": ~/PBDEV/sources/cr3-line-v8);
# the tree must have been configured by cmake once (dev/build/build_package.sh makes zconf.h and
# jconfig.h there). The result: ~/PBDEV/pdfdjvu/arm/lib/*.a, taken by cmake with
# -D PDFDJVU_DIR=~/PBDEV/pdfdjvu (cr3gui/CMakeLists.txt). Sources: fetch_sources.sh.
set -u
LINE=${1:-v8}
PBDEV=${PBDEV:-$HOME/PBDEV}
TREE=$PBDEV/sources/cr3-line-$LINE
D=$PBDEV/pdfdjvu
MU=$D/mupdf-1.28.6-source
DJ=$D/djvulibre-3.5.29
P=$PBDEV/SDK_481/bin/arm-obreey-linux-gnueabi
OUT=$D/arm/lib
mkdir -p "$OUT"
for f in "$TREE/pbpro5/thirdparty/zlib/zconf.h" "$TREE/pbpro5/thirdparty/libjpeg/jconfig.h" \
         "$TREE/thirdparty/freetype/include/ft2build.h"; do
    [ -f "$f" ] || { echo "no $f: configure the tree of the line first"; exit 1; }
done
FT="-I$TREE/thirdparty/freetype/include"
ZL="-I$TREE/thirdparty/zlib -I$TREE/pbpro5/thirdparty/zlib"
JP="-I$TREE/thirdparty/libjpeg/src -I$TREE/pbpro5/thirdparty/libjpeg"
ulimit -v 4000000

echo "===== MuPDF"
cd "$MU" || exit 1
# gcc 4.8.1 takes C of 1990 by default: NAN, INFINITY and isfinite need C99
nice -n 10 make -k -j"${JOBS:-4}" build=release shared=no verbose=no HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no \
    HAVE_OBJCOPY=no html=no xps=no svg=no extract=no mujs=no brotli=no tesseract=no barcode=no \
    tofu=yes tofu_cjk=yes \
    USE_SYSTEM_FREETYPE=yes SYS_FREETYPE_CFLAGS="$FT" SYS_FREETYPE_LIBS= \
    USE_SYSTEM_ZLIB=yes SYS_ZLIB_CFLAGS="$ZL" SYS_ZLIB_LIBS= \
    USE_SYSTEM_LIBJPEG=yes SYS_LIBJPEG_CFLAGS="$JP" SYS_LIBJPEG_LIBS= \
    OUT=build/pb-arm-sys CC="$P-gcc" CXX="$P-g++" AR="$P-ar" LD="$P-ld" RANLIB="$P-ranlib" \
    XCFLAGS="-std=gnu99" libs 2>&1 | head -c 20000000 > "$D/mupdf-arm-sys.log"
echo "make exit: ${PIPESTATUS[0]}; errors: $(grep -c -E 'error:|Error [0-9]' "$D/mupdf-arm-sys.log")"
grep -E 'error:|Error [0-9]' "$D/mupdf-arm-sys.log" | sed -E 's/^.*mupdf-1.28.6-source\///' | sort | uniq -c | sort -rn | head -20
cp build/pb-arm-sys/libmupdf.a build/pb-arm-sys/libmupdf-third.a "$OUT/" || exit 1

echo "===== DjVuLibre"
B=$DJ/build-arm-sys
rm -rf "$B"; mkdir -p "$B" && cd "$B" || exit 1
FLAGS="-O2 -ffunction-sections -fdata-sections"
CC="$P-gcc" CXX="$P-g++" AR="$P-ar" RANLIB="$P-ranlib" STRIP="$P-strip" NM="$P-nm" \
CFLAGS="$FLAGS" CXXFLAGS="$FLAGS" CPPFLAGS="$JP" LDFLAGS="-L$TREE/pbpro5/thirdparty/libjpeg" \
    ../configure --host=arm-obreey-linux-gnueabi --disable-shared --enable-static \
    --disable-xmltools --disable-desktopfiles --without-tiff > "$D/djvu-arm-sys.log" 2>&1
echo "configure exit: $?"
grep -i -E "JPEG support|Pthread support" "$D/djvu-arm-sys.log"
nice -n 10 make -k -j"${JOBS:-4}" -C libdjvu >> "$D/djvu-arm-sys.log" 2>&1
echo "make exit: $?; errors: $(grep -c -E 'error:|Error [0-9]' "$D/djvu-arm-sys.log")"
cp libdjvu/.libs/libdjvulibre.a "$OUT/" || exit 1

echo "===== result"
ls -la "$OUT"
echo "FreeType drivers of the program (a PDF needs truetype, cff, type1, cid):"
grep -h -o 'FT_USE_MODULE( *[A-Za-z_]*, *[a-z0-9_]*' "$TREE"/thirdparty/freetype/include/freetype/config/ftmodule.h \
    "$TREE"/pbpro5/thirdparty/freetype/include/freetype/config/ftmodule.h 2>/dev/null | sed 's/.*, *//' | sort -u | tr '\n' ' '
echo
