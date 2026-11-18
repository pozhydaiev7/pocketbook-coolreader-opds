# Development guide

For a developer, or an assistant such as Claude, who starts on a machine without the history of the
work: where the things are, how a package is built and put on a reader, how a change is measured,
and the rules the owner of the project has set. Read this file first; it replaces a long search.

## The project in a paragraph

CoolReader 3 for PocketBook e-ink readers (the CRGUI front end, no Qt): the fork of
`blchinezu/pocketbook-coolreader` moved to CREngine 3.2.59, with an OPDS catalog browser, reading
statistics, an update over the network from the GitHub releases, a dictionary window, touch lists.
The device at hand is an InkPad 4 (PB743G, firmware 6.11); a tester has a PocketBook 632. The
package target is `pro5` (firmware 5 and 6, toolchain `SDK_481`).

## Branches and versions

| Line | Branch | Versions | State |
|---|---|---|---|
| current | `master` | `0.0.7.x-opds`, test builds `-alphaN` | released: `0.0.7-opds` |
| PDF and DjVu | `pdf-djvu` | `0.0.8-opds-alphaN` | in work; not merged into `master`; `master` is merged into it often |

The version is `CR_PB_VERSION` and `CR_PB_BUILD_DATE` in `cr3gui/src/cr3pocketbook.h`. A test build
is a GitHub pre-release; the update screen of the program offers pre-releases only with *Test
versions* on. Release notes: English first, then `## Русский` and `## Українська`.

Two lines are kept side by side as two working folders of one repository (`git worktree`), so both
can be built without a switch:

    git worktree add ../pocketbook-coolreader-opds-v8 pdf-djvu
    git -C ../pocketbook-coolreader-opds-v8 merge master        # bring master into the line

## Where the code is

| Path | What |
|---|---|
| `crengine/` | the engine: parsing, styles, layout, drawing (`src/lvdocview.cpp`, `lvtinydom.cpp`, `lvrend.cpp`, `lvtextfm.cpp`, `lvstsheet.cpp`), the window toolkit `crgui` |
| `cr3gui/src/` | the program: `cr3pocketbook.cpp` (device layer, events, screen), `mainwnd.cpp` (book window), `settings.cpp` (settings menu), `viewdlg.cpp` (text windows), `citedlg.cpp`, `pb_*.cpp` (dictionaries, statistics, font lists, contents list, debugging), `ota_update.cpp`, `web.cpp` |
| `cr3gui/src/opds/` | the OPDS browser and the touch list `OpdsMenu` every list screen is made of |
| `cr3gui/po/` | translations (`cr3_ru.po`, `cr3_uk.po`, ...) |
| `cr3gui/data/devices/pb360/skins/` | skins by screen (`pb740fw5`, `pbXfw5`, `pb626fw5`, ...) |
| `data/common/` | what goes into a package as is: `system/share/cr3` (styles, manuals in ru, en, uk), `install.sh` |
| `thirdparty/` | bundled libraries, listed with versions in `thirdparty/versions.json` |
| `tools/` | cmake toolchain files |
| `CHANGELOG.md` | every change with the functions touched, the reason and the measurement |
| `dev/` | this folder: build scripts, the PC bench, sample books |

## The build environment

Windows with WSL (Ubuntu). The clone lies on the Windows side (`core.autocrlf=true`, CRLF in the
working tree); the build runs inside WSL on a copy with LF line ends.

Inside WSL, `~/PBDEV` (the layout of `blchinezu/pocketbook-sdk`):

