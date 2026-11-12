# Changelog

## 0.0.6-17-66-opds (not released)

Speed review. Test builds: `0.0.6-17-66-opds-perf0` (stage timing only), `-beta1` (timing and the changes outside of the engine) `-beta2` (the engine changes), `-beta3` (the direct copy to the screen) and `-beta4` (all changes below).

Measured on InkPad 4 (743G), median of the page turns in the log: 145 ms with `-beta2` (text 43, `Stretch()` 88), 65 ms with `-beta3` (text 43, copy to the canvas 7.5).

The engine changes were checked on a PC: the engine was built for Linux before and after them, and 720 pages of three books in four configurations came out the same bit for bit, together with the position found for a tap and the text of the page.

### Changed

- **Skin caches** (`crengine/src/crskin.cpp`): 64 images and 32 skin items instead of 8. The touch menu draws 9 icons, so with 8 places every icon was read from the skin archive and decoded from PNG again each time the menu opened.
- **Reading statistics** are counted when the page has gone to the screen (`CRPocketBookScreen::flush`), not before it: the word count and the write to `cr3stats.txt` no longer delay the page. The records are the same.
- **Document buffers:** `DOC_BUFFER_SIZE` of the pro5 build is `0xA00000` (10 MB, the default of the engine) instead of 4 MB, so fewer chunks of a big book are packed and unpacked while it is read. Cache files stay compatible.
- **Glyph cache** of the pro5 build is 2 MB instead of 128 KB (`crengine/include/crsetup.h`).
- **Log:** without `crlog.ini` no log is created (it was written to `/dev/null` at the INFO level). `crengine.log.level` is read in any letter case.
- A tap looks for the text node under the finger only for the dictionary, a link and a long tap.
- The standby timer asks for the touch panel once, not on every event.
- **Page buffer copy** (`LVGrayDrawBuf::DrawTo`, `crengine/src/lvdrawbuf.cpp`): a row of a byte-per-pixel buffer is copied at once inside the clip rectangle. It was a loop with two clip tests for every pixel, run over the whole page twice at every page turn.
- **Text drawing** (`LVFreeTypeFace::DrawTextString`, `crengine/src/lvfntman.cpp`): the glyph index is looked up only when kerning is on; the second lookup of the glyph cache, whose result was thrown away, is removed.
- **Glyph cache of a font** (`LVFontLocalGlyphCache`): 256 short lists chosen by the character code instead of one list of all glyphs.
- **Page drawing** (`DrawDocument`, `crengine/src/lvrend.cpp`): a block outside of the page is skipped before its font and style are asked for. A chapter of thousands of paragraphs has them all as children of one block.
- **Cache of formatted paragraphs:** 128 instead of 32 (`crengine/src/lvtinydom.cpp`).
- **Glyph metrics** (`LVFreeTypeFace::getGlyphInfo`): the result is kept by the character code. The layout of italic text asked for the metrics of the same glyphs at every line start and end, and each request loaded and hinted the glyph again. The layout of a book with italics is about a third faster on the PC bench.
- **Page to the screen** (`CRPocketBookScreen::draw`, `cr3gui/src/cr3pocketbook.cpp`): the rows of the page are written to the screen canvas of the firmware directly, instead of the firmware call `Stretch()`. On InkPad 4 that call took 88 ms of the 145 ms a page turn is processed, twice the time of drawing the text.
  - The direct way is taken only when the canvas has one byte per pixel and the size of the page, and only after it has given the same bytes as `Stretch()` twice on this canvas. A canvas of another kind (a color screen, another firmware, an orientation the firmware turns itself) or one different byte leaves `Stretch()`, as before.
  - `cr3.pocketbook.direct.blit=0` in `cr3.ini` turns the direct way off.
  - The touch menu moves the page up on the canvas the same way, after one check against `BitmapFromScreen()` + `DrawBitmap()`, which took 90 ms of the 135 ms the menu needs to open.
  - The last pixel column of the screen is drawn now: `Draw4Bits()` left it black on a screen of an even width.
- **Background writing of the cache file** of a new book: slices of 150 ms with pauses of 450 ms instead of 500 and 1500. The share of time is the same, a tap waits a third of the former time at most.

### Fixed

- **A book downloaded from a catalog was missing on the home screen** right after exit, until the library was visited. The firmware adds a book to its list of the last opened books (`system/state/lastopen.txt`) only when it starts the reader for that book. CoolReader calls `AddLastOpen` and `FlushLastOpen` now when it opens a downloaded book itself (looked up at run time).
- `LVFreeTypeFace::measureText` wrote the flags of all characters after the fit point to one cell of the output array. No caller reads those cells today, so nothing changes on the screen.

### Added

