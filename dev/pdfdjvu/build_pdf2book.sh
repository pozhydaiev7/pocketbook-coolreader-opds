#!/bin/bash
# PC tool of the translator "a PDF with text as a book": cr3gui/src/pb_pdftext.cpp with the MuPDF
# built for the PC (build_mupdf.sh host). Run inside WSL:  build_pdf2book.sh
set -u
HERE=$(dirname "$(readlink -f "$0")")
CLONE=$(cd "$HERE/../.." && pwd)
MU=$HOME/PBDEV/pdfdjvu/mupdf-1.28.6-source
OUT=$HOME/PBDEV/pdfdjvu/bin
mkdir -p "$OUT"
# the sources are copied with LF line ends: the clone may have CRLF
T=$HOME/PBDEV/pdfdjvu/pdf2book-src; rm -rf "$T"; mkdir -p "$T"
for f in "$CLONE/cr3gui/src/pb_pdftext.cpp" "$CLONE/cr3gui/src/pb_pdftext.h" "$HERE/pdf2book.cpp"; do
    tr -d '\r' < "$f" > "$T/$(basename "$f")"
done
g++ -std=gnu++98 -O2 -Wall -Wextra -I"$T" -I"$MU/include" "$T/pdf2book.cpp" "$T/pb_pdftext.cpp" \
    -L"$MU/build/host" -lmupdf -lmupdf-third -lpthread -lm -o "$OUT/pdf2book" 2>&1 | head -40
ls -la "$OUT/pdf2book"
