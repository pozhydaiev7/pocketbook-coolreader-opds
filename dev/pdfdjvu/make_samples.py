"""Diagnostic files of PDF and DjVu: the parts that need no library (run inside WSL, python3).

    make_samples.py html <dir>              elements.html, book.html, their pictures, finish.js
    make_samples.py compose <dir> <pages>   the page files of mutool create for the "camera" pages
    make_samples.py threshold <in.pgm> <out.pbm> <level>
    make_samples.py djvutxt <stext.xml> <dpi> <out prefix>   text layer of each page for djvused;
                                                             prints the numbers of the pages with pictures

The documents are laid out by the HTML engine of MuPDF (mutool convert), see make_samples.sh.
"""
import math
import os
import random
import struct
import sys
import xml.etree.ElementTree as ET
import zlib


# ---------------------------------------------------------------- pictures
def write_png(path, width, height, pixels, alpha=None):
    """8-bit gray picture; alpha: one more byte per pixel"""
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
    rows = bytearray()
    if alpha is None:
        for y in range(height):
            rows += b"\0" + pixels[y * width:(y + 1) * width]
        color = 0
    else:
        for y in range(height):
            row = bytearray(width * 2)
            row[0::2] = pixels[y * width:(y + 1) * width]
            row[1::2] = alpha[y * width:(y + 1) * width]
            rows += b"\0" + row
        color = 4
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, color, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(rows), 6)))
        f.write(chunk(b"IEND", b""))


def chart_png(path):
    w, h = 600, 360
    px = bytearray(b"\xff" * (w * h))
    def rect(x0, y0, x1, y1, v):
        for y in range(y0, y1):
            px[y * w + x0:y * w + x1] = bytes([v]) * (x1 - x0)
    rect(40, 20, 43, 320, 0)
    rect(40, 317, 580, 320, 0)
    for i, (value, gray) in enumerate([(120, 60), (210, 110), (160, 60), (270, 150), (90, 110), (240, 60)]):
        rect(70 + i * 85, 317 - value, 130 + i * 85, 317, gray)
    for y in range(80, 317, 60):
        for x in range(44, 580, 6):
            px[y * w + x] = 150
    write_png(path, w, h, px)


def photo_png(path):
    w, h = 640, 420
    px = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            v = 70 + 120 * x / w + 40 * math.sin(y / 23.0) + 25 * math.sin((x + y) / 11.0)
            d = math.hypot(x - 420, y - 170)
            if d < 110:
                v += 90 * (1 - d / 110)
            px[y * w + x] = max(0, min(255, int(v)))
    write_png(path, w, h, px)


# ---------------------------------------------------------------- text
P_RU = [
    "Чтение с экрана электронной книги отличается от чтения бумажной страницы меньше, чем принято думать. "
    "Глаз так же движется по строке скачками, так же возвращается к началу следующей строки, и так же устаёт "
    "от слишком длинных строк и слишком мелкого шрифта. Поэтому всё, что наборщики знали о ширине полосы, "
    "интерлиньяже и полях, остаётся в силе и для экрана в шесть или восемь дюймов.",
    "Сканированная книга хранит не текст, а картинку страницы. Её нельзя перенабрать другим шрифтом, зато "
    "она передаёт страницу в точности: с формулами, таблицами, пометками на полях и следами времени. Читать "
    "такую книгу на небольшом экране удобно только тогда, когда программа умеет показать нужную часть "
    "страницы крупно и не заставляет каждый раз искать её заново.",
    "Когда книгу снимают фотоаппаратом, вокруг страницы остаются широкие поля: стол, край соседней страницы, "
    "тень от переплёта. Сама страница занимает середину кадра и слегка повёрнута. На чётных и нечётных "
    "страницах поля зеркальны, потому что корешок оказывается то слева, то справа.",
    "Текстовый слой добавляют программой распознавания. Он лежит под картинкой и хранит слова вместе с их "
    "прямоугольниками, сгруппированные в строки, абзацы и колонки. По нему ищут, из него копируют цитаты, "
    "и по нему же можно показать перевод слова, которого коснулись пальцем.",
]
P_EN = [
    "A page of a PDF file is a list of drawing commands: place this glyph here, draw a line there, paint "
    "this picture into that rectangle. Nothing in the file says where a paragraph ends, which line is a "
    "heading, or that six short lines of numbers are the rows of a table. A program that wants to show the "
    "text in another font has to guess all of that from the positions and the sizes of the letters.",
    "The guess is good for a novel: one column, lines of the same length, a larger font for the titles. "
    "It is harder for a textbook with two columns, footnotes, formulas and pictures with captions, and a "
    "careful program falls back to the picture of a part of the page when it is not sure.",
]


