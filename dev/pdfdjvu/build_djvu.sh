#!/bin/bash
# Stage 0 of PDF and DjVu: DjVuLibre built as a static library (run inside WSL).
#   build_djvu.sh arm    with the toolchain of the reader (SDK_481, gcc 4.8.1, glibc 2.9)
#   build_djvu.sh host   with the compiler of WSL, for the PC bench
# At most 4 jobs, nice. The log: ~/PBDEV/pdfdjvu/djvu-<kind>.log
set -u
KIND=${1:-arm}
SRC=$HOME/PBDEV/pdfdjvu/djvulibre-3.5.29
LOG=$HOME/PBDEV/pdfdjvu/djvu-$KIND.log
P=$HOME/PBDEV/SDK_481/bin/arm-obreey-linux-gnueabi
B=$SRC/build-$KIND
[ "${CLEAN:-0}" = 1 ] && rm -rf "$B"
mkdir -p "$B" && cd "$B" || exit 1
FLAGS="-O2 -ffunction-sections -fdata-sections"
if [ ! -f Makefile ]; then
    if [ "$KIND" = arm ]; then
        CC="$P-gcc" CXX="$P-g++" AR="$P-ar" RANLIB="$P-ranlib" STRIP="$P-strip" NM="$P-nm" \
        CFLAGS="$FLAGS" CXXFLAGS="$FLAGS" \
            ../configure --host=arm-obreey-linux-gnueabi --disable-shared --enable-static \
            --disable-xmltools --disable-desktopfiles --without-tiff > "$LOG" 2>&1
    else
        CFLAGS="$FLAGS" CXXFLAGS="$FLAGS" \
            ../configure --disable-shared --enable-static \
            --disable-xmltools --disable-desktopfiles --without-tiff > "$LOG" 2>&1
    fi
    echo "configure exit: $?"
    grep -n -i -E "jpeg|pthread|thread model|error" "$LOG" | tail -12
fi
ulimit -v 4000000
nice -n 10 make -k -j"${JOBS:-4}" -C libdjvu >> "$LOG" 2>&1
echo "make exit: $?"
echo "errors: $(grep -c -E 'error:|Error [0-9]' "$LOG")  warnings: $(grep -c 'warning:' "$LOG")"
grep -E 'error:|Error [0-9]|undefined reference' "$LOG" | sort | uniq -c | sort -rn | head -30
echo "--- result"
ls -la libdjvu/.libs/*.a 2>/dev/null