- `~/PBDEV/SDK_481`: the PocketBook SDK for firmware 5 (<https://github.com/pocketbook/SDK_481>):
  `arm-obreey-linux-gnueabi`, gcc 4.8.1 (crosstool-NG 1.19.0), glibc 2.9, armv7-a softfp neon.
  C code needs `-std=gnu99` for C99 library macros; the compiler defaults to C90.
- `~/PBDEV/sources/cr3-line-<line>`: the build tree of a line, made by the build script.
- `~/PBDEV/releases/coolreader3/dev`: release archives made by `updateReleases.sh`.
- Host packages: `cmake`, `make`, `gettext`, `zip`, `unzip`, `rsync`, `python3`.

Rules of the owner for WSL: say before a build starts; at most 4 jobs, with `nice`; batch the
changes and build once; `wsl.exe --shutdown` when the work in WSL is over (a runaway build once
took all the memory of the PC). From Git Bash a WSL script is started as
`MSYS_NO_PATHCONV=1 wsl.exe -e bash /mnt/c/...`.

## Building a package

    JOBS=4 bash <clone>/dev/build/build_package.sh <line> [clean]

`<line>` is `master` or `v8`; it only names the build tree, the sources are those of the clone the
script lies in. The script copies the clone into the tree (`sync.py`), runs cmake and make in
`<tree>/pbpro5`, strips the binary, runs `updateReleases.sh pro5` and writes the checked package
`cr3-v<version>-pro5.zip` to `C:\Temp\cr3-opds-build\beta` (`CR3_OUT`). The first build of a tree
is a full one; later builds compile what changed.

Before a build: raise `CR_PB_VERSION` and `CR_PB_BUILD_DATE`, add the changelog entry.

Checks and install (Windows side, the reader connected by USB as drive `D:`):

    python -I dev/build/check_pkg.py <zip> <version> [text expected in the binary...]
    python -I dev/build/install_pkg.py <zip> D:/

`install_pkg.py` copies `system/bin` and `system/share/cr3` and compares every file. Ask the owner
to connect the reader before touching `D:`; never delete anything on the reader (he does that
himself); after the install give him exact steps with the names of the menu items in Russian.

The log of the program on the reader: *Настройки → Отладка* writes
`system/share/cr3/crlog.ini`; the log is `/mnt/ext1/cr3log.txt` (the previous run:
`cr3log.prev.txt`), drive root on the PC. Timing lines start with `perf:`. The log must never change
what the program does.

## The PC bench

`dev/bench` builds the engine for the PC inside WSL and runs books through it: page images hashed
page by page, times of parsing, layout and drawing. It is how a change of the engine is proved
harmless (every page of every book the same) and how a speed-up is measured before a device sees it.

    bash dev/bench/bench2.sh books                    # copy the test books to ~/PBDEV/bench/books
    bash dev/bench/bench2.sh sync ng <name>           # tree <name> from the build tree of master
    SRCNG=clone bash dev/bench/bench2.sh sync ng <name>   # ... with the engine of the clone
    bash dev/bench/bench2.sh build ng <name>
    bash dev/bench/bench2.sh run ng <name> std|user [books...]
    bash dev/bench/bench2.sh compare ng <a> <b> std|user

- `bench2.cpp` is the bench program; environment switches `BENCH_*` (`BENCH_RELAYOUT`,
  `BENCH_PREPARE`, `BENCH_IMAGE`, `BENCH_SAMPLE` with `sample_report.py`, `BENCH_ELEMFACES`,
  `BENCH_DUMP`) are described at its top.
- It needs `~/PBDEV/bench/fonts`: a folder of TrueType fonts (the fonts of the reader).
- `dev/samples/lab.fb2`, `lab.epub`: laboratory books made by `dev/bench/make_lab_books.py`, with
  every element the engine lays out (tables with merged cells, lists, footnotes, verse, pictures
  inline and floating, the kinds of CSS selectors, 72 nested blocks, right-to-left text). They are
  generated, so they are free to share. The other books of the bench list (`ALLBOOKS` in
  `bench2.sh`: public domain books and the books of the owner) are not in the repository: put your
  own into `dev/bench/books/<any>/` or run the bench with the lab books only.
- `timeit.sh`, `prepare_test.sh`, `xdxf_test.sh`, `exp_elemfaces.sh`: narrower checks named in
  `CHANGELOG.md` next to the change they proved.

The device is 30 to 50 times slower than the PC of the bench (an image drawn in 10 ms on the PC took
285 ms on InkPad 4; a layout of 54 ms took 2.7 s).

## Rules of the owner

- He writes and reads Russian; code, comments, commit messages, changelog and this folder are English.
- Commit, push, tag and release only on his word, each time. A commit is one dense subject line
  ending with the version, no body.
- A device package is built and put on the reader only on his word, and only when everything he
  listed is done: every plug cycle wears the USB socket. No closing list of "not done yet".
- A feature comes with its strings in `cr3gui/po`, its section in the three manuals
  (`data/common/system/share/cr3/manual`), and its changelog entry.
- No diagnostic text on the screens of the program; diagnostics go to the log.
- Speed matters more than the beauty of the code: measure, keep what was computed, do heavy work
  ahead in the pause, avoid copies and virtual calls in pixel loops. After a feature works and is
  checked, a pass of optimisation with numbers from the bench.
- Settings: a group is a folder of the settings menu; a setting with two or three values is
  switched by a tap in place, a list is opened only for more values.
- Files are edited as files (no inline `sed`/`perl` rewriting); unit tests only when asked.
- Nothing is published to outside services without his word; his books and device logs are private
  and never go into the repository.