def elements_html():
    ru, en = P_RU, P_EN
    return f"""<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Диагностический документ</title>
<style>
@page {{ margin: 54pt 44pt 50pt 44pt; }}
body {{ font-family: serif; font-size: 10.5pt; line-height: 1.3; text-align: justify; }}
h1 {{ font-size: 19pt; text-align: center; margin: 0 0 14pt 0; page-break-before: always; }}
h1.first {{ page-break-before: avoid; }}
h2 {{ font-size: 14pt; text-align: left; margin: 12pt 0 6pt 0; }}
h3 {{ font-size: 11.5pt; text-align: left; font-style: italic; margin: 9pt 0 4pt 0; }}
p {{ margin: 0; text-indent: 16pt; }}
p.noindent {{ text-indent: 0; margin-top: 5pt; }}
p.center {{ text-indent: 0; text-align: center; }}
p.caption {{ text-indent: 0; text-align: center; font-size: 9pt; font-style: italic; margin: 3pt 0 8pt 0; }}
p.note {{ text-indent: 0; font-size: 8pt; margin-top: 2pt; }}
p.epigraph {{ text-indent: 0; margin: 0 0 10pt 55%; font-style: italic; font-size: 9.5pt; text-align: left; }}
blockquote {{ margin: 6pt 22pt; font-size: 10pt; }}
pre {{ font-family: monospace; font-size: 8.5pt; margin: 6pt 12pt; text-align: left; }}
code {{ font-family: monospace; font-size: 9.5pt; }}
table.grid {{ border-collapse: collapse; margin: 8pt auto; font-size: 9.5pt; text-align: left; }}
table.grid td, table.grid th {{ border: 0.7pt solid black; padding: 2pt 5pt; }}
table.plain {{ margin: 8pt auto; font-size: 9.5pt; text-align: left; }}
table.plain td, table.plain th {{ padding: 2pt 9pt; }}
table.columns {{ width: 100%; }}
table.columns td {{ width: 50%; vertical-align: top; padding: 0 7pt; font-size: 9.5pt; text-align: justify; }}
table.formula {{ margin: 6pt auto; text-align: center; font-style: italic; }}
td.num {{ border-bottom: 0.7pt solid black; padding: 0 5pt; }}
td.den {{ padding: 0 5pt; }}
hr.notes {{ width: 30%; margin: 14pt 0 3pt 0; text-align: left; }}
img.wide {{ width: 80%; }}
img.small {{ width: 45%; }}
.sc {{ font-variant: small-caps; }}
</style></head><body>

<h1 class="first">Диагностический документ</h1>
<p class="center">Все элементы страницы PDF и DjVu в одном файле</p>
<p class="center"><i>CoolReader для PocketBook, линия 0.0.8</i></p>
<p class="epigraph">Книга есть способ существования текста во времени.<br/>— из записной книжки наборщика</p>
<p>{ru[0]}</p>
<p>{ru[1]} В этом абзаце есть <b>полужирный текст</b>, <i>курсив</i>, <b><i>полужирный курсив</i></b>,
<u>подчёркнутый текст</u>, <code>моноширинный шрифт</code>, верхний индекс: E = mc<sup>2</sup>, нижний
индекс: H<sub>2</sub>O, сноска<sup>1</sup> и <a href="https://example.org/">ссылка на сайт</a>.</p>
<p>{en[0]}</p>
<hr class="notes"/>
<p class="note"><sup>1</sup> Текст сноски стоит в нижней части страницы и набран мельче основного текста.</p>
<p class="note"><sup>2</sup> A second footnote, in English, with a number: 3.14159.</p>

<h1>Глава 1. Заголовки и абзацы</h1>
<h2>1.1. Заголовок второго уровня</h2>
<p>{ru[2]}</p>
<p>{ru[3]}</p>
<h3>Заголовок третьего уровня</h3>
<p>{en[1]}</p>
<blockquote>Цитата набрана с отступами слева и справа. {ru[2]}</blockquote>
<h2>1.2. Списки</h2>
<p class="noindent">Маркированный список:</p>
<ul><li>первый пункт списка;</li><li>второй пункт, длинный, чтобы он занял больше одной строки и было видно,
как переносится текст внутри пункта списка;</li><li>третий пункт.</li></ul>
<p class="noindent">Нумерованный список:</p>
<ol><li>открыть файл;</li><li>выбрать масштаб и кадр;</li><li>листать страницы.</li></ol>
<h2>1.3. Стихи и код</h2>
<p class="noindent">Буря мглою небо кроет,<br/>Вихри снежные крутя;<br/>То, как зверь, она завоет,<br/>То
заплачет, как дитя.</p>
<pre>for (page = 0; page &lt; count; page++) {{
    image = render(page, frame);
    show(image);
}}</pre>

<h1>Глава 2. Таблицы</h1>
<h2>2.1. Простая таблица с линиями</h2>
<table class="grid">
<tr><th>Формат</th><th>Текст</th><th>Картинки</th><th>Размер, МБ</th></tr>
<tr><td>FB2</td><td>да</td><td>да</td><td>1,2</td></tr>
<tr><td>EPUB</td><td>да</td><td>да</td><td>0,8</td></tr>
<tr><td>PDF</td><td>иногда</td><td>да</td><td>14,5</td></tr>
<tr><td>DjVu</td><td>слой</td><td>страница</td><td>6,3</td></tr>
</table>
<p class="caption">Таблица 1. Простая таблица: четыре столбца, пять строк, все линии.</p>
<h2>2.2. Таблица без линий</h2>
<table class="plain">
<tr><th>Устройство</th><th>Экран</th><th>Точек на дюйм</th></tr>
<tr><td>PocketBook 632</td><td>1072 × 1448</td><td>300</td></tr>
<tr><td>InkPad 4</td><td>1404 × 1872</td><td>300</td></tr>
<tr><td>PocketBook 740</td><td>1404 × 1872</td><td>300</td></tr>
</table>
<p class="caption">Таблица 2. Таблица без линий: столбцы видны только по выравниванию.</p>
<h2>2.3. Сложная таблица</h2>
<table class="grid">
<tr><th rowspan="2">Режим</th><th colspan="2">Листание</th><th rowspan="2">Примечание</th></tr>
<tr><th>страница</th><th>кадр</th></tr>
<tr><td>Вся страница</td><td>да</td><td>—</td><td>мелко на экране 6 дюймов</td></tr>
<tr><td>По ширине</td><td>да</td><td>да</td><td>два или три кадра на страницу; длинный текст в ячейке
переносится на несколько строк</td></tr>
<tr><td>Свой кадр</td><td>да</td><td>да</td><td>кадр держится при листании</td></tr>
<tr><td colspan="3">Итого режимов</td><td>3</td></tr>
</table>
<p class="caption">Таблица 3. Объединённые ячейки по строкам и по столбцам.</p>
<p>{ru[0]}</p>

<h1>Глава 3. Картинки и формулы</h1>
<p>{ru[1]}</p>
<p class="center"><img class="wide" src="fig-chart.png"/></p>
<p class="caption">Рис. 1. Диаграмма: картинка во всю ширину с подписью.</p>
<p>{en[0]}</p>
<p class="center"><img class="small" src="fig-photo.png"/></p>
<p class="caption">Рис. 2. Полутоновая картинка.</p>
<h2>3.1. Формулы</h2>
<p>Формула в строке: a<sup>2</sup> + b<sup>2</sup> = c<sup>2</sup>, сумма x<sub>1</sub> + x<sub>2</sub> + … +
x<sub>n</sub>. Формула отдельной строкой:</p>
<table class="formula"><tr><td rowspan="2">x<sub>1,2</sub> = </td><td class="num">−b ± √(b<sup>2</sup> − 4ac)</td>
<td rowspan="2">&#160;&#160;&#160;(1)</td></tr><tr><td class="den">2a</td></tr></table>
<p>{ru[3]}</p>

<h1>Глава 4. Две колонки</h1>
<table class="columns"><tr>
<td><p>{ru[0]}</p><p>{ru[2]}</p><p>{en[1]}</p></td>
<td><p>{ru[1]}</p><p>{ru[3]}</p><p>{en[0]}</p></td>
</tr></table>

<h1>Chapter 5. English text</h1>
<h2>5.1. Ligatures and hyphenation</h2>
<p>{en[0]} The office staff filed five affidavits: fi, fl, ff, ffi and ffl are the ligatures of this font.</p>
<p>{en[1]}</p>
<h2>5.2. Small print</h2>
<p class="note">{en[0]}</p>
<p class="note">{ru[2]}</p>
<p>{ru[0]}</p>
<p>{ru[1]}</p>
</body></html>
"""


