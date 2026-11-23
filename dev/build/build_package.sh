#!/bin/bash
# Builds the device package (pro5: firmware 5 and 6, SDK_481) of the clone this script lies in.
# Run inside WSL:
#
#   JOBS=4 bash <clone>/dev/build/build_package.sh <line> [clean|check]
#
# "clean" builds the tree from nothing. "check" stops after the program is compiled and linked:
# no release archive and no package (the owner had asked for no package; the code for the reader
# still has to be known to build).
#
# <line> names the build tree: <PBDEV>/sources/cr3-line-<line>. Every line of versions (a branch
# checked out in a clone or a git worktree of its own) has its tree and its object files, so a
# switch between the lines rebuilds nothing. Line in use: "master" (the release 0.0.8-opds was
# built in the tree "v8" of the former branch pdf-djvu).
#
# Steps: the clone is copied into the tree with LF line ends (sync.py); cmake and make in
# <tree>/pbpro5; the binary is stripped; updateReleases.sh makes the release archive; the package
# <out>/cr3-v<version>-pro5.zip is made from it and checked (the same archive is copied to the
# reader by install_pkg.py and installed by the update over the network). The package has the file
# associations and the switch of the Applications screen: it sets up a reader that never had
# CoolReader as well.
#
# Environment: PBDEV (default ~/PBDEV, holds SDK_481), CR3_OUT (default
# /mnt/c/Temp/cr3-opds-build/beta), JOBS (default 4).
set -u
LINE=${1:?usage: build_package.sh <line> [clean|check]}
HERE=$(dirname "$(readlink -f "$0")")
CLONE=$(cd "$HERE/../.." && pwd)
PBDEV=${PBDEV:-$HOME/PBDEV}
TREE=$PBDEV/sources/cr3-line-$LINE
OUT=${CR3_OUT:-/mnt/c/Temp/cr3-opds-build/beta}
SDK=$PBDEV/SDK_481
LOGS=$PBDEV/logs
mkdir -p "$LOGS" "$OUT" "$PBDEV/releases/coolreader3/dev"

echo "===== $LINE: sync $CLONE -> $TREE"
python3 "$HERE/sync.py" "$CLONE" "$TREE" || exit 1
cd "$TREE" || exit 1
VER=$(grep CR_PB_VERSION cr3gui/src/cr3pocketbook.h | awk '{print $3}' | tr -d '"')
echo "version: $VER"
if [ "${2:-}" = clean ]; then rm -rf pbpro5; fi
mkdir -p pbpro5 && cd pbpro5 || exit 1

