# PDF and DjVu: state of the work

Branch `pdf-djvu`, versions `0.0.8-opds-alphaN`. The plan the owner approved on 2026-11-18 is
`PLAN.ru.md` (Russian, as he reads it). This file is the short English state: what is decided,
what is measured, what comes next. Update it at the end of every working session.

## Decided by the owner

- PDF by **MuPDF** (AGPL-3), DjVu by **DjVuLibre** (GPL-2+). Our code is GPL-2 or later; the built
  program of this line is under GPL-3/AGPL-3, the licence line of `README.md` changes when the
  libraries are linked in.
- **Text mode is a translator, not a copy of the layout.** A file with text (a text PDF, a DjVu
  with a text layer) becomes book markup with the element set of EPUB (headings, paragraphs,
  emphasis, lists, tables, pictures, footnotes) and is shown by the styles and settings of the
  reader, as FB2 and EPUB are. Fonts, sizes, columns and positions of the original are dropped:
  the screens are 5 to 6 inches. What is not understood with confidence (a complex table, a
  formula) goes in as a picture cut from the page.
- **Picture mode** (scans) has two independent settings. Scale: whole page, page width, content
  (margins cropped), own frame. Leafing: "next page at once" (the same frame on the next page) or
  "by frames" (the classic way: the frames of a page until it ends, then the next page). The own
  frame holds scale, shift and **rotation** (fine steps to level a crooked scan, and 90° turns),
  separately for odd and even pages (the margins of a photographed book are mirrored), and is
  remembered for the file.
- **Settings**: two folders in the root of the settings menu, *Настройки просмотра PDF* and
  *Настройки просмотра DjVu*, in the design of the other folders (`CRSettingsGroupMenu`): a setting
  with up to three values is switched by a tap in place. No font face, size or weight there: text
  uses the common settings. The folders hold what is special, mostly for picture books.
- Order: stage 0 feasibility, stage 1 picture mode for both formats, stage 2 PDF as a book,
  stage 3 DjVu text, dictionary and citations by the text layer on a picture page, search. After
  the features work and are checked: a pass of optimisation with numbers from the PC bench.
- Speed matters more than the beauty of the code. A page is drawn by the library straight into
  the gray buffer of the final size; what is computed is kept; the next page is prepared in the
  pause, as it is for books.

## Stage 0: done on 2026-11-18

Scripts of this folder, run inside WSL: `fetch_sources.sh`, `build_mupdf.sh arm|host|gen`,
`build_djvu.sh arm|host`, `link_probe.sh arm|host`, `link_djvuprobe.sh arm|host`,
`make_samples.sh`. Sources and builds lie in `~/PBDEV/pdfdjvu`.

- **Both libraries build with the toolchain of the reader with no patch**: MuPDF 1.28.6 (the
  version KOReader ships) and DjVuLibre 3.5.29, gcc 4.8.1, glibc 2.9, 0 errors.
  - MuPDF needs `-std=gnu99` (the compiler defaults to C90: `NAN`, `INFINITY`, `isfinite`).
  - glibc 2.9 has no `getentropy`; the program supplies one (`pdfprobe.c`, reads `/dev/urandom`).
  - MuPDF options: `html=no xps=no svg=no extract=no mujs=no brotli=no tesseract=no barcode=no
    tofu=yes tofu_cjk=yes`, no viewers, no curl. Its ARM assembly for image scaling is on.
- **Size**, stripped ARM probes: MuPDF 4.06 MB with its own FreeType, zlib and libjpeg (the
  program has these already, so less is added), DjVuLibre 0.72 MB. Both need glibc 2.7 at most.
- **Times on the PC**, a page drawn 1404 pixels wide (the reader is 30 to 50 times slower):

  | Page | Draw | Text |
  |---|---|---|
  | text PDF | 0.3–5 ms | 0.3–2.7 ms with segmentation, paragraphs and table hunt |
  | JPEG scan 3000 × 4000 in a PDF | 50–70 ms | |
  | DjVu photo page (IW44) 3000 × 4000 | decode 110–119 ms, draw 20–29 ms | |
  | DjVu black and white (JB2) 3000 × 4000 | decode 21 ms, draw 14–16 ms | |
  | DjVu text page 1750 × 2480 | decode 2.5 ms, draw 8–12 ms | 0.3–0.5 ms, layer of 120–260 words |

  So a photographed page takes seconds on a reader: preparing the next page ahead is a must, and
  the decoded page has to be kept while its frames are shown.