def book_html():
    parts = ["""<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Книга для съёмки</title>
<style>
@page { margin: 50pt 46pt 54pt 46pt; }
body { font-family: serif; font-size: 11pt; line-height: 1.32; text-align: justify; }
h1 { font-size: 17pt; text-align: center; margin: 30pt 0 18pt 0; page-break-before: always; }
h1.first { page-break-before: avoid; }
p { margin: 0; text-indent: 17pt; }
</style></head><body>"""]
    order = [0, 1, 2, 3, 2, 0, 3, 1]
    for chapter in range(3):
        parts.append('<h1%s>Глава %d</h1>' % (' class="first"' if chapter == 0 else "", chapter + 1))
        for i in range(11):
            parts.append("<p>%s</p>" % P_RU[order[(i + chapter) % len(order)]])
            if i % 4 == 3:
                parts.append("<p>%s</p>" % P_EN[(i + chapter) % 2])
    parts.append("</body></html>")
    return "\n".join(parts)


HEADINGS = [
    (0, "Диагностический документ"),
    (0, "Глава 1. Заголовки и абзацы"), (1, "1.1. Заголовок второго уровня"), (1, "1.2. Списки"),
    (1, "1.3. Стихи и код"),
    (0, "Глава 2. Таблицы"), (1, "2.1. Простая таблица с линиями"), (1, "2.2. Таблица без линий"),
    (1, "2.3. Сложная таблица"),
    (0, "Глава 3. Картинки и формулы"), (1, "3.1. Формулы"),
    (0, "Глава 4. Две колонки"),
    (0, "Chapter 5. English text"), (1, "5.1. Ligatures and hyphenation"), (1, "5.2. Small print"),
]