echo "===== $LINE: build"
# The copies of the translation files in the build folder are made from the sources at every
# build: a kept copy does not get new strings, and a damaged one was merged again and again
# until it took 2.7 GB and all the memory of the PC.
rm -f cr3gui/po/*.po cr3gui/po/*.upd
# the skin archives are made again: their make rule does not depend on the files of a skin
rm -f cr3gui/*.cr3skin
# the compiled translations are made again; their folders are made by cmake, not by make
find i18n -name "*.mo" -delete 2>/dev/null
[ -d i18n/ru/LC_MESSAGES ] || { [ -f Makefile ] && cmake . > /dev/null 2>&1; }
export CMAKE_POLICY_VERSION_MINIMUM=3.5
# PDF and DjVu: the line that has the picture mode is built with the libraries of
# dev/pdfdjvu/build_libs_arm.sh when they are there
PAGEDOC=
if [ -f "$TREE/cr3gui/src/pb_pagedoc.cpp" ] && [ -f "$PBDEV/pdfdjvu/arm/lib/libmupdf.a" ]; then
    PAGEDOC="-D PDFDJVU_DIR=$PBDEV/pdfdjvu"
    echo "PDF and DjVu: with the libraries of $PBDEV/pdfdjvu/arm/lib"
fi
if [ ! -f Makefile ]; then
    cmake $PAGEDOC \
        -D CMAKE_TOOLCHAIN_FILE="$TREE/tools/toolchain-arm-obreey-linux-gnueabi-pocketbook.cmake" \
        -D TARGET_TYPE=ARM \
        -D DEVICE_NAME=pb360 \
        -D MAX_IMAGE_SCALE_MUL=2 \
        -D CMAKE_BUILD_TYPE=Release \
        -D ENABLE_CHM=1 \
        -D ENABLE_ANTIWORD=1 \
        -D GUI=CRGUI_PB \
        -D ENABLE_PB_DB_STATE=1 \
        -D BACKGROUND_CACHE_FILE_CREATION=1 \
        -D POCKETBOOK_PRO=1 \
        -D POCKETBOOK_PRO_FW5=1 \
        -D DOC_BUFFER_SIZE=0xA00000 \
        -D CMAKE_EXE_LINKER_FLAGS="-Wl,--exclude-libs,ALL" \
        -D CMAKE_POLICY_DEFAULT_CMP0065=NEW \
        .. > "$LOGS/cmake-$LINE.log" 2>&1
    echo "cmake exit: $?"
    grep -n -E "CMake Error|FATAL" -A6 "$LOGS/cmake-$LINE.log" | head -40
fi
# safety net: no process of the build may take more than 4 GB, the log is cut at 20 MB
ulimit -v 4000000
nice -n 10 make -k -j"${JOBS:-4}" 2>&1 | head -c 20000000 > "$LOGS/make-$LINE.log"
RC=${PIPESTATUS[0]}
echo "make exit: $RC"
grep -E "error:|Error [0-9]|undefined reference|No rule to make|msgmerge failed" "$LOGS/make-$LINE.log" \
    | sed -E "s|^.*cr3-line-$LINE/||" | sort | uniq -c | sort -rn | head -40
echo "errors: $(grep -c -E 'error:' "$LOGS/make-$LINE.log")  warnings: $(grep -c 'warning:' "$LOGS/make-$LINE.log")"
[ "$RC" = 0 ] || exit 1
if [ "${2:-}" = check ]; then
    echo "check only: the program compiles and links; no release archive and no package are made"
    ls -la "$TREE/pbpro5/cr3gui/cr3-pb.app"
    exit 0
fi

echo "===== $LINE: release archive"
cd "$TREE" || exit 1
"$SDK/bin/arm-obreey-linux-gnueabi-strip" pbpro5/cr3gui/cr3-pb.app
bash updateReleases.sh pro5 > "$LOGS/release-$LINE.log" 2>&1
echo "updateReleases exit: $?"
RE=$SDK/bin/arm-obreey-linux-gnueabi-readelf
echo "shared libraries: $($RE -d pbpro5/cr3gui/cr3-pb.app | grep NEEDED | sed 's/.*\[//;s/\]//' | sort | tr '\n' ' ')"

echo "===== $LINE: package"
SRC=$PBDEV/releases/coolreader3/dev/cr3-v$VER-pro5.zip
WORK=/tmp/cr3-package-check-$LINE
rm -rf "$WORK"; mkdir -p "$WORK"; cd "$WORK" || exit 1
unzip -q "$SRC" || exit 1
# The file associations (system/config/extensions.cfg: the firmware offers a program for a book
# only by that file) and the switch of the Applications screen are part of the package. A reader
# is set up by copying the package whole, as it has always been done with this program, and both
# replace those of the reader at every install. Nothing else of the settings may be in a package.
[ "$(find system/config -type f | sort | tr '\n' ' ')" = "system/config/extensions.cfg " ] \
    || { echo "system/config of the package is not extensions.cfg alone:"; find system/config -type f; exit 1; }
# the line ends of Windows, as in the archives the readers have been set up from
sed -i 's/\r$//; s/$/\r/' system/config/extensions.cfg
grep -q "^fb2:.*:1:cr3-pb.app," system/config/extensions.cfg || { echo "extensions.cfg does not name CoolReader first"; exit 1; }
[ "$(ls applications)" = "Cool Reader Selector.app" ] || { echo "applications of the package:"; ls applications; exit 1; }
grep -q "$VER" system/share/cr3/bin/cr3-pb.app || { echo "version $VER not in the binary"; exit 1; }
for f in system/bin/cr3-pb.app install.sh "applications/Cool Reader Selector.app"; do
    [ -f "$f" ] || { echo "no $f"; exit 1; }
    [ "$(tr -cd '\r' < "$f" | wc -c)" = 0 ] || { echo "$f has CRLF"; exit 1; }
    sh -n "$f" || { echo "$f: syntax error"; exit 1; }
done
grep -q "^Version: $VER\$" package.version || { echo "package.version is not of $VER"; cat package.version; exit 1; }
# the list of files is the folder, name by name
( cd system/share/cr3 && find . -type f | sed 's|^\./||' | sort ) > "$WORK/files.now"
sort system/share/cr3/package.files > "$WORK/files.listed"
cmp -s "$WORK/files.now" "$WORK/files.listed" \
    || { echo "package.files differs from the folder:"; diff "$WORK/files.now" "$WORK/files.listed" | head; exit 1; }
rm -f "$WORK/files.now" "$WORK/files.listed"
# The folder of the file associations is spelled "system/Config" in the package. The versions
# 0.0.7-opds to 0.0.7.1-opds-alpha10 refuse an update that has a name beginning with
# "system/config", in these letters exactly; to them "system/Config" is not of the settings, so they
# take the package over the network. On the storage of a reader and on Windows (FAT and NTFS name
# files without the case of the letters) it is the folder system/config all the same, whether the
# package is copied by hand or unpacked by the launcher. install.sh looks for both spellings.
mv system/config system/Config || exit 1
[ -z "$(find . -path './system/config*')" ] || { echo "a name of the package begins with system/config"; exit 1; }
PKG="$OUT/cr3-v$VER-pro5.zip"
rm -f "$PKG"
zip -q -r "$PKG" system applications install.sh package.version
echo "package.files: $(wc -l < system/share/cr3/package.files) files; file associations: $(wc -l < system/Config/extensions.cfg) kinds of files"
ls -la "$PKG"
sha256sum "$PKG"
