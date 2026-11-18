#!/bin/bash
# PDF and DjVu: the sources of the two libraries, downloaded and unpacked into ~/PBDEV/pdfdjvu
# (run inside WSL). The checksums are those of the archives the first builds were made of.
set -u
D=$HOME/PBDEV/pdfdjvu
mkdir -p "$D/dl" && cd "$D/dl" || exit 1
get() { # url sha256
    local f; f=$(basename "$1")
    [ -s "$f" ] || curl -sS -L --fail --max-time 600 -o "$f" "$1" || { echo "FAILED $1"; rm -f "$f"; exit 1; }
    echo "$2  $f" | sha256sum -c - || exit 1
}
get https://mupdf.com/downloads/archive/mupdf-1.28.6-source.tar.gz \
    dc85000640a45d90ca9b77b061f6ca67cce0c4d4fbecf8819647baf573eef101
get https://downloads.sourceforge.net/project/djvu/DjVuLibre/3.5.29/djvulibre-3.5.29.tar.gz \
    d3b4b03ae2bdca8516a36ef6eb27b777f0528c9eda26745d9962824a3fdfeccf
cd "$D" || exit 1
[ -d mupdf-1.28.6-source ] || tar xzf dl/mupdf-1.28.6-source.tar.gz
[ -d djvulibre-3.5.29 ] || tar xzf dl/djvulibre-3.5.29.tar.gz
ls -d "$D"/mupdf-1.28.6-source "$D"/djvulibre-3.5.29
