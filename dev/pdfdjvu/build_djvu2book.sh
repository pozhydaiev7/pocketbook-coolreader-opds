#!/bin/bash
# PC tool of the translator "a DjVu with a text layer as a book": cr3gui/src/pb_djvutext.cpp with
# the libraries built for the PC (build_djvu.sh host, build_mupdf.sh host: MuPDF packs the
# pictures). Run inside WSL:  build_djvu2book.sh
set -u
HERE=$(dirname "$(readlink -f "$0")")
CLONE=$(cd "$HERE/../.." && pwd)
MU=$HOME/PBDEV/pdfdjvu/mupdf-1.28.6-source
DJ=$HOME/PBDEV/pdfdjvu/djvulibre-3.5.29
OUT=$HOME/PBDEV/pdfdjvu/bin
mkdir -p "$OUT"
# the sources are copied with LF line ends: the clone may have CRLF
T=$HOME/PBDEV/pdfdjvu/djvu2book-src; rm -rf "$T"; mkdir -p "$T"
for f in "$CLONE/cr3gui/src/pb_djvutext.cpp" "$CLONE/cr3gui/src/pb_djvutext.h" "$HERE/djvu2book.cpp"; do
    tr -d '\r' < "$f" > "$T/$(basename "$f")"
done
g++ -std=gnu++98 -O2 -Wall -Wextra -I"$T" -I"$MU/include" -I"$DJ" "$T/djvu2book.cpp" "$T/pb_djvutext.cpp" \
    -L"$MU/build/host" -lmupdf -lmupdf-third "$DJ/build-host/libdjvu/.libs/libdjvulibre.a" \
    -lpthread -lm -o "$OUT/djvu2book" 2>&1 | head -40
ls -la "$OUT/djvu2book"
