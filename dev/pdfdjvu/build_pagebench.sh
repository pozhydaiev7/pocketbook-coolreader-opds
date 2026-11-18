#!/bin/bash
# PC bench of the page documents: cr3gui/src/pb_pagedoc.cpp with the libraries built for the PC
# (build_mupdf.sh host, build_djvu.sh host). Run inside WSL:  build_pagebench.sh
set -u
HERE=$(dirname "$(readlink -f "$0")")
CLONE=$(cd "$HERE/../.." && pwd)
MU=$HOME/PBDEV/pdfdjvu/mupdf-1.28.6-source
DJ=$HOME/PBDEV/pdfdjvu/djvulibre-3.5.29
OUT=$HOME/PBDEV/pdfdjvu/bin
mkdir -p "$OUT"
# the sources are copied with LF line ends: the clone has CRLF
T=/tmp/pagebench-src; rm -rf "$T"; mkdir -p "$T"
for f in "$CLONE/cr3gui/src/pb_pagedoc.cpp" "$CLONE/cr3gui/src/pb_pagedoc.h" "$HERE/pagebench.cpp"; do
    tr -d '\r' < "$f" > "$T/$(basename "$f")"
done
g++ -std=gnu++11 -O2 -Wall -I"$T" -I"$MU/include" -I"$DJ" "$T/pagebench.cpp" "$T/pb_pagedoc.cpp" \
    -L"$MU/build/host" -lmupdf -lmupdf-third "$DJ/build-host/libdjvu/.libs/libdjvulibre.a" \
    -lpthread -lm -o "$OUT/pagebench" 2>&1 | head -40
ls -la "$OUT/pagebench"
