#!/bin/bash
# Stage 0 of PDF and DjVu: djvuprobe linked with the DjVuLibre library (run inside WSL).
#   link_djvuprobe.sh arm|host
# Shows the size of the stripped program: what DjVuLibre adds.
set -u
KIND=${1:-arm}
HERE=$(dirname "$(readlink -f "$0")")
SRC=$HOME/PBDEV/pdfdjvu/djvulibre-3.5.29
OUTDIR=$HOME/PBDEV/pdfdjvu/bin
mkdir -p "$OUTDIR"
if [ "$KIND" = arm ]; then
    P=$HOME/PBDEV/SDK_481/bin/arm-obreey-linux-gnueabi-
    JPEG=-ljpeg
else
    # WSL has no libjpeg for development: the library of the PC bench is built without JPEG pages
    P=
    JPEG=
fi
BIN=$OUTDIR/djvuprobe-$KIND
"${P}g++" -O2 -ffunction-sections -fdata-sections -I"$SRC" "$HERE/djvuprobe.cpp" \
    "$SRC/build-$KIND/libdjvu/.libs/libdjvulibre.a" $JPEG -lrt -lpthread -lm -Wl,--gc-sections -o "$BIN" 2>&1 | head -40
[ -f "$BIN" ] || { echo "not linked"; exit 1; }
cp "$BIN" "$BIN.stripped"
"${P}strip" "$BIN.stripped"
ls -la "$BIN.stripped"
"${P}size" "$BIN.stripped"
if [ "$KIND" = arm ]; then
    echo "--- newest glibc symbol version needed"
    "${P}objdump" -T "$BIN" | grep -o 'GLIBC_[0-9.]*' | sort -u -t. -k1,1 -k2,2n -k3,3n | tail -3
    echo "--- shared libraries needed"
    "${P}readelf" -d "$BIN" | grep NEEDED
fi
