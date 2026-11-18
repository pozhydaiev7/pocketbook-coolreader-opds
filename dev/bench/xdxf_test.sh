#!/bin/bash
# Desktop check of the kept index of the XDXF dictionaries (pb_xdxf.cpp of the clone).
#   bash xdxf_test.sh
set -e
SRC=/mnt/c/Temp/pocketbook-coolreader-opds.git/cr3gui/src
HERE=/mnt/c/Temp/cr3-opds-build/bench-ng
WORK=~/PBDEV/bench/xdxf-test
rm -rf "$WORK"
mkdir -p "$WORK/cache"
python3 - "$WORK/dict.xdxf" <<'EOF'
import sys, random
random.seed(7)
letters = "абвгдежзиклмнопрстуфхцчшэюя"
with open(sys.argv[1], "w", encoding="utf-8") as f:
    f.write('<?xml version="1.0" encoding="UTF-8" ?>\n<xdxf lang_from="RUS" lang_to="ENG" format="visual">\n'
            '<full_name>Test dictionary</full_name>\n<description>generated</description>\n')
    for i in range(120000):
        word = "".join(random.choice(letters) for _ in range(random.randint(3, 12)))
        extra = "<k>%s<opt> (%d)</opt></k>" % (word[::-1], i) if i % 7 == 0 else ""
        f.write("<ar><k>%s</k>%s\n<tr>%s</tr> article %d &amp; text <i>%s</i></ar>\n" % (word, extra, word, i, word * 3))
    f.write("</xdxf>\n")
EOF
ls -la "$WORK/dict.xdxf"
# zlib of the bench tree: the headers are not installed in WSL
ZT=$(ls -d ~/PBDEV/bench/src-ng-*/thirdparty/zlib* | tail -1)
ZL=$(find ~/PBDEV/bench/build-ng-a14 -name "libz*.a" | head -1)
echo "zlib: $ZT $ZL"
g++ -O2 -o "$WORK/xdxf_test" "$HERE/xdxf_test.cpp" "$SRC/pb_xdxf.cpp" "$SRC/pb_stardict.cpp" -I"$SRC" -I"$ZT" "$ZL"
"$WORK/xdxf_test" "$WORK/dict.xdxf" "$WORK/cache"
ls -la "$WORK/cache"
# a dictionary that changed: the kept index is not used
echo "<!-- changed -->" >> "$WORK/dict.xdxf"
"$WORK/xdxf_test" "$WORK/dict.xdxf" "$WORK/cache" | sed 's/^/changed file: /'
# a kept index cut short: not used
head -c 20000 "$WORK"/cache/xdxf-*.idx > "$WORK/cut" && cp "$WORK/cut" "$(ls "$WORK"/cache/xdxf-*.idx)"
"$WORK/xdxf_test" "$WORK/dict.xdxf" "$WORK/cache" | sed 's/^/cut index: /' || true
