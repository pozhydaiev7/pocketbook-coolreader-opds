#!/bin/bash
# Stage 0 of PDF and DjVu: pdfprobe linked with the MuPDF libraries (run inside WSL).
#   link_probe.sh arm|host
# Shows the undefined references, if any, and the size of the stripped program: what MuPDF adds.
set -u
KIND=${1:-arm}
HERE=$(dirname "$(readlink -f "$0")")
SRC=$HOME/PBDEV/pdfdjvu/mupdf-1.28.6-source
OUTDIR=$HOME/PBDEV/pdfdjvu/bin
mkdir -p "$OUTDIR"
if [ "$KIND" = arm ]; then
    P=$HOME/PBDEV/SDK_481/bin/arm-obreey-linux-gnueabi-
    LIB=$SRC/build/pb-arm
    DEFS=-DNEED_GETENTROPY
else
    P=
    LIB=$SRC/build/host
    DEFS=-D_GNU_SOURCE
fi
BIN=$OUTDIR/pdfprobe-$KIND
"${P}gcc" -O2 -std=gnu99 $DEFS -ffunction-sections -fdata-sections -I"$SRC/include" "$HERE/pdfprobe.c" \
    -L"$LIB" -lmupdf -lmupdf-third -lm -lrt -lpthread -Wl,--gc-sections -o "$BIN" 2>&1 | head -40
[ -f "$BIN" ] || { echo "not linked"; exit 1; }
cp "$BIN" "$BIN.stripped"
"${P}strip" "$BIN.stripped"
ls -la "$BIN" "$BIN.stripped"
"${P}size" "$BIN.stripped"
file "$BIN.stripped" | cut -c1-160
if [ "$KIND" = arm ]; then
    echo "--- newest glibc symbol version needed"
    "${P}objdump" -T "$BIN" | grep -o 'GLIBC_[0-9.]*' | sort -u -t. -k1,1 -k2,2n -k3,3n | tail -3
    echo "--- shared libraries needed"
    "${P}readelf" -d "$BIN" | grep NEEDED
fi