- **What the structured text of MuPDF gives** (`FZ_STEXT_SEGMENT | PARAGRAPH_BREAK | TABLE_HUNT |
  COLLECT_STRUCTURE | PRESERVE_IMAGES | DEHYPHENATE`, printed by `fz_print_stext_page_as_xhtml`) on
  `samples/diag-elements.pdf`: headings as `h2`/`h3`, paragraphs, bold and italic, and a real
  `<table>` with `colspan` for the ruled table with merged cells. The table without rules comes
  as paragraphs. The running title and the page number stay in the text: removing what repeats
  from page to page is ours to do.

## Sample files (`samples/`, made by `make_samples.sh`)

| File | What |
|---|---|
| `diag-elements.pdf` | text PDF with every element: headings of three levels, paragraphs, bold, italic, underline, monospace, superscript and subscript, link, epigraph, quotation, lists, verse, code, ruled table, table without rules, table with merged cells, pictures with captions, formulas, footnotes, two columns, small print, running title, page numbers, contents of two levels |
| `diag-elements.djvu` | the same pages as a 300 dpi DjVu with the hidden text layer (page, column, region, para, line, word); the page with pictures is a photo page, the others are black and white |
| `diag-book.pdf` | plain book text, the source of the photographed pages |
| `diag-scan-photo.pdf` | the book photographed with a camera: JPEG frames of 3000 × 4000, the page in the middle with wide margins, turned by about a degree, the spine left and right in turn |
| `diag-scan-photo.djvu` | the same frames as DjVu photo pages |
| `diag-scan-bw.djvu` | the same frames in black and white |

They are generated: laid out by the HTML engine of MuPDF from the text in `make_samples.py`,
"photographed" by composing the page picture over a grainy table with uneven light, encoded by the
tools of DjVuLibre. No library of Python is needed. `PAGES=8 bash make_samples.sh` makes more frames.

## Stage 1 in work (started 2026-11-18)

Done and checked on the PC:

- `cr3gui/src/pb_pagedoc.{h,cpp}`: `PbPageDoc`, one interface over both libraries, free of the
  engine and of the firmware. A frame (`PbPageView`: the point of the page in the middle of the
  buffer, zoom, angle) is drawn by the library straight into the 8-bit gray buffer of the caller.
  PDF: the display list of the page is kept, the draw device writes into our buffer, any angle is
  a matrix. DjVu: the decoded page is kept, the decoder scales and writes the rows itself, quarter
  turns are its own, a small angle is `pbRotateGray` (16.16 fixed point, bilinear) from an upright
  frame with room for the corners. `contentBox`: on a picture of the page 256 pixels wide, Otsu
  level, a dark rim means a page photographed on a table (the paper is found first), then the
  rows and columns with enough dark pixels; kept per page. `contents`, `hasText`, `pageSize`.
