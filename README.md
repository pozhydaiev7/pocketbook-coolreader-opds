# CoolReader 3 for PocketBook — OPDS edition

[![Latest release](https://img.shields.io/github/v/release/pozhydaiev7/pocketbook-coolreader-opds)](https://github.com/pozhydaiev7/pocketbook-coolreader-opds/releases/latest)
[![License: GPL v2](https://img.shields.io/badge/license-GPL%20v2-blue.svg)](LICENSE)

A fork of [blchinezu/pocketbook-coolreader](https://github.com/blchinezu/pocketbook-coolreader) (0.0.6-17-62.1)
that adds a built-in **OPDS catalog browser**, runs on **PocketBook firmware 6.11**, and brings in the
changes of the community build 0.0.6-17-62.2 by sergkv12
([4pda](https://4pda.to/forum/index.php?showtopic=886480&st=2420#entry76669650)).

Tested on **PocketBook 743G (InkPad 4)** with firmware **U743g 6.8.4143** and **U743g 6.11.1683**.

--------------------------------------------------------------------------------

## What's new in this fork

### OPDS catalog browser

Browse an online library, search it, download a book and open it in the reader, without leaving CoolReader.

- **Default catalog:** [Flibusta](https://flibusta.is/opds). Any other OPDS catalog works too; the address is a setting.
- **How to open it:**
  - the catalog icon in the touch quick menu (tap the centre of the page);
  - or *Settings → OPDS catalog*.
- **Catalog sections and search.** Search is two-step, as the catalog serves it: first choose authors or books, then the result list.
- **Book lists.** Each book takes two lines, title and author, in a compact font. The bottom row is the pager:
  - *Back* on the left, *Next* on the right; the next catalog page is loaded on demand;
  - the page number in the centre opens the catalog menu: back, search, catalog start page, catalog address, close.
- **Book screen.** It shows:
  - the full title and author; tap it to read the description;
  - *Download and open* for every format the catalog offers, best first: FB2.ZIP, EPUB, MOBI, RTF, TXT, HTML, DOC, CHM;
  - links to other books by the author and in the series.
- **Downloads.**
  - Books are saved to `/mnt/ext1/Books/Flibusta` and opened right away.
  - The current book's position is saved first.
  - A download that turns out to be an HTML error page is rejected.
- **Network.** If Wi-Fi is off, the reader asks the system to connect. Requests time out instead of hanging.
- **Settings in `cr3.ini`:**
  - `cr3.pocketbook.opds.url` — the catalog address; change it in *Settings → OPDS catalog address* or in the catalog menu;
  - `cr3.pocketbook.opds.dir` — the download folder.

### Firmware 6.11 support

On firmware 6.11 the earlier CoolReader builds do not start: the firmware no longer ships `libpng12.so.0`. This fork changes two things:

- **libpng and libjpeg are linked statically.** They are built from `thirdparty/` with `-Wl,--exclude-libs,ALL`, so the reader does not export them. The firmware's `libinkview` keeps using its own libraries.
- **The launcher** (`system/bin/cr3-pb.app`) exports `GLIBC_TUNABLES=glibc.rtld.execstack=2`. This is the workaround for the 6.11 rule against system libraries that request an executable stack; KOReader hit the same rule ([koreader#15720](https://github.com/koreader/koreader/issues/15720)). Older firmware ignores the variable.

The binary still needs only `libinkview`, `libcurl`, `libz` and the C/C++ runtime from the firmware, so it also runs on the earlier 6.x firmware.

### Changes from the 0.0.6-17-62.2 community build

That build was published as a binary only. Its changes are re-implemented here from the description:

- **pb740fw5 skin** for 1404×1872 screens (InkPad 4 and others), with the touch quick menu, the new TOC and partial screen updates.
- **Cite toolbar.** Every button works by touch, and there are new *Up* / *Down* buttons.
- **Dictionary:**
  - tapping a word in the list selects the right entry;
  - the exact-match lookup is disabled;
  - the list shows as many lines as fit in the window.
- **Bigger elements on high-density screens:** settings menu fonts, the page counter of the touch menu and the battery icon.
- **Page margins** up to 350.
- **Data files** from that build: `keymaps.ini`, `dict.css` and the Russian manual.

### Other changes

- The *OTA update* item is hidden. It would replace this build with the upstream one.
- Logging is off by default. To turn it on, create `system/share/cr3/crlog.ini`:

  ```ini
  crengine.log.filename=/mnt/ext1/crengine.log.txt
  crengine.log.autoflush=true
  crengine.log.level=INFO
  ```

- Russian translation for all new strings.

See [CHANGELOG.md](CHANGELOG.md) for the full list.

--------------------------------------------------------------------------------

## Install

1. Download `cr3-v0.0.6-17-63-opds-pro5.zip` from the [latest release](https://github.com/pozhydaiev7/pocketbook-coolreader-opds/releases/latest).
2. Connect the reader by USB. Back up `system/share/cr3` and `system/bin/cr3-pb.app` from it.
3. Copy the `system` folder from the archive to the root of the reader's storage, replacing the existing files.

   The archive has no `system/config` folder, so your `cr3.ini` and file associations are kept.
4. Disconnect USB and open any book in CoolReader.
5. If the menu still looks like the old 3×3 grid, choose the skin:
   *Settings → Display settings → Skin → pb740fw5.cr3skin*.

To roll back, restore the files saved in step 2.

The package is the `pro5` build, for PocketBook devices on firmware 5.x and 6.x. Only this target is built and tested in this fork.

--------------------------------------------------------------------------------

## Build

The toolchain is [blchinezu/pocketbook-sdk](https://github.com/blchinezu/pocketbook-sdk) (`SDK_481`, gcc 4.8.1). It runs on any x86-64 Linux, including WSL on Windows.

```bash
sudo apt-get update
sudo apt-get install cmake git zip rsync

git clone https://github.com/blchinezu/pocketbook-sdk ~/PBDEV
mkdir -p ~/PBDEV/sources ~/PBDEV/releases/coolreader3/dev

# do not symlink this directory, cmake fails on symlinks
git clone https://github.com/pozhydaiev7/pocketbook-coolreader-opds.git ~/PBDEV/sources/cr3-fork

cd ~/PBDEV/sources/cr3-fork
# needed with CMake 4.x: the project's cmake_minimum_required is older than 3.5
export CMAKE_POLICY_VERSION_MINIMUM=3.5
bash make.sh pro5

# the package is created in ~/PBDEV/releases/coolreader3/dev/
```

CMake does not track skin files. After changing a skin, delete the `pbpro5` folder and build again.

### OPDS parser test

The catalog parser builds on the host and comes with saved Flibusta feeds:

```bash
cd cr3gui/src/opds/test
g++ -std=c++11 -Wall -I.. opdsfeed_test.cpp ../opdsfeed.cpp -o opdsfeed_test && ./opdsfeed_test
```

--------------------------------------------------------------------------------

## Source layout

    cr3gui/src/opds   - OPDS feed parser (opdsfeed.*) and catalog browser UI (opdsbrowser.*)
    cr3gui/src        - CR3 GUI for e-ink devices, PocketBook frontend in cr3pocketbook.*
    crengine          - CREngine, the DOM/XML/CSS e-book rendering library
    thirdparty        - third-party libraries (libpng, libjpeg, freetype, ... are built from here)
    tinydict          - .dict dictionary support
    data              - files packaged with the reader (launcher, keymaps, manuals, css)
    tools             - toolchain and configuration files

--------------------------------------------------------------------------------

## Credits

- **CoolReader 3 and CREngine:** Vadim Lopatin ([buggins/coolreader](https://github.com/buggins/coolreader)).
- **PocketBook port:** pkb ([sourceforge](https://sourceforge.net/p/crengine/crengine/ci/pocketbook-port/tree/)).
- **PocketBook fork 0.0.6-17-62.1:** blchinezu ([blchinezu/pocketbook-coolreader](https://github.com/blchinezu/pocketbook-coolreader)) and its contributors.
- **Community build 0.0.6-17-62.2 and the pb740fw5 skin:** sergkv12 ([4pda](https://4pda.to/forum/index.php?showtopic=886480)).

The upstream feature list, discussion and translation links are in the
[original README](https://github.com/blchinezu/pocketbook-coolreader#readme).

## License

All source code, except the `thirdparty` directory, is provided under the terms of the
GNU GPL, version 2 (see [LICENSE](LICENSE)). The libraries in `thirdparty` keep their own licenses.

The OPDS browser is a generic client. The default catalog address is only a preset. You are responsible
for complying with the copyright law of your country when you download books.