def finish_js():
    """mutool run finish.js in.pdf out.pdf: the running title, the page number, the contents"""
    title_hex = "Диагностический документ".encode("koi8_u").hex()
    heads = ",\n".join('  [%d, "%s"]' % (level, title) for level, title in HEADINGS)
    return """// made by make_samples.py: the running title and the page number of each page, the contents
var doc = Document.openDocument(scriptArgs[0]);
var font = doc.addSimpleFont(new Font("Times-Roman"), "Cyrillic");
var count = doc.countPages();
var headings = [
""" + heads + """
];
var pageOf = [];
var from = 0;
for (var h = 0; h < headings.length; h++) {
    pageOf[h] = -1;
    for (var n = from; n < count && pageOf[h] < 0; n++) {
        var hits = doc.loadPage(n).toStructuredText().search(headings[h][1]);
        if (hits && hits.length > 0) { pageOf[h] = n; from = n; }
    }
    print("heading", h, "page", pageOf[h] + 1, headings[h][1]);
}
for (var n = 0; n < count; n++) {
    var page = doc.findPage(n);
    var box = page.get("MediaBox");
    var w = box.get(2).asNumber(), top = box.get(3).asNumber();
    var number = "" + (n + 1);
    var ops = "q 0 g 0 G 0.5 w\\n";
    if (n > 0) {
        ops += "BT /FHdr 8 Tf 44 " + (top - 36) + " Td <""" + title_hex + """> Tj ET\\n";
        ops += "BT /FHdr 8 Tf " + (w - 44 - 4 * number.length) + " " + (top - 36) + " Td (" + number + ") Tj ET\\n";
        ops += "44 " + (top - 40) + " m " + (w - 44) + " " + (top - 40) + " l S\\n";
    }
    ops += "BT /FHdr 8 Tf " + (w / 2 - 2 * number.length - 8) + " 28 Td (- " + number + " -) Tj ET\\nQ\\n";
    var extra = doc.addStream(ops, {});
    var res = page.get("Resources");
    var fonts = res.get("Font");
    if (!fonts || fonts.isNull()) { fonts = doc.newDictionary(); res.put("Font", fonts); }
    fonts.put("FHdr", font);
    var old = page.get("Contents");
    var list = doc.newArray();
    list.push(extra);
    if (old.isArray()) { for (var i = 0; i < old.length; i++) list.push(old.get(i)); } else list.push(old);
    page.put("Contents", list);
}
var it = doc.outlineIterator();
var inChild = false;
for (var h = 0; h < headings.length; h++) {
    if (pageOf[h] < 0) continue;
    var item = { title: headings[h][1], uri: "#page=" + (pageOf[h] + 1), open: true };
    if (headings[h][0] == 0) {
        if (inChild) { it.up(); it.next(); inChild = false; }
        it.insert(item);
    } else {
        if (!inChild) { it.prev(); it.down(); inChild = true; }
        it.insert(item);
    }
}
doc.save(scriptArgs[1], "compress");
"""