- `dev/pdfdjvu/pagebench.cpp`, `build_pagebench.sh`: the bench of that interface. Times on the PC,
  buffer 1404 × 1872, "first" reads the page, "again" is another frame of the same page:

  | File | first | again | turned 1.5° |
  |---|---|---|---|
  | text PDF (owner's, 161 pages) | 1–3 ms | 0.4 ms | 0.8 ms |
  | photographed page, JPEG 12 MP in a PDF | 55–155 ms | 9–12 ms | 21 ms |
  | photographed page, DjVu IW44 12 MP | 114 ms | 15–19 ms | 25 ms |
  | DjVu 600 dpi 4644 × 6719 (owner's, 789 pages) | 32 ms | 6–11 ms | 13 ms |
  | DjVu 300 dpi landscape, compound | 22 ms | 14 ms | 19 ms |

- Run the bench on files of the Linux side: DjVuLibre reads in small pieces, and a file under
  `/mnt/c` took 4.6 s a page instead of 0.11 s. To be watched on the storage of a reader.
- Known: the content box of a photographed page takes in the shadow at the spine (it is dark).

The owner's real files (not in the repository) are in `~/PBDEV/pdfdjvu/user` on the first machine.
A PDF or DjVu inside a zip archive has to open too (his word): taken out once into a kept file.

## Stage 1: written and built, not run on a reader (2026-11-18)

`JOBS=4 bash dev/build/build_package.sh v8` builds `0.0.8-opds-alpha1` with no error: the program
is 9.6 MB (5.1 MB in `master`), the package 9.7 MB. First build the libraries:
`fetch_sources.sh`, a first run of `build_package.sh v8` (it configures the tree), then
`build_libs_arm.sh v8`.

- **The libraries are not in the repository.** `build_libs_arm.sh` builds them from the release
  archives against the FreeType, zlib and libjpeg of the program into `~/PBDEV/pdfdjvu/arm/lib`;
  cmake takes them by `-D PDFDJVU_DIR` (`cr3gui/CMakeLists.txt`, define `CR3_PAGEDOC`), linked
  with `pthread` and `--gc-sections`. `clock_gettime` is not used: it is in librt of glibc 2.9.
- **`PbPageWindow`** (`cr3gui/src/pb_pageview.{h,cpp}`): the window of the picture mode over the
  book window. Opened in `cr3pocketbook.cpp` before `main_win->loadDocument` when
  `PbPageWindow::isPageFile`. Scale (five values), leafing (two), the frame-setting screen (eight
  buttons, a drag pans), the next frame prepared ahead by a weak timer of 0.4 s, the menu as a
  touch list, contents through `PbTocList`, the page selector of the firmware, bookmarks, the
  page number in the corner, the state of a file in `system/state/cr3/pages/<fnv>.state`, a
  document of a zip taken out to `system/state/cr3/pages/arc-*`, reading statistics
  (`PbStatsView::pageOfFileShown`). Commands it does not know go to the book window, except those
  that act on the text of a book.
- **Settings**: `settings.cpp`, two `CRSettingsGroupMenu` folders, six properties
  `cr3.pocketbook.pdf|djvu.scale|leafing|page.number`.
- Strings in `cr3_ru.po` and `cr3_uk.po`, the section "PDF and DjVu" of the three manuals, the
  changelog, the licence lines of `README.md`, `pdf`, `djvu`, `djv` in the template
  `data/common/system/config/extensions.cfg`. A package does not carry `system/config`: on a
  reader the owner's own `system/config/extensions.cfg` needs the lines with `cr3-pb.app`.
- **The frames** are computed by `PbPageDoc::framesOf` (`pb_pagedoc.cpp`), so `pagebench` runs the
  same code: it prints the frames of every scale and leafing for a page.

Optimisation pass, numbers of the PC bench (buffer 1404 × 1872):

- **A photographed page of a PDF was decoded anew for every frame**: MuPDF keeps a decoded
  picture per zoom step and per visible part, so each frame of "by frames" and each step of the
  frame-setting screen cost a decoding (50–57 ms for 12 megapixels). `PbPdfDoc` now passes the
  page through a device (`fz_new_passthrough_device_of_size`, `fill_image` replaced) that gives
  a large picture as a gray copy decoded once, at the size the frames need
  (`PbPageDoc::_detailZoom`, told by `framesOf` before the content box is looked for). First frame
  of a page 56–59 ms as before, every other frame 9–12 ms (was 50–57); the whole-page frame has
  the hash it had.
- **Two pages are kept** (the one shown and the one prepared ahead), with their display lists and
  decoded pictures: going back to the first reads nothing.
- Colour management is off (`fz_disable_icc`): the screen is gray. A coloured cover 14.5 → 11.7 ms.
- Tried and dropped: a few pixels drawn at a large zoom to make the library keep the whole
  picture (it keeps only the part drawn).

Open, by what it would give:

1. DjVu: every frame of a page is scaled by the decoder (5–19 ms, a compound page of 600 dpi
   38–41 ms; a reader is 30–50 times slower). A kept gray picture of the page, as for a PDF,
   would make the frames after the first nearly free; it needs an own scaler with area averaging.
   Only one decoded DjVu page is kept (`PbDjvuDoc::_page`).
2. The content box of a photographed page takes in the shadow at the spine.
3. MuPDF built with `FZ_ENABLE_ICC=0` and without the CJK character maps would be about 2 MB
   smaller; without the maps a Chinese, Japanese or Korean PDF that relies on them shows no text.
4. Not seen on the reader yet (alpha1 showed white pages): the content box and the scales on
   real pages, the frame-setting screen, the lists of the menu, the contents, a file in a zip.

## First run on the reader: alpha1, then alpha2 (2026-11-18)

InkPad 4, firmware 6.11, log level "Отладка". What the log of alpha1 told:

- **Every page was white.** The canvas of the screen is an `LVGrayDrawBuf` of 4 bits (2, 3, 4 or
  8 by `main()` of `cr3pocketbook.cpp`); the page was handed to `LVGrayDrawBuf::DrawTo` as 8 bits,
  and `DrawTo` returns at once when the two depths differ. Fixed in alpha2: `toCanvasDepth`
  (`pb_pageview.cpp`) clears the low bits when a frame is rendered, `draw()` hands the picture
  over in the depth of the canvas (2 bits: packed). PC check outside the repository:
  `C:\Temp\cr3-opds-build\pdfdjvu\depth_test.sh` (takes `toCanvasDepth` out of the source, runs
  it and `DrawTo` of the bench engine library for 2, 3, 4, 8 bits; it also shows the alpha1 fault).
  **Lesson: the bench renders into its own buffer; what goes to the canvas has to be checked
  against the depth of the canvas.**
- Times on the reader: a text page of a PDF 40–97 ms (PC 1–3 ms); a page of the owner's 600 dpi
  DjVu 300–600 ms, its first three pages 2.0–2.2 s each (PC 32 ms). So the reader is 30–60 times
  slower, as assumed. alpha2 logs the parts: `page read`, `content box`, the rest is the frame
  (`PbPageDoc::readMs`, `boxMs`). Look at them before optimising DjVu (item 1 above).
- Worked as written: opening from the library, page turns by taps, the frame prepared ahead and
  swapped in (0.0 ms at the turn), the state files `system/state/cr3/pages/*.state`, the book state
  of the firmware saved at the exit (`bsSave`), the settings folders, the list
  `system/config/extensions.cfg` written with CRLF kept.
- The owner set "Open with CoolReader" to *In the "Open with" list* and expected the reader to
  remember his choice there, as it seems to for FB2. It remembers nothing: it takes the first
  program of the line, which is CoolReader for FB2. *By default* is the value for that.
- **He expects a file with a text layer to read as a book already.** That is stages 2 and 3; until
  then every PDF and DjVu is shown in the picture mode. Of the test files a text layer have:
  `diag-elements.pdf`, `diag-book.pdf`, `diag-elements.djvu`, and of his files a text PDF of 161
  pages and two DjVu; pictures only: `diag-scan-photo.pdf`, `diag-scan-photo.djvu`,
  `diag-scan-bw.djvu`, and of his files a scanned PDF and a DjVu of 789 pages.

## Second run on the reader (alpha2) and the work after it, 2026-11-18

**Where the work stands: `0.0.8-opds-alpha3` is in the sources with stages 1, 2 and 3, compiled
and linked for the reader (`build_package.sh v8 check`: 0 errors), not made into a package and
not installed.** The
owner said after the second run: fix, do not build, do not install, go on to the next stage; and:
after every stage an optimisation cycle with the bench. Nothing of alpha3 has run on a reader.

What the second run told (the log is in the notes of the first machine, not in the repository):

- The pages are shown. The owner: **"the turn of the frame does not work, the touch panel is
  dead"**. The log shows *Set the frame* drawn once and nothing after: a touch posts no event and
  the window manager draws only after an event. Fixed: `PbPageWindow::redrawAfterTouch` posts
  `CRGUIUpdateEvent`. **Any touch handler of a window that changes the picture without posting a
  command has to do that.**
- He set *Open with CoolReader* to *By default* for PDF and DjVu; the firmware obeyed its list.
- Times of a frame by parts are in `CHANGELOG.md` ("the picture mode made faster").

Done after it, each checked on the PC:

- **Stage 1 optimisation cycle**: `pbBlitGray`, the gray copy of a PDF picture drawn here, the
  kept picture of a DjVu page, the next DjVu page decoded beside. Numbers in `CHANGELOG.md`.
  Run again with `C:\\Temp\\cr3-opds-build\\pdfdjvu\\opt1.sh <label>` and `opt1_turn.sh` (outside the
  repository; they call `pagebench`, whose "turn" lines are a page turn as the window makes it).
  Not measured on a reader. If the frame of a DjVu page is still slow there, the next thing is
  the blit itself (2.6 megapixels a frame).
- **"Open with CoolReader" for all formats**: `pb_openwith.{h,cpp}`, a settings folder; PDF and
  DjVu only in their own folders (the owner's word). PC check outside the repository:
  `openwith_test.sh` against the list of his reader.
- **Stage 2, a PDF with text as a book**: `pb_pdftext.{h,cpp}` (the translator, MuPDF only),
  `pb_pdfbook.{h,cpp}` (where the book is kept, the way between the book and the pages),
  `dev/pdfdjvu/pdf2book.cpp` + `build_pdf2book.sh` (the PC tool). What it does and its numbers are
  in `CHANGELOG.md`. **Lessons of the MuPDF API:**
  - `fz_try`, `fz_always`, `fz_catch` are three statements. `if (x) fz_try {...} fz_catch {...}`
    runs the catch part without a try and crashes; they need a block of their own.
  - Nothing with a destructor may be alive across a call that can throw (a longjmp): the calls
    stand in small `fz_try` blocks with plain variables.
  - Without `FZ_STEXT_COLLECT_VECTORS` the table hunt does not see the rules of a table and
    takes half a page for a table of two columns.
  - The library gives the cells of a row without rules as lines of their own on one row, and
    ends a paragraph at every short line of a text with a ragged right edge.
  - `FZ_STEXT_SEGMENT` is 82 % of the extraction time; it is asked for only when the columns of
    a page come mixed.

- **Stage 3, a DjVu with a text layer as a book** (the owner: "do not build; finish with DjVu"):
  `pb_djvutext.{h,cpp}` (DjVuLibre for the text layer and the pages, MuPDF only to pack the
  pictures as JPEG), wired through `pb_pdfbook.cpp` (its names say "PDF", it serves both),
  `dev/pdfdjvu/djvu2book.cpp` + `build_djvu2book.sh`. What it does and its numbers are in
  `CHANGELOG.md`. **Lessons:**
  - Text layers differ: one file has page/column/region/para/line/word, another only lines
    under the page, with a line end in front of the first word of every line and stray bytes
    that are not UTF-8 (the book was not well-formed until `clean` dropped them).
  - A file made from a PDF gives the words of a justified line as lines of their own on one row.
    They are merged; pieces set wide apart are cells of a table only when the row beside has its
    pieces at the same places, and never inside a paragraph of the file that has a usual line.
  - The boxes of the lines are not exact. A place "with no text" taken right up to the lines
    brought halves of letters into the pictures.
  - The first cut of the pictures was mostly rubbish (the frame and the ornament of every
    slide). The owner's rules stand in `takePictures`: inside the text block only, a least size
    in millimetres, no strips, rules and frames not counted, the same thing on three pages out.
    **Look at the pictures a translator makes** (`fb2_pictures.py` outside the repository takes
    them out of a book) before telling the owner it works.
  - Under WSL the first `ddjvu_context_create` of a run takes 60 ms: the library looks for the
    program along `PATH`, where the folders of Windows are. Benches run with a short `PATH`.
- **The owner on the known limits of the PDF translator** ("deal with the known bugs, so that
  formulas are pictures"): a formula inside a line with a root, a sum or an integral is a
  picture in the line; pictures are taken by size, shape and place; a contents page is lines
  with one mark for the dots. Test file: `make_formula_pdf.py` (outside the repository).
- **Dropped by the owner**: the dictionary, the search and citations on the text layer in the
  picture mode ("not needed: what would we search in pictures?"). A file with a text layer is a
  book, and a book has all three. `PbPageDoc::words` was written for it and taken out again.

- **The owner after the report of stage 3: "go on fixing, and put it on the reader only when
  all is fixed."** The six limits listed then are done (`CHANGELOG.md` has each): PDF, a
  fraction inside a line, a table given as one paragraph, glossaries; DjVu, a picture beside
  the text, a formula inside a line, tables by the words of a line, and with them a drawing
  with words in it as one picture. **Lessons:**
  - MuPDF gives a fraction inside a line as three things: the line up to it (with the numerator
    taken in as raised characters), the denominator as a block of its own, and the rest of the
    line as another block. `PB_PDFTEXT_LINES=<page>` shows it.
  - A rule made for one case changed another: counting the characters of a picture in a line
    as formula signs kept a display formula with a root whole; not counting them (for the
    fraction) broke it. The test files are run all together after every change (`pdf_run.sh`,
    `djvu_run.sh` outside the repository), and the counts in their second lines are read.
  - A picture grown over its labels ran into the frame and the ground of a slide. The owner's
    rule holds there too: not out of the text block.
  - Check a rule on the owner's book by the list of pages it fires on, not by one page: the
    list-of-entries rule fires on pages 149 to 161 of his book and on no other.

The program compiles and links for the reader with all of this (`build_package.sh v8 check`:
0 errors).

Not done, in the order it would be taken:

**Third run on the reader (alpha3, 2026-11-18).** The owner opened books made of PDF and DjVu
files and went between the two views: it works. His remarks, all done in alpha4
(`CHANGELOG.md`): the item that led from a book to its pages was lost on the second page of
the settings and looked like a double of the setting in the PDF folder (now *View as the
original*, the first item of the main menu of the book; the setting is *A new file with
text*); a slide of his DjVu gave a column of stray marks (a picture the recognition read as
text: `dropNoise`); the zoom buttons of *Set the frame* were too coarse. From the log (kept
outside the repository): a DjVu book of 9 pages 1.6 s (PC 85 ms, 19 times; 0.29 s a decoded
page); in the picture mode a page prepared ahead is shown in 5 ms and prepared in 250 ms.
The log is cut at every start of the program: read it before the next start.

The names in the list below are those of alpha3 (*Read as a book* is *View as a book* now,
*Pages of the ... as they are* is *View as the original*, *A file with text* is *A new file
with text*).

1. The run of alpha4 on the reader. Device-only code that
   had never run before alpha3: `pb_pdfbook.cpp` (the progress bar, the kept books, `goLink` to `#pgN`,
   the page of a place by `getAbsRect`; now for DjVu too), the menu item *Read as a book* (it
   closes its own window before the book is loaded), the settings items *Pages of the PDF as
   they are* and *Pages of the DjVu as they are*, *A file with text* in both folders, the
   folder *Open with CoolReader*, the kept picture of a DjVu page and `hintNext` in the window.
2. The first opening of a file with text on a reader: the PDF book of 161 pages should take
   about 5 s to make (84 ms on the PC), then the engine reads an FB2 of 1.9 MB. A DjVu book
   costs by the pages that have to be decoded for pictures: 83 of the 84 slides took 1.1 s on
   the PC, so 30 to 50 s there, once; a book of text pages has far fewer such pages. The log
   lines `perf: PDF as a book` and `perf: DjVu as a book` give the parts. If it is too long:
   fewer places looked at (a larger least gap, no places beside the text), or the pictures of
   a DjVu book as an option.
3. Known limits of the translators. PDF: a formula of plain signs inside a line stays text with
   superscripts and subscripts (only one with a root, a sum, an integral or a fraction is a
   picture). DjVu: a formula inside a line is a picture only when the recognition gave one of
   those signs; a page the file tells to show turned gives no pictures; a drawing with words
   that finds no white band within twelve lines stays in pieces (seen on three slides of the
   owner's file: a table of marks, a scheme).
4. Size: MuPDF without ICC and the CJK maps would take about 2 MB off the program (9.81 MB
   stripped now); ask the owner about Chinese, Japanese and Korean PDFs first.

Asked of the owner: more of the files he really reads, to see where the translators fail.