- **Stage timing in the log** (`perf:` lines of the INFO level, written only with `crlog.ini`):
  - one line per event that reached the screen: page draw, statistics, battery, buffer compare, copy to the screen, screen update call;
  - the stages of the start: fonts, skin, settings, history, book;
  - book parsing and layout, every slice of the background cache writing;
  - the touch menu.

## 0.0.6-17-65-opds — 2026-11-10

Tested on InkPad 4 (743G) with firmware 6.11.1683, as the beta builds `0.0.6-17-65-opds-beta1` to `-beta7`.

### Added

- **StarDict dictionaries,** read by CoolReader itself (`cr3gui/src/pb_stardict.cpp`):
  - files: `.ifo`, `.idx` or `.idx.gz`, `.dict` or `.dict.dz` (dictzip with random access), `.syn`;
  - word forms from `.syn`: *домами* finds *дом*;
  - articles in plain text, HTML, Pango and XDXF markup, with bold, italic and colors kept;
  - a word that is not in the dictionary opens the list of the nearest words, as with `.dic`;
  - dictionaries are looked for in `system/dictionaries` (internal storage and SD card) and in its subfolders, at most 200;
  - an index is loaded on the first lookup and stays in memory until exit.
- **XDXF dictionaries** (`cr3gui/src/pb_xdxf.cpp`): plain `.xdxf` files of the XDXF 1 (visual) and XDXF 2 (logical) formats, in the same folders as the StarDict ones.
  - The dictionary name comes from `<full_name>`, `<full_title>` or `<title>`, otherwise from the file name (from the folder name for `dict.xdxf`).
  - Every `<k>` of an article is a key of it; a key with `<opt>` is found with and without the optional part.
  - The index is built on the first lookup and stays in memory; articles are read from the file when shown.
- **One list of dictionaries** for all kinds (`cr3gui/src/pb_dict.cpp`). StarDict and XDXF dictionaries have their type after the name, the PocketBook `.dic` ones have no mark. The chosen dictionary is saved as `dic:<name>`, `sd:<path>` or `xd:<path>`; a plain name saved by older versions still works.
- **Selected dictionaries:** the first row of the list of dictionaries.
  - A tap on a word and the search look the word up in several dictionaries of any kind.
  - The dictionaries are checked under *Check dictionaries…*, up to 5. With none checked, nothing is looked up and the window says that no dictionary is selected.
  - The articles of different dictionaries are separated by a line (`hr` in `dict.css`); each has a small subtitle with the dictionary name. A word found through a word form also names the dictionary word.
  - When no dictionary has the word, the list of the nearest words comes from the dictionary whose nearest word shares the longest beginning with it.
- **Reading statistics** (`cr3gui/src/pb_stats.cpp`, `pb_statsview.cpp`), opened from the new chart icon of the touch quick menu (pb740fw5, pb631fw5, pb626fw5) or from *Settings → Reading statistics*:
  - every page shown for 5 seconds or longer is written to `system/state/cr3/cr3stats.txt`, at most 2 minutes per page; standby and exit stop the clock;
  - book report: reading started and finished, reading days, pages read of the total, total time, estimated time to finish, average time per page and per day, time of each reading day;
  - period reports for today, this week, this month, this year, all time or dates typed in: pages and words with averages, books read and read to the end, total time, time by days and by books;
  - all books with their reading time.
- **Keyboard of CoolReader for the dictionary search** (`cr3gui/src/pb_keyboard.cpp`), used instead of the firmware keyboard on touch devices:
  - English, Russian and Ukrainian layouts (the layout key switches them, the last one is remembered) and a page of digits and signs;
  - Shift, space, backspace, Cancel and OK; the word of the dictionary window is selected in the field and the first letter replaces it;
  - it follows the night mode of CoolReader; the firmware keyboard keeps its colors;
  - devices without a touch screen keep the firmware keyboard, whatever the setting says.
- **Dictionary settings** (*Settings → Dictionary settings*):
  - *Dictionary window*: away from the selected word, at the top or at the bottom (moved here from the display settings);
  - *Word search*: *With nearest words* (as before: a word that is not in the dictionary opens the list of the nearest words) or *Exact* (only the article of the word itself, in any letter case or through a StarDict word form; otherwise "word not found");
  - *Search keyboard*: the keyboard of CoolReader with the night mode or the firmware keyboard; shown on touch devices only.
- **Several OPDS catalogs** (*Settings → OPDS catalogs*):
  - Flibusta is always the first catalog: it cannot be renamed or deleted, its address can be changed (a mirror);
  - other catalogs are added with an address and a name, then renamed, readdressed or deleted on their card;
  - one catalog can be the default one: the catalog icon of the touch menu opens it, without a default catalog the icon shows the list;
  - *Other catalog…* in the catalog menu switches catalogs;
  - the catalogs are kept by number, `cr3.pocketbook.opds.catalog.<N>.name` / `.url` (0 is Flibusta) and `cr3.pocketbook.opds.default`; a catalog address set in an earlier version becomes a catalog of the list, or the Flibusta address when it is a Flibusta mirror.