def cmd_html(folder):
    os.makedirs(folder, exist_ok=True)
    chart_png(os.path.join(folder, "fig-chart.png"))
    photo_png(os.path.join(folder, "fig-photo.png"))
    for name, text in (("elements.html", elements_html()), ("book.html", book_html()), ("finish.js", finish_js())):
        with open(os.path.join(folder, name), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    print("html, pictures and finish.js written to", folder)


# ---------------------------------------------------------------- a page photographed with a camera
PHOTO_W, PHOTO_H = 720.0, 960.0     # points; drawn at 300 dpi: 3000 x 4000, a 12 megapixel frame


def cmd_compose(folder, pages):
    rnd = random.Random(8)
    w, h = 750, 1000
    # the table under the book: dark, grainy
    desk = bytearray(os.urandom(w * h)).translate(bytes(88 + (v % 34) for v in range(256)))
    write_png(os.path.join(folder, "desk.png"), w, h, desk)
    # uneven light of a lamp over the whole frame: a black layer, clear in the middle
    black = bytearray(w * h)
    light = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            d = math.hypot((x - w * 0.46) / w, (y - h * 0.42) / h)
            light[y * w + x] = max(0, min(150, int(d * d * 330)))
    write_png(os.path.join(folder, "light.png"), w, h, black, light)
    # the paper is not white, and it darkens to the spine: one layer for the spine at the left, one at the right
    pw, ph = 420, 595
    for side in ("left", "right"):
        shade = bytearray(pw * ph)
        for y in range(ph):
            for x in range(pw):
                to_spine = x if side == "left" else pw - 1 - x
                shade[y * pw + x] = 26 + max(0, int(95 * (1 - to_spine / 70.0))) + rnd.randrange(7)
        write_png(os.path.join(folder, "shade-%s.png" % side), pw, ph, bytearray(pw * ph), shade)
    for n in range(1, pages + 1):
        odd = n % 2 == 1
        # the page takes 58 % of the width of the frame; the spine is at the left on odd pages
        scale = PHOTO_W * 0.58 / 420.0
        angle = math.radians((1.3 if odd else -1.1) + rnd.uniform(-0.4, 0.4))
        pwid, phei = 420.0 * scale, 595.0 * scale
        cx = PHOTO_W * (0.55 if odd else 0.45) + rnd.uniform(-8, 8)
        cy = PHOTO_H * 0.52 + rnd.uniform(-10, 10)
        ca, sa = math.cos(angle), math.sin(angle)
        # unit square -> rotated rectangle of the page centred at cx, cy
        a, b, c, d = pwid * ca, pwid * sa, -phei * sa, phei * ca
        e = cx - (a + c) / 2
        f = cy - (b + d) / 2
        m = "%.4f %.4f %.4f %.4f %.3f %.3f cm" % (a, b, c, d, e, f)
        # the edge of the facing page behind the spine: a strip of paper
        strip_w = pwid * 0.16
        sx = -strip_w / pwid if odd else 1.0
        lines = [
            "%%%%MediaBox 0 0 %d %d" % (PHOTO_W, PHOTO_H),
            "%%Image Desk desk.png",
            "%%%%Image Page book-%d.png" % n,
            "%%%%Image Shade shade-%s.png" % ("left" if odd else "right"),
            "%%Image Light light.png",
            "q %d 0 0 %d 0 0 cm /Desk Do Q" % (PHOTO_W, PHOTO_H),
            "q %s 0.80 g %.4f 0 %.4f 1 re f Q" % (m, sx, strip_w / pwid),
            "q %s /Page Do Q" % m,
            "q %s /Shade Do Q" % m,
            "q %d 0 0 %d 0 0 cm /Light Do Q" % (PHOTO_W, PHOTO_H),
        ]
        with open(os.path.join(folder, "photo-%d.txt" % n), "w", newline="\n") as out:
            out.write("\n".join(lines) + "\n")
    print("page files of", pages, "photographed pages written")


def cmd_threshold(src, dst, level):
    data = open(src, "rb").read()
    # header of a binary PGM: P5 <width> <height> <max>
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    pos += 1
    width, height = int(fields[1]), int(fields[2])
    table = bytes(49 if v < level else 48 for v in range(256))     # "1" is black in a PBM
    row_bytes = (width + 7) // 8
    pad = b"0" * (row_bytes * 8 - width)
    out = bytearray()
    for y in range(height):
        bits = data[pos + y * width:pos + (y + 1) * width].translate(table) + pad
        out += int(bits, 2).to_bytes(row_bytes, "big")
    with open(dst, "wb") as f:
        f.write(b"P4\n%d %d\n" % (width, height))
        f.write(out)


# ---------------------------------------------------------------- text layer of a DjVu page
def quote(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def cmd_djvutxt(xml_path, dpi, prefix):
    k = float(dpi) / 72.0
    picture_pages = []
    for number, page in enumerate(ET.parse(xml_path).getroot().iter("page"), 1):
        width, height = float(page.get("width")), float(page.get("height"))
        W, H = int(round(width * k)), int(round(height * k))
        if page.find(".//image") is not None:
            picture_pages.append(number)

        def box(x0, y0, x1, y1):
            # MuPDF: points, y down; DjVu: pixels, y up
            return "%d %d %d %d" % (max(0, int(x0 * k)), max(0, H - int(math.ceil(y1 * k))),
                                    min(W, int(math.ceil(x1 * k))), min(H, H - int(y0 * k)))
        groups = []        # (class, [blocks]): consecutive blocks of one column
        for block in page.iter("block"):
            lines = []
            for line in block.iter("line"):
                words, chars = [], []
                for ch in list(line.iter("char")) + [None]:
                    c = ch.get("c") if ch is not None else " "
                    if c is not None and c.strip():
                        q = [float(v) for v in ch.get("quad").split()]
                        chars.append((c, min(q[0::2]), min(q[1::2]), max(q[0::2]), max(q[1::2])))
                    elif chars:
                        words.append(("".join(t[0] for t in chars), min(t[1] for t in chars), min(t[2] for t in chars),
                                      max(t[3] for t in chars), max(t[4] for t in chars)))
                        chars = []
                if words:
                    lines.append(words)
            if not lines:
                continue
            x0 = min(w[1] for l in lines for w in l)
            x1 = max(w[3] for l in lines for w in l)
            kind = "L" if x1 <= width * 0.52 else "R" if x0 >= width * 0.48 else "F"
            if groups and groups[-1][0] == kind:
                groups[-1][1].append(lines)
            else:
                groups.append((kind, [lines]))
        out = ["(page 0 0 %d %d" % (W, H)]
        for kind, blocks in groups:
            flat = [w for lines in blocks for l in lines for w in l]
            out.append(" (column %s" % box(min(w[1] for w in flat), min(w[2] for w in flat),
                                            max(w[3] for w in flat), max(w[4] for w in flat)))
            for lines in blocks:
                bw = [w for l in lines for w in l]
                bbox = box(min(w[1] for w in bw), min(w[2] for w in bw), max(w[3] for w in bw), max(w[4] for w in bw))
                out.append("  (region %s\n   (para %s" % (bbox, bbox))
                for l in lines:
                    out.append("    (line %s" % box(min(w[1] for w in l), min(w[2] for w in l),
                                                    max(w[3] for w in l), max(w[4] for w in l)))
                    for w in l:
                        out.append("     (word %s %s)" % (box(w[1], w[2], w[3], w[4]), quote(w[0])))
                    out[-1] += ")"
                out[-1] += "))"
            out[-1] += ")"
        out[-1] += ")"
        with open("%s-%d.txt" % (prefix, number), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(out) + "\n")
    print(" ".join(str(n) for n in picture_pages))


if __name__ == "__main__":
    command = sys.argv[1]
    if command == "html":
        cmd_html(sys.argv[2])
    elif command == "compose":
        cmd_compose(sys.argv[2], int(sys.argv[3]))
    elif command == "threshold":
        cmd_threshold(sys.argv[2], sys.argv[3], int(sys.argv[4]))
    elif command == "djvutxt":
        cmd_djvutxt(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        sys.exit("unknown command " + command)
