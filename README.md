# CoolReader 3 for PocketBook — OPDS edition

[![Latest release](https://img.shields.io/github/v/release/pozhydaiev7/pocketbook-coolreader-opds)](https://github.com/pozhydaiev7/pocketbook-coolreader-opds/releases/latest)
[![License: GPL v2](https://img.shields.io/badge/license-GPL%20v2-blue.svg)](LICENSE)

A fork of [blchinezu/pocketbook-coolreader](https://github.com/blchinezu/pocketbook-coolreader) (0.0.6-17-62.1)
for PocketBook readers on firmware 5.x and 6.x, **firmware 6.11** included. It is built on **CREngine 3.2.59**,
the current engine of CoolReader, and adds:

- a built-in **OPDS catalog browser**;
- **StarDict and XDXF dictionaries**, several dictionaries looked up at once;
- **reading statistics**;
- **updates over the network**, from the releases of this repository;
- **PDF and DjVu** (since 0.0.8), as the file has them or as a book in your font;
- **reading position sync** with KOReader (since 0.0.8);
- citations selected by touch, auto rotation, six touch skins and the changes of the community build
  0.0.6-17-62.2 by sergkv12 ([4pda](https://4pda.to/forum/index.php?showtopic=886480&st=2420#entry76669650)).

Tested on **PocketBook 743G (InkPad 4)** with firmware **U743g 6.11.1683**.

--------------------------------------------------------------------------------

## What's new in this fork

### CREngine 3.2.59

Since 0.0.7-opds the program is built on CREngine 3.2.59-1 (official CoolReader, 2026) with the PocketBook port
of [dderugin/pocketbook-coolreader](https://github.com/dderugin/pocketbook-coolreader), instead of CREngine 3.1.2 of 2014.

- **Page layout by CSS:** tables, floats, borders and margins of a book are drawn as its stylesheet asks.
- **More formats:** DOCX, ODT and FB3 books.
- **Font weight** from 300 to 900, *Letter spacing in justified lines*, hyphenation dictionaries in the format of the new engine.
- **Speed:**
  - the page is written straight to the screen canvas of the firmware instead of the firmware call `Stretch()`, which took 88 ms of the 145 ms of a page turn on InkPad 4;
  - the touch menu opens in 45–56 ms instead of 132–220 ms;
  - glyph, skin and paragraph caches, faster parsing of EPUB, a faster start;
  - since 0.0.7.1: the next page is drawn in memory while a page is read, so a page turn only shows it (*Display settings → Prepare next page*, on by default).
- **Wait icon** while a big book is laid out again; taps made meanwhile are dropped.
- Reading positions and bookmarks of the earlier versions are converted. A book is laid out again at its first opening: the cache files of the old engine are not used.

### PDF and DjVu

Since 0.0.8-opds.

- **The file as it is.** A PDF or a DjVu, also one inside a zip, is shown page by page as the file has it: the view for scanned and photographed books. You choose the scale (whole page, page width, the content without the margins, or automatic, where every page is levelled by itself, which helps with crooked scans) and the way the pages are turned (a page at once, by frames, or as a ribbon).
- **The file as a book.** A PDF with text and a DjVu with a text layer open as an ordinary book: in your font and size, with hyphenation, the dictionary, citations, bookmarks and search. Headings, the contents, tables, pictures and footnotes are carried over; running titles and page numbers are left out. The book is made when the file is opened for the first time, which takes a while for a big file; after that it opens at once.
- **From one view to the other.** The first item of the main menu, *View as the original* or *View as a book*, changes the view and keeps your place. The same can be put on a tap zone.
- **Everything a book has.** In the original view the tap zones, the touch menu, the status line, the contents, bookmarks, *Go to page* and the dictionary (tap a word of the page) work as in a book. The main menu has the same rows; those that cannot work over a picture of a page (citations, for example) are gray.
- **Search over the pages** (since 0.0.8.1). A PDF with text and a DjVu with a text layer are searched in the original view too: the places found are shown on the page, and a panel under it leads to the previous and to the next one.
- **An orientation of their own.** *Settings → PDF and DjVu settings → Orientation of the pages*: for example scans in landscape while books stay upright.
- *Settings → File associations* gives any kind of file back to the reading program of the firmware.

### Reading position sync

Since 0.0.8-opds.

- The place where you stopped is kept on a KOReader sync server and taken by your other devices: KOReader on a phone, a computer or another reader, or CoolReader on another PocketBook. The file must be the same on all of them.
- *Settings → Reading position sync settings*: the switch (off by default) and *Server and login...*. The server of KOReader is set from the start; a server of your own works as well (Calibre-Web-Automated, Kavita, korrosync). You can register right from the reader.
- The place is sent when you close the book or leave the program and asked for when you open it. A newer place from another device is offered with a question. Without Wi-Fi the place is kept and sent later.

### Contents and main menu

Since 0.0.8-opds.

- **Levels that fold.** A long contents list opens with its first level and the branch you are reading; a tap on the triangle opens or closes a branch. *Display settings → Contents: levels closed* shows every level as before. A book without a contents list gets one made of its headings. *End* is the last entry of every contents list.
- **Main menu of your own.** *Control settings → Main menu*: choose what each of its items does.
- **OPDS catalogs with a login.** A catalog closed by a password (a Calibre library, for example) opens with the login and the password set in its card.

### Program update

*Settings → Program update* installs a newer version from the [releases](https://github.com/pozhydaiev7/pocketbook-coolreader-opds/releases) of this repository.

- **Check for updates** asks GitHub now. **Check automatically** (on by default) does it at every start of the program, only when Wi-Fi is connected already; a version answered with *Not now* is not announced again.
- **The card of a version** shows its date, size and release notes in the language of the interface. The download starts only after a *Yes*.
- **Choose a version:** any published version can be installed, an older one too.
- **Test versions:** a switch that also offers alpha and beta versions (pre-releases).
- **Checks:** the package is checked by its size, SHA-256 and contents before it is installed.
- **Your files are kept:** settings, reading history and statistics are never touched; the book cache, your fonts and every file you added to `system/share/cr3` are carried over.
- **Rollback:** when the new version does not start, the launcher brings the previous one back.

Release notes are written in English; a section under the heading `## Русский` or `## Українська` is shown instead to an interface in that language.

### Citations

- **Text selected by touch** in the citation window: drag from a word to a word, tap a word to move a bound of the selection, long tap to select one word.
- **Buttons** *A*, *A+*, *A−*, *C+*, *C−* add or remove a paragraph or a sentence at a tap; the arrows move a bound of the selection.
- **List of citations:** a tap on a citation offers *Go to*, *Edit* and *Delete*; the first row starts a new citation. An edited citation keeps its type, comment and place in the list.

### OPDS catalog browser

Browse an online library, search it, download a book and open it in the reader, without leaving CoolReader.

- **Catalogs:** [Flibusta](https://flibusta.is/opds) is always in the list (its address can be changed to a mirror, it cannot be deleted); any other OPDS catalog can be added with its address and name.
- **How to open it:**
  - the catalog icon in the touch quick menu (tap the centre of the page) opens the default catalog, or the list of catalogs when none is the default;
  - *Settings → OPDS catalog settings* shows the list: open a catalog, add one, or edit them (name, address, default catalog, delete);
  - *Other catalog…* in the catalog menu switches catalogs.
- **Navigation:** the arrow at the caption and the first row of every screen go one level up; from the catalog start page, back to the book.
- **Catalog sections and search.** Search is two-step, as the catalog serves it: first choose authors or books, then the result list.
- **Book lists.** Each book takes two lines, title and author, in a compact font.
  - The screens are turned with the arrows of the title; the next part of the catalog is loaded on demand.
  - The row *Catalog menu* opens the catalog menu: back, search, catalog start page, catalog address, close.
- **Book screen.** It shows:
  - the full title and author; tap it to open the description screen (title, author, genres, year, format, language, size, series and the description);
  - *Download and open* for every format the catalog offers, best first: FB2.ZIP, EPUB, MOBI, RTF, TXT, HTML, DOC, CHM;
  - links to other books by the author and in the series.
- **Downloads.**
  - Books are saved to `/mnt/ext1/Books/Flibusta` and opened right away.
  - The current book's position is saved first.
  - A download that turns out to be an HTML error page is rejected.
  - The book appears in the PocketBook library at once: the reader starts the firmware library scanner in the background. Firmware without the scanner lists it after the next USB connection or reboot.
- **Any skin.** The catalog works with every interface skin, including `default` and `pb62x`.
- **Network.** If Wi-Fi is off, the reader asks the system to connect. Requests time out instead of hanging.
- **Settings in `cr3.ini`:**
  - `cr3.pocketbook.opds.catalog.<N>.name` and `.url` — the catalogs by number, 0 is Flibusta; `cr3.pocketbook.opds.default` — the number of the default catalog plus one, 0 for none;
  - `cr3.pocketbook.opds.dir` — the download folder.

### Firmware 6.11 support

On firmware 6.11 the earlier CoolReader builds do not start: the firmware no longer ships `libpng12.so.0`. This fork changes two things:

- **libpng, libjpeg-turbo, FreeType and zlib are linked statically.** They are built from `thirdparty/` with `-Wl,--exclude-libs,ALL`, so the reader does not export them. The firmware's `libinkview` keeps using its own libraries.
- **The launcher** (`system/bin/cr3-pb.app`) exports `GLIBC_TUNABLES=glibc.rtld.execstack=2`. This is the workaround for the 6.11 rule against system libraries that request an executable stack; KOReader hit the same rule ([koreader#15720](https://github.com/koreader/koreader/issues/15720)). Older firmware ignores the variable.

The binary needs only `libinkview`, `libcurl` and the C/C++ runtime from the firmware, so it also runs on the earlier 6.x firmware.

### StarDict and XDXF dictionaries

Besides the PocketBook `.dic` dictionaries (read by the firmware), CoolReader reads StarDict and XDXF dictionaries itself. All of them are in one list; StarDict and XDXF dictionaries have their type after the name, because the firmware reader cannot use them.

- **Where:** `system/dictionaries` on the reader (or on the SD card), the same folder as the `.dic` files. Unpack each dictionary into a folder of its own there, or put its files straight into `system/dictionaries`.
- **StarDict files:** `*.ifo`, `*.idx` or `*.idx.gz`, `*.dict` or `*.dict.dz`, and optionally `*.syn` (word forms: *домами* finds *дом*).
- **XDXF files:** a plain `*.xdxf` file (XDXF 1 or 2), easy to edit in a text editor. Every `<k>` of an article is a key; the name comes from `<full_name>` or `<full_title>`, otherwise from the file or folder name.
- **Articles:** plain text, HTML, Pango and XDXF markup; bold, italic and colors are kept.
- **Limits:**
  - at most 200 StarDict and XDXF dictionaries are looked for;
  - the first lookup in a dictionary loads its index into memory, which stays there until CoolReader exits.
- **Dictionary settings** (*Settings → Dictionary settings*):
  - *Dictionary window*: away from the selected word, at the top or at the bottom of the screen;
  - *Word search*: *With nearest words* (a word that is not in the dictionary opens the list of the nearest words) or *Exact* (only the article of the word itself, otherwise "word not found");
  - *Search keyboard* (touch devices only): the keyboard of CoolReader (the English, Russian and Ukrainian layouts of the firmware keyboard), which follows the night mode, or the firmware keyboard. Without a touch screen the firmware keyboard is always used.
- **Selected dictionaries:** the first row of the dictionary list. With it, both a tap on a word and the search look the word up in the dictionaries checked under *Check dictionaries…*, up to 5, of any kind; with none checked, nothing is looked up. The articles of different dictionaries are separated by a line, each with a small subtitle naming the dictionary; a word found through a word form also names the dictionary word.

Free StarDict dictionaries: [FreeDict](https://freedict.org/downloads/) (choose the StarDict format), [Wiktionary dictionaries by Vuizur](https://github.com/Vuizur/Wiktionary-Dictionaries).

### Reading statistics

CoolReader records how long every page stays on the screen and shows reports, as the statistics module of KOReader does.

- **How to open it:** the chart icon in the touch quick menu, or *Settings → Reading statistics*.
- **What counts as reading:**
  - a page shown for less than 5 seconds was only turned over and is not counted;
  - more than 2 minutes on one page counts as 2 minutes (the reader went away or the device slept);
  - standby and exit stop the clock.
- **This book:** reading started and finished, reading days, pages read of the total (a font size change is taken into account), total time, estimated time to finish, average time per page and per reading day, reading time of each day.
- **Today, this week (from Monday), this month, this year, all time, or any period** (`dd.mm.yyyy-dd.mm.yyyy`): pages and words read with the averages per reading day and per day of the period, books read, books read to the end, total time, reading time by days and by books.
- **All books:** every book with its total reading time; a tap opens the book report.
- **The log:** `system/state/cr3/cr3stats.txt`, a text file with one line per page. It is kept by updates; delete it to start over.

### Contents and rotation

- **Old contents mode** (*Display settings → Use new TOC menu* off): a plain touch list of chapters with indents and page numbers, for every skin. The firmware contents window used before crashes on firmware 6.11.
- **Auto rotation:** with *Control settings → Rotate → 360°* (the default), CoolReader reads the G-sensor while the book is on the screen and turns the page by 90 degrees when the reader stands on a side edge, for example when reading lying on one's side. Firmware 6.11 does not send rotation events to CoolReader, so it asks for the orientation itself, twice a second; only CoolReader is rotated.

### Skins

The package has the FW5 touch skins pb618fw5 (758×1024), pb626fw5 (758×1024), pb631fw5 (1072×1448), pb700fw5 (1264×1680), pb740fw5 and pbXfw5 (1404×1872; pbXfw5 has smaller fonts), plus the older `default` and `pb62x`. Every FW5 skin has the catalog and statistics icons in its own style, the TOC with page numbers and the larger dictionary toolbar. Without a chosen skin, CoolReader takes one by the screen size.

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

- **Debugging.** Logging is off by default. *Settings → Debugging* turns the log on and off on the device, with the level *Info* or *Error*. The log is `/mnt/ext1/cr3log.txt`; the log of the previous run is kept as `cr3log.prev.txt`. Turning the debugging off deletes both.
- **Font chosen by a sample** (since 0.0.7.1): the lists of the font face, the font size and the font weight have a sample under them, a sentence with every letter of the alphabet drawn with the value tapped. The value chosen is set when the list is left.
- **Text windows** (the manual, *Key functions*, *Book information*): pages are turned by the tap zones and swipes of the book.
- ***Document embedded fonts*** turned off also drops the font family named by the styles of a book, so the text is drawn with the chosen font.
- **Manuals** in Russian, Ukrainian and English.
- Russian and Ukrainian translations for all new strings.

See [CHANGELOG.md](CHANGELOG.md) for the full list.

--------------------------------------------------------------------------------

## Install

The install is by USB:

1. Download `cr3-v<version>-pro5.zip` from the [latest release](https://github.com/pozhydaiev7/pocketbook-coolreader-opds/releases/latest).
2. Connect the reader by USB. The folder `system` of the reader is hidden: turn on the hidden files in the file manager of the PC. If the reader has CoolReader, back up `system/share/cr3` and `system/bin/cr3-pb.app` from it.
3. Copy the folders `system` and `applications` from the archive to the root of the reader's storage, replacing the existing files.
4. Disconnect USB and restart the reader.
5. Tap a book: FB2, EPUB, TXT, RTF and HTML open in CoolReader; since 0.0.8, PDF and DjVu too. The reading program of the firmware stays in the menu of a long tap, *Open with*.
6. If the menu still looks like the old 3×3 grid, choose the skin:
   *Settings → Display settings → Skin → pb740fw5.cr3skin*.

What the archive brings besides the program:

- `system/config/extensions.cfg`, the file associations. CoolReader is not an item of the *Applications* screen: it is a reading program, and the firmware offers it for a book only when this file names it. CoolReader is first in every line. The file replaces the one of the reader at every install. In the archive its folder is spelled `system/Config`; on the reader it is the folder `system/config`.
- `applications/Cool Reader Selector.app`, a switch on the *Applications* screen: a tap puts the file associations aside, so books open in the programs of the firmware; the next tap brings them back.

The archive has nothing else of `system/config`: `cr3.ini` and the other settings are kept.

To roll back, restore the files saved in step 2.

Every later version is installed from the program: *Settings → Program update → Check for updates*. It replaces the file associations too.
Versions older than `0.0.6-17-67-opds-alpha18` have no working update and are replaced by USB.

The package is the `pro5` build, for PocketBook devices on firmware 5.x and 6.x. Only this target is built and tested in this fork.

--------------------------------------------------------------------------------

## Build

The toolchain is [blchinezu/pocketbook-sdk](https://github.com/blchinezu/pocketbook-sdk) (`SDK_481`, gcc 4.8.1). It runs on any x86-64 Linux, including WSL on Windows.

```bash
sudo apt-get update
sudo apt-get install cmake git zip rsync gettext build-essential

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

    cr3gui/src/opds   - OPDS feed parser (opdsfeed.*), catalog browser UI (opdsbrowser.*), touch lists (opdsmenu.*)
    cr3gui/src        - CR3 GUI for e-ink devices, PocketBook frontend in cr3pocketbook.*
    crengine          - CREngine, the DOM/XML/CSS e-book rendering library
    thirdparty        - third-party libraries (libpng, libjpeg-turbo, freetype, zlib, cJSON are built from here)
    tinydict          - .dict dictionary support
    data              - files packaged with the reader (launcher, keymaps, manuals, css)
    tools             - toolchain and configuration files

--------------------------------------------------------------------------------

## Credits

- **CoolReader 3 and CREngine:** Vadim Lopatin ([buggins/coolreader](https://github.com/buggins/coolreader)).
- **PocketBook port of CREngine 3.2.59 and the updated bundled libraries:** Dmitrij Derjugin ([dderugin/pocketbook-coolreader](https://github.com/dderugin/pocketbook-coolreader)).
- **PocketBook port:** pkb ([sourceforge](https://sourceforge.net/p/crengine/crengine/ci/pocketbook-port/tree/)).
- **PocketBook fork 0.0.6-17-62.1:** blchinezu ([blchinezu/pocketbook-coolreader](https://github.com/blchinezu/pocketbook-coolreader)) and its contributors.
- **Community build 0.0.6-17-62.2 and the pb740fw5 skin:** sergkv12 ([4pda](https://4pda.to/forum/index.php?showtopic=886480)).

The upstream feature list, discussion and translation links are in the
[original README](https://github.com/blchinezu/pocketbook-coolreader#readme).

## License

All source code, except the `thirdparty` directory, is provided under the terms of the
GNU GPL, version 2 or (at your option) any later version (see [LICENSE](LICENSE) and the headers
of the source files). The libraries in `thirdparty` keep their own licenses.

Since 0.0.8 the program shows PDF files with [MuPDF](https://mupdf.com/)
(GNU AGPL, version 3) and DjVu files with [DjVuLibre](https://djvu.sourceforge.net/) (GNU GPL,
version 2 or later). Their sources are not in this repository: `dev/pdfdjvu/fetch_sources.sh`
downloads the unchanged release archives by their checksums. A program built with them is
distributed under the terms of the GNU GPL, version 3, together with the GNU AGPL, version 3,
for the part that is MuPDF.

The OPDS browser is a generic client. The default catalog address is only a preset. You are responsible
for complying with the copyright law of your country when you download books.