- **Auto rotation by the G-sensor** for *Control settings → Rotate → 360°*. Firmware 6.11 sends no orientation events to CoolReader, so it asks for the orientation while the book is on the screen (twice a second on firmware 6.x, once a second from the raw sensor values): when the reader stands on a side edge (lying on one's side), the page turns by 90 degrees. Only CoolReader is rotated, the system orientation stays as it is.
  - Firmware 6.x: the orientation of the firmware (`GetGSensorOrientation`, looked up in `libinkview` at start; `ReadGSensor` returns -1 there). If the firmware sensor is off, CoolReader turns it on while it runs and off again at exit.
  - Earlier firmware: the raw sensor values (`ReadGSensor`).
  - Polling stops when the firmware sends orientation events itself.
- **Page numbers in the touch TOC:** *Display settings → Page numbers in TOC*. The classic *Chapter ……… 98* look, without separator lines between the rows.
- **Article pages in the dictionary window:**
  - a vertical swipe turns the page of the article or of the word list;
  - the title shows the page, for example *1/4*, when the article is longer than the window.
- **Ukrainian translation** of the interface and of the manual.
- **More FW5 touch skins** in the package: pb618fw5 (758×1024), pb700fw5 (1264×1680) and pbXfw5 (1404×1872, smaller fonts than pb740fw5). The pb626fw5 and pb631fw5 skins are replaced with their newer versions (pb631fw5 is now drawn for 1072×1448, with the nine-button cite toolbar). All of them have what pb740fw5 got in this version:
  - the catalog and statistics icons of the touch menu, drawn in the style of the skin (gray circle, plain, or black circle or square when pressed);
  - the `touch-toc-dots` menu for the TOC with page numbers and the plain lists;
  - dictionary window and word list titles tall enough for the larger toolbar buttons.
- **Default skin by screen size** when none is chosen: pb740fw5 from 1872 px, pb700fw5 from 1680 px, pb631fw5 above 1024 px, pb626fw5 above 800 px. pb618fw5 also uses the touch quick menu and the touch TOC.

### Changed

- **Old TOC mode** (*Use new TOC menu* off) is a plain touch list of chapters with indents and page numbers, which works with every skin. The firmware contents window used before crashes on firmware 6.11.
- **Rotate** is *360°* by default for new settings.
- **OPDS book description:** no separator lines between its rows.
- **Statistics screens:** the labels and the values are drawn in one font on one baseline (labels in gray); the report has no separator lines between rows, sections start with a line; the chart icon has the gray circle of the other icons of the row (pb740fw5, pb631fw5).
- **Navigation of the catalog and statistics screens:** the arrow at the caption and the Back key go one level up (from the catalog start page and the statistics menu, back to the book); the first row of every screen names where it goes back to; *Close catalog* is now *Back to the book*.
- **Dictionary window:**
  - its place is a setting, *Dictionary settings → Dictionary window*: away from the selected word (as before), at the top or at the bottom;
  - a vertical swipe no longer selects a word under the finger, so the window does not jump to the other half of the screen;
  - it follows the night mode (inverted display);
  - page keys and a horizontal swipe over the text turn the book page, the dictionary stays open;
  - the toolbar buttons are 1.5 times the image size, with taller titles in the FW5 touch skins;
  - a pressed button is drawn inverted until the finger is lifted, and the key cursor is not shown after a touch;
  - the word list shows no HTML tags or character entities;
  - `<br>` and `<font color>` in `.dic` articles are kept as line breaks and colors;
  - Lingvo DSL stress marks left in converted `.dic` articles (`прибл[']и[/']зно`, also with the apostrophe written as `&#x27;`) are removed from the article and the word list.
- **OPDS catalog:** its full-screen touch lists moved to `cr3gui/src/opds/opdsmenu.cpp` to be shared with the statistics screens and the contents. The unused `readingStats` stub is removed.
- **Package:** no `system/config` folder, so file associations on the device are kept.
- **Build:** `CMAKE_POLICY_DEFAULT_CMP0065=NEW` in `make.sh`. With CMake 3.4 and later the old policy linked the binary with `-rdynamic`; its exported symbols replaced those of `libinkview`, and the reader crashed at start. `gettext` is listed in the build dependencies.

### Fixed

- The old TOC mode crashed the reader on firmware 6.11.
- A dictionary chosen in the list of dictionaries was not applied when more than one dictionary was installed.
- Word list rows kept pointers into the firmware dictionary memory; they now keep copies.

### Known limits

- Firmware 6.11 keeps its own dictionaries in `/mnt/secure`, out of reach of other applications, and no longer has `OpenDictionaryView`. CoolReader reads `.dic` files from `system/dictionaries`, StarDict and XDXF dictionaries.
- The firmware on-screen keyboard (search in the book, OPDS, devices without a touch screen) keeps its colors in the night mode of CoolReader.
- XDXF dictionaries are read only as plain `.xdxf` files, not compressed.

## 0.0.6-17-64-opds — 2026-11-02

### Fixed

- **Crash on opening the catalog with skins without a touch TOC** (`default`, `pb62x`). The catalog used the `touch-toc` menu skin, which only the FW5 touch skins have. It now takes the first of `touch-toc`, `settings` and `main` that the current skin defines.
- **Book description flashed and disappeared.** It was a firmware dialog that the reader page repainted over as soon as the catalog closed. The description is now a catalog screen.

### Added

- **Book description screen:**
  - title and author in bold;
  - the book data with bold labels: genres from the catalog, and the *Year*, *Format*, *Language*, *Size*, *Series* lines that Flibusta appends;
  - the description in paragraphs.

  A tap returns to the book screen.
- **Downloaded books appear in the PocketBook library at once.** After a download the reader starts the firmware scanner (`/ebrmain/bin/scanner.app`) the way [inkshelf](https://github.com/N-Combinator/inkshelf) does it:
  - from a detached supervisor process that outlives the reader;
  - the scanner is stopped when it reports *Scan finished*, or after two minutes.

  Firmware without the scanner lists the book after its next scan, as before. `BookPreparing`/`BookReady` did not add the file on InkPad 4 with firmware 6.11.1683.
- **Genres** (`category`) in the OPDS parser.

## 0.0.6-17-63-opds — 2026-10-26

Based on blchinezu/pocketbook-coolreader 0.0.6-17-62.1.

### Added

- **OPDS catalog browser** (`cr3gui/src/opds`). Default catalog: Flibusta.
  - Opened from the touch quick menu icon or from *Settings → OPDS catalog*.
  - Catalog sections, search, two-line book lists with a pager row (*Back*, page number with the catalog menu, *Next*).
  - Next catalog pages are loaded on demand.
  - Book screen: description, *Download and open* in every format CoolReader reads (FB2.ZIP preferred), links to related books.
  - Books are downloaded with libcurl into `/mnt/ext1/Books/Flibusta`, checked (HTML error pages are rejected) and opened at once. The position of the current book is saved first.
  - Wi-Fi is connected on demand through the firmware.
  - The on-screen keyboard is opened from a placeholder window, so the reader stays in front after input.
- **Settings:**
  - *OPDS catalog* and *OPDS catalog address* (`cr3.pocketbook.opds.url`);
  - the download folder (`cr3.pocketbook.opds.dir`), set in `cr3.ini`.
- **Catalog icon** in the touch menu of the pb740fw5, pb631fw5 and pb626fw5 skins.
- **pb740fw5 skin** for 1404×1872 screens (from the 0.0.6-17-62.2 community build).
- **Cite toolbar:**
  - *Up* / *Down* buttons in skins with nine cite buttons;
  - all buttons work by touch.
- Page margin value 350.
- Host-side parser test with saved Flibusta feeds (`cr3gui/src/opds/test`).

### Changed

- **Firmware 6.11 support:**
  - libpng and libjpeg are linked statically from `thirdparty/` and are not exported (`-Wl,--exclude-libs,ALL`);
  - freetype is built without bzip2, png and harfbuzz;
  - the binary no longer needs `libpng12.so.0`, `libjpeg.so.8` or `libbz2.so.1.0`.
- **Launcher:**
  - exports `GLIBC_TUNABLES=glibc.rtld.execstack=2` for the 6.11 executable-stack restriction;
  - a syntax error in its crash-detection block is fixed.
- **Touch skins.** The touch quick menu and the new TOC are enabled for the FW5+ skins pb740fw5, pb700fw5, pbXfw5 and pb617fw5, in addition to pb626fw5 and pb631fw5.
- **High-density screens** (long side ≥ 1400 px): settings menu fonts, the touch menu page counter and the battery icon are scaled by 1.5.
- **Dictionary:**
  - touch selection picks the tapped entry;
  - the number of lines fits the window;
  - the exact-match lookup is disabled.
- **Network requests** (`web.cpp`):
  - connect and transfer timeouts;
  - HTTP status and curl errors are logged;
  - no signals are used.
- **Data files** from the community build: `keymaps.ini` (pro5), `dict.css` and the Russian manual.
- The *OTA update* settings item is hidden. It would replace this build with the upstream release.
- Russian translation of all new strings.
