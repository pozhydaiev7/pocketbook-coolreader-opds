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

## Next: stage 1, picture mode

1. `thirdparty/mupdf` and `thirdparty/djvulibre` in the cmake build, with the FreeType, zlib and
   libjpeg of the program; `thirdparty/versions.json`; `getentropy` for glibc 2.9.
2. `cr3gui/src/pb_pagedoc.{h,cpp}`: one interface of a document of pages with two back ends (page
   count, page size, a region of a page drawn into a gray buffer, contents, words with rectangles).
3. `cr3gui/src/pb_pageview.{h,cpp}`: the window of the picture mode on `CRGUIWindowBase`; scale,
   leafing, frame with rotation for odd and even pages, next page prepared ahead, contents by
   `PbTocList`, go to page, bookmarks, position kept, reading statistics.
4. The two settings folders, the strings in `cr3gui/po`, the three manuals, the changelog,
   `pdf` and `djvu` in `data/common/system/config/extensions.cfg`.
5. A PC bench for the page documents next to `dev/bench`, then the optimisation pass.

Not known yet: the speed on a reader (the ARM probes `pdfprobe-arm` and `djvuprobe-arm` are built
and can be run there), and how the text mode does on real books of the owner (asked of him: five
to ten files he reads).
