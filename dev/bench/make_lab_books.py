#!/usr/bin/env python3
"""Laboratory books of the bench: every element the engine lays out, in one FB2 and one EPUB.

make_lab_books.py <output folder>   ->  lab.fb2, lab.epub

The books of real life show what readers meet; these two show the rest: tables with merged
cells, lists, footnotes, poems, citations, pictures (inline, block, floating), the kinds of CSS
selectors, deep nesting, right-to-left text, preformatted text, text-transform. The text is made
by a fixed generator: the same files at every run.
"""
import base64
import struct
import sys
import zipfile
import zlib

RU = ("время человек год дело жизнь день рука работа слово место вопрос лицо глаз страна друг сторона дом "
      "случай голова ребёнок сила конец вид система часть город отношение женщина деньги земля машина вода "
      "отец проблема час право нога решение дверь образ история власть закон война голос книга возможность "
      "результат ночь стол имя область достопримечательность взаимопонимание электрификация "
      "сельскохозяйственный высокопревосходительство человеконенавистничество железнодорожный "
      "и в не на я что он с как а то все она так его но да ты к у же вы за бы по только её мне было вот от").split()
EN = ("the of and to in is that for it as was with be by on not he this are or his from at which but have an "
      "had they you were their one all we can her has there been if more when will would who so no "
      "time people way water word work life world house night question government understanding "
      "internationalization counterrevolutionary extraordinary responsibility").split()


class Gen:
    def __init__(self, seed):
        self.s = seed

    def n(self, m):
        self.s = (self.s * 1103515245 + 12345) & 0x7FFFFFFF
        return (self.s >> 8) % m

    def sentence(self, words, lo=4, hi=18):
        k = lo + self.n(hi - lo)
        ws = [words[self.n(len(words))] for _ in range(k)]
        s = " ".join(ws)
        if self.n(5) == 0:
            s = s.replace(" ", ", ", 1)
        if self.n(7) == 0:
            s += " — " + words[self.n(len(words))]
        return s[0].upper() + s[1:] + ".!?"[self.n(10) // 4 if self.n(10) < 9 else 0]

    def para(self, words, lo=2, hi=8):
        return " ".join(self.sentence(words) for _ in range(lo + self.n(hi - lo)))


def png(w, h, fn):
    raw = b"".join(b"\x00" + bytes(fn(x, y) & 255 for x in range(w)) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def pictures():
    return {
        "cover.png": png(600, 800, lambda x, y: 255 if (x < 20 or y < 20 or x > 579 or y > 779) else (x * y // 97) % 200 + 30),
        "wide.png": png(900, 200, lambda x, y: (x // 6 + y) % 256),
        "tall.png": png(200, 700, lambda x, y: 40 if (x // 25 + y // 25) % 2 else 220),
        "small.png": png(48, 48, lambda x, y: 0 if (x - 24) ** 2 + (y - 24) ** 2 < 400 else 255),
        "photo.png": png(480, 360, lambda x, y: (x * 255 // 480 + y * 255 // 360) // 2),
    }


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def make_fb2(path):
    g = Gen(20261117)
    pics = pictures()
    out = []
    w = out.append
    w('<?xml version="1.0" encoding="utf-8"?>')
    w('<FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0" xmlns:l="http://www.w3.org/1999/xlink">')
    w('<description><title-info><genre>prose</genre><author><first-name>Лаборатория</first-name>'
      '<last-name>Стендова</last-name></author><book-title>Лабораторная книга FB2</book-title>'
      '<annotation><p>Все элементы FB2 в одной книге: <strong>таблицы</strong>, <emphasis>стихи</emphasis>, '
      'цитаты, сноски, картинки.</p><p>' + esc(g.para(RU)) + '</p></annotation>'
      '<coverpage><image l:href="#cover.png"/></coverpage><lang>ru</lang>'
      '<sequence name="Стенд" number="1"/></title-info>'
      '<document-info><author><nickname>bench</nickname></author><date>2026-11-17</date><id>lab-fb2</id>'
      '<version>1.0</version></document-info></description>')
    w('<body>')
    w('<title><p>Лабораторная книга</p><p>Все элементы</p></title>')
    w('<epigraph><p>' + esc(g.sentence(RU)) + '</p><p>' + esc(g.sentence(RU)) + '</p>'
      '<text-author>Автор эпиграфа</text-author></epigraph>')
    notes = []

    def note(text):
        notes.append(text)
        return '<a l:href="#n%d" type="note">[%d]</a>' % (len(notes), len(notes))

    def inline(words):
        s = esc(g.sentence(words))
        parts = s.split(" ")
        k = g.n(8)
        if len(parts) > 4:
            i = 1 + g.n(len(parts) - 3)
            tag = ["emphasis", "strong", "strikethrough", "code", "sub", "sup", "emphasis", "strong"][k]
            parts[i] = "<%s>%s</%s>" % (tag, parts[i], tag)
            if k == 6:
                parts[i + 1] = "<strong><emphasis>%s</emphasis></strong>" % parts[i + 1]
        return " ".join(parts)

    for ch in range(1, 9):
        w('<section id="ch%d"><title><p>Глава %d. %s</p></title>' % (ch, ch, esc(g.sentence(RU, 2, 5))))
        if ch % 3 == 1:
            w('<epigraph><p>' + esc(g.sentence(RU)) + '</p><text-author>Эпиграф главы</text-author></epigraph>')
        if ch == 2:
            w('<annotation><p>' + esc(g.para(RU, 1, 3)) + '</p></annotation>')
        for sec in range(1, 4):
            w('<section><title><p>%d.%d %s</p></title>' % (ch, sec, esc(g.sentence(RU, 2, 4))))
            for p in range(6 + g.n(10)):
                kind = g.n(24)
                if kind == 0:
                    w('<subtitle>* * *</subtitle>')
                elif kind == 1:
                    w('<empty-line/>')
                elif kind == 2:
                    w('<cite><p>' + inline(RU) + '</p><p>' + esc(g.para(RU, 1, 3)) + '</p>'
                      '<subtitle>Подзаголовок цитаты</subtitle><text-author>Автор цитаты</text-author></cite>')
                elif kind == 3:
                    w('<poem><title><p>Стихотворение</p></title><epigraph><p>' + esc(g.sentence(RU, 3, 6)) + '</p></epigraph>')
                    for st in range(2 + g.n(3)):
                        w('<stanza>' + "".join('<v>' + esc(g.sentence(RU, 3, 7)) + '</v>' for _ in range(4)) + '</stanza>')
                    w('<text-author>Поэт</text-author><date>1901</date></poem>')
                elif kind == 4:
                    w('<p>' + inline(RU) + ' ' + note(g.para(RU, 1, 3)) + ' ' + inline(RU) + ' '
                      + note(g.sentence(RU)) + '</p>')
                elif kind == 5:
                    w('<image l:href="#%s"/>' % ["wide.png", "tall.png", "photo.png"][g.n(3)])
                elif kind == 6:
                    w('<p>' + inline(RU) + ' <image l:href="#small.png"/> ' + inline(RU) + '</p>')
                elif kind == 7:
                    rows = 3 + g.n(5)
                    cols = 2 + g.n(4)
                    w('<table>')
                    w('<tr>' + "".join('<th align="center">Столбец %d</th>' % (c + 1) for c in range(cols)) + '</tr>')
                    for r in range(rows):
                        cells = []
                        c = 0
                        while c < cols:
                            if r == 1 and c == 0 and cols > 2:
                                cells.append('<td colspan="2" align="center">' + esc(g.sentence(RU, 2, 5)) + '</td>')
                                c += 2
                                continue
                            if r == 2 and c == cols - 1 and rows > 4:
                                cells.append('<td rowspan="2" valign="middle">' + esc(g.sentence(RU, 2, 6)) + '</td>')
                                c += 1
                                continue
                            if r == 3 and c == cols - 1 and rows > 4:
                                c += 1
                                continue
                            al = ["left", "right", "center"][g.n(3)]
                            cells.append('<td align="%s">%s</td>' % (al, inline(RU) if g.n(3) else str(g.n(100000))))
                            c += 1
                        w('<tr>' + "".join(cells) + '</tr>')
                    w('</table>')
                elif kind == 8:
                    w('<p>' + esc(g.para(EN, 2, 6)) + '</p>')
                elif kind == 9:
                    w('<p>— ' + esc(g.sentence(RU)) + ' — ' + esc(g.sentence(RU, 2, 5))
                      + ' — «' + esc(g.sentence(RU, 3, 6)) + '».</p>')
                elif kind == 10:
                    w('<p>Ссылка: <a l:href="https://example.org/">' + esc(g.sentence(RU, 2, 4)) + '</a>, '
                      'внутренняя: <a l:href="#ch1">к первой главе</a>. Неразрывные пробелы тут, '
                      'мягкий­перенос и длинноесловобезпробеловкотороенепомещаетсявстрокуцеликомникогда.</p>')
                else:
                    w('<p>' + " ".join(inline(RU) for _ in range(1 + g.n(6))) + '</p>')
            if sec == 2:
                w('<section><title><p>Вложенный раздел</p></title><p>' + esc(g.para(RU)) + '</p>'
                  '<section><title><p>Ещё глубже</p></title><p>' + esc(g.para(RU)) + '</p></section></section>')
            w('</section>')
        w('</section>')
    w('</body>')
    w('<body name="notes"><title><p>Примечания</p></title>')
    for i, t in enumerate(notes):
        w('<section id="n%d"><title><p>%d</p></title><p>%s</p></section>' % (i + 1, i + 1, esc(t)))
    w('</body>')
    for name, data in pics.items():
        w('<binary id="%s" content-type="image/png">%s</binary>' % (name, base64.b64encode(data).decode()))
    w('</FictionBook>')
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))


CSS = """
body { margin: 0; }
h1 { text-align: center; page-break-before: always; font-size: 1.6em; margin: 1em 0 0.5em 0; }
h2 { text-align: left; font-size: 1.3em; margin-top: 1em; border-bottom: 1px solid black; }
h3 { font-style: italic; } h4 { text-transform: uppercase; letter-spacing: 0.1em; } h5 { font-variant: small-caps; } h6 { font-weight: normal; text-decoration: underline; }
p { text-indent: 1.5em; margin: 0; text-align: justify; }
h1 + p, h2 + p { text-indent: 0; }
h2 ~ p.far { margin-left: 2em; }
p.center { text-align: center; text-indent: 0; } p.right { text-align: right; } p.left { text-align: left; }
p.big { font-size: 140%; line-height: 1.6; } p.small { font-size: 0.75em; line-height: 1.1; }
p.upper { text-transform: uppercase; } p.lower { text-transform: lowercase; } p.cap { text-transform: capitalize; }
p.spaced { letter-spacing: 0.2em; word-spacing: 0.4em; } p.hang { text-indent: -2em; margin-left: 2em; }
p.pct { margin-left: 15%; margin-right: 10%; } p.px { margin: 12px 30px; padding: 6px; border: 2px solid #444; background-color: #ddd; }
p.first:first-child { font-weight: bold; }
p[title] { font-style: italic; } p[lang="en"] { hyphens: auto; }
div.box > p { color: #333; } div.box p span.mark { background-color: #bbb; }
div.deep div div div p { text-indent: 0; }
section.chapter div.wrap blockquote p { font-size: 0.9em; }
blockquote { margin: 1em 2em; font-style: italic; }
pre { white-space: pre; font-family: monospace; font-size: 0.8em; margin: 0.5em 0; }
p.prewrap { white-space: pre-wrap; } p.nowrap { white-space: nowrap; }
code { font-family: monospace; }
ul { list-style-type: disc; margin-left: 1.5em; } ul ul { list-style-type: circle; } ol.roman { list-style-type: upper-roman; } ol.alpha { list-style-type: lower-alpha; }
ol.inside { list-style-position: inside; }
dt { font-weight: bold; } dd { margin-left: 2em; }
table { border-collapse: collapse; margin: 0.5em auto; width: 100%; } table.narrow { width: 60%; } table.sep { border-collapse: separate; border-spacing: 4px; }
td, th { border: 1px solid black; padding: 3px 5px; vertical-align: top; } th { background-color: #ccc; text-align: center; }
td.num { text-align: right; } td.mid { vertical-align: middle; text-align: center; }
caption { font-style: italic; caption-side: top; }
img.block { display: block; margin: 0.5em auto; max-width: 100%; } img.half { width: 50%; } img.fixed { width: 120px; height: 90px; }
img.left { float: left; margin: 0 0.6em 0.3em 0; width: 30%; } img.right { float: right; margin: 0 0 0.3em 0.6em; width: 25%; }
div.floatbox { float: right; width: 35%; border: 1px solid black; padding: 4px; margin-left: 8px; font-size: 0.8em; }
div.clear { clear: both; }
span.ib { display: inline-block; border: 1px solid black; padding: 2px 6px; margin: 0 3px; }
span.sup { vertical-align: super; font-size: 0.7em; } span.sub { vertical-align: sub; font-size: 0.7em; }
p.note::before { content: "NB: "; font-weight: bold; } q::before { content: "\\00ab"; } q::after { content: "\\00bb"; }
aside { margin: 0.5em 1em; font-size: 0.85em; border-left: 3px solid black; padding-left: 0.5em; }
hr { margin: 1em 20%; }
div.pagebreak { page-break-after: always; }
p.keep { page-break-inside: avoid; } h3 { page-break-after: avoid; }
div.cover { text-align: center; margin: 0; padding: 0; }
.rtl { direction: rtl; text-align: right; }
"""


def make_epub(path):
    g = Gen(7102611)
    pics = pictures()
    chapters = []

    def page(title, body, lang="ru"):
        return ('<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE html>\n'
                '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" lang="%s">'
                '<head><title>%s</title><link rel="stylesheet" type="text/css" href="style.css"/></head>'
                '<body>%s</body></html>' % (lang, esc(title), body))

    def ps(n, words=RU, cls=""):
        c = ' class="%s"' % cls if cls else ""
        return "".join("<p%s>%s</p>" % (c, esc(g.para(words))) for _ in range(n))

    cover = ('<div class="cover"><svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
             'version="1.1" width="100%" height="100%" viewBox="0 0 600 800" preserveAspectRatio="xMidYMid meet">'
             '<image width="600" height="800" xlink:href="cover.png"/></svg></div>')
    chapters.append(("cover", "Обложка", page("Обложка", cover)))

    b = ['<section class="chapter"><h1>Текст и заголовки</h1>', ps(1)]
    for lv in range(2, 7):
        b.append("<h%d>Заголовок уровня %d: %s</h%d>" % (lv, lv, esc(g.sentence(RU, 2, 5)), lv))
        b.append(ps(2))
    for cls in ["center", "right", "left", "big", "small", "upper", "lower", "cap", "spaced", "hang", "pct", "px",
                "note", "keep", "far"]:
        b.append('<p class="%s">%s</p>' % (cls, esc(g.para(RU, 2, 4))))
    b.append('<p title="t">%s</p><p lang="en">%s</p>' % (esc(g.para(RU, 1, 3)), esc(g.para(EN, 3, 6))))
    b.append('<p>Вставки: <b>жирный</b>, <i>курсив</i>, <u>подчёркнутый</u>, <s>зачёркнутый</s>, <code>код()</code>, '
             'H<sub>2</sub>O, E=mc<sup>2</sup>, <span class="sup">верх</span>, <span class="sub">низ</span>, '
             '<small>мелкий</small>, <big>крупный</big>, <q>цитата в строке</q>, <span class="ib">блок в строке</span>, '
             '<span class="ib">ещё один</span>, <a href="https://example.org/">внешняя ссылка</a>, '
             '<a href="ch3.xhtml#t2">ссылка на таблицу</a>, перенос<br/>строки, неразрывный пробел, '
             'мягкий­перенос, длинноесловобезпробеловкотороенепомещаетсявстрокуцеликомникогда.</p>')
    b.append('<div class="box"><p class="first">%s</p><p>%s <span class="mark">отмеченное</span> %s</p></div>'
             % (esc(g.para(RU, 1, 3)), esc(g.sentence(RU)), esc(g.sentence(RU))))
    b.append('<div class="wrap"><blockquote><p>%s</p><p>%s</p></blockquote></div>' % (esc(g.para(RU)), esc(g.para(RU, 1, 3))))
    b.append('<hr/>' + ps(3) + '</section>')
    chapters.append(("ch1", "Текст и заголовки", page("Текст", "".join(b))))

    b = ['<section class="chapter"><h1>Списки</h1>', ps(1)]
    b.append('<ul>' + "".join('<li>%s</li>' % esc(g.sentence(RU)) for _ in range(4))
             + '<li>Вложенный:<ul>' + "".join('<li>%s</li>' % esc(g.para(RU, 1, 3)) for _ in range(3))
             + '<li><ol>' + "".join('<li>%s</li>' % esc(g.sentence(RU)) for _ in range(3)) + '</ol></li></ul></li></ul>')
    for cls in ["", "roman", "alpha", "inside"]:
        b.append('<ol class="%s">' % cls + "".join('<li><p>%s</p><p>%s</p></li>' % (esc(g.sentence(RU)), esc(g.sentence(RU)))
                                                 if i == 1 else '<li>%s</li>' % esc(g.para(RU, 1, 3)) for i in range(12)) + '</ol>')
    b.append('<ol start="98">' + "".join('<li>%s</li>' % esc(g.sentence(RU)) for _ in range(5)) + '</ol>')
    b.append('<dl>' + "".join('<dt>%s</dt><dd>%s</dd>' % (esc(g.sentence(RU, 1, 3)), esc(g.para(RU, 1, 3))) for _ in range(5)) + '</dl>')
    b.append(ps(2) + '</section>')
    chapters.append(("ch2", "Списки", page("Списки", "".join(b))))

    b = ['<section class="chapter"><h1>Таблицы</h1>', ps(1)]
    b.append('<table id="t1"><caption>Простая таблица</caption><thead><tr>'
             + "".join('<th>Столбец %d</th>' % c for c in range(1, 5)) + '</tr></thead><tbody>'
             + "".join('<tr><td>%s</td><td class="num">%d</td><td>%s</td><td class="num">%d,%02d</td></tr>'
                       % (esc(g.sentence(RU, 1, 4)), g.n(100000), esc(g.sentence(RU, 2, 8)), g.n(1000), g.n(100)) for _ in range(14))
             + '</tbody><tfoot><tr><td colspan="3">Итого</td><td class="num">12345,00</td></tr></tfoot></table>')
    b.append('<table id="t2" class="narrow"><tr><th rowspan="2">Объединение</th><th colspan="3">Три столбца</th></tr>'
             '<tr><th>A</th><th>B</th><th>C</th></tr>'
             '<tr><td rowspan="3" class="mid">%s</td><td>1</td><td colspan="2">%s</td></tr>'
             '<tr><td>2</td><td>%s</td><td rowspan="2">%s</td></tr><tr><td>3</td><td>%s</td></tr></table>'
             % (esc(g.sentence(RU, 2, 5)), esc(g.sentence(RU, 2, 5)), esc(g.sentence(RU, 2, 6)), esc(g.para(RU, 1, 3)),
                esc(g.sentence(RU, 1, 3))))
    b.append('<table class="sep"><tr><td><p>%s</p><p>%s</p></td><td><ul><li>в ячейке</li><li>список</li></ul></td>'
             '<td><img class="fixed" src="photo.png" alt=""/></td></tr><tr><td colspan="3"><table><tr><td>вложенная</td>'
             '<td>таблица</td></tr><tr><td>%s</td><td>%s</td></tr></table></td></tr></table>'
             % (esc(g.sentence(RU)), esc(g.sentence(RU)), esc(g.sentence(RU, 1, 4)), esc(g.sentence(RU, 1, 4))))
    b.append('<table>' + "".join('<tr>' + "".join('<td>%s</td>' % esc(g.para(RU, 1, 4)) for _ in range(3)) + '</tr>'
                                 for _ in range(30)) + '</table>')
    b.append('<div><tr><td>ячейка без таблицы</td><td>%s</td></tr></div>' % esc(g.sentence(RU)))
    b.append(ps(2) + '</section>')
    chapters.append(("ch3", "Таблицы", page("Таблицы", "".join(b))))

    b = ['<section class="chapter"><h1>Картинки и обтекание</h1>', ps(1)]
    b.append('<img class="block" src="wide.png" alt="широкая"/>' + ps(1))
    b.append('<p><img class="left" src="photo.png" alt=""/>%s</p>' % esc(" ".join(g.para(RU) for _ in range(3))) + ps(1))
    b.append('<p><img class="right" src="tall.png" alt=""/>%s</p>' % esc(" ".join(g.para(RU) for _ in range(4))))
    b.append('<div class="clear"></div><div class="floatbox"><p>%s</p></div>' % esc(g.para(RU, 1, 3)) + ps(3))
    b.append('<div class="clear"></div><p>В строке <img src="small.png" alt=""/> значок, и ещё <img src="small.png" '
             'alt="" style="width:1em;height:1em"/> по высоте строки. %s</p>' % esc(g.para(RU)))
    b.append('<img class="block half" src="photo.png" alt=""/><img class="block" src="tall.png" alt=""/>')
    b.append('<p class="center"><img src="missing.png" alt="картинки нет"/></p>')
    b.append('<figure><img class="block" src="photo.png" alt=""/><figcaption>Подпись к рисунку: %s</figcaption></figure>'
             % esc(g.sentence(RU)))
    b.append(ps(2) + '</section>')
    chapters.append(("ch4", "Картинки", page("Картинки", "".join(b))))

    b = ['<section class="chapter"><h1>Сноски, код, направление письма</h1>', ps(1)]
    for i in range(1, 9):
        b.append('<p>%s<a epub:type="noteref" href="#fn%d"><sup>%d</sup></a> %s</p>'
                 % (esc(g.sentence(RU)), i, i, esc(g.para(RU, 1, 3))))
    b.append("".join('<aside epub:type="footnote" id="fn%d"><p>%d. %s</p></aside>' % (i, i, esc(g.para(RU, 1, 3)))
                     for i in range(1, 9)))
    b.append('<pre>def layout(book):\n    for page in book:\n        draw(page)      # табуляция\tи пробелы\n'
             '    return   "%s"\n</pre>' % esc(g.sentence(EN, 3, 6)))
    b.append('<p class="prewrap">Строка   с   пробелами\nи переводом строки. %s</p>' % esc(g.sentence(RU)))
    b.append('<p class="nowrap">%s</p>' % esc(g.sentence(RU, 4, 7)))
    b.append('<p class="rtl" dir="rtl" lang="he">שלום עולם, זה טקסט '
             'מימין לשמאל 123 abc.</p>')
    b.append('<p>Смешанный: русский مرحبا بالعالم english '
             '中文文本测试 ελληνικά.</p>')
    deep = '<p>%s</p>' % esc(g.para(RU, 1, 3))
    for i in range(72):
        deep = '<div>%s</div>' % deep
    b.append('<div class="deep">%s</div>' % deep)
    b.append('<div class="deep"><div><div><div><p>%s</p><span>%s <em>вложенный <strong>в три <u>слоя</u></strong></em></span>'
             '</div></div></div></div>' % (esc(g.para(RU, 1, 3)), esc(g.sentence(RU))))
    b.append('<div class="pagebreak"></div>' + ps(2) + '</section>')
    chapters.append(("ch5", "Сноски и код", page("Сноски", "".join(b))))

    for n in range(6, 12):
        b = ['<section class="chapter"><h1>Глава %d. %s</h1>' % (n, esc(g.sentence(RU, 2, 5)))]
        for part in range(4):
            b.append('<h2>%s</h2>' % esc(g.sentence(RU, 2, 5)) + ps(10 + g.n(12), EN if n == 8 else RU))
            if part == 1:
                b.append('<div class="wrap"><blockquote><p>%s</p></blockquote></div>' % esc(g.para(RU)))
            if part == 2:
                b.append('<p class="center">* * *</p>')
        b.append('</section>')
        chapters.append(("ch%d" % n, "Глава %d" % n, page("Глава %d" % n, "".join(b), "en" if n == 8 else "ru")))

    manifest = "".join('<item id="%s" href="%s.xhtml" media-type="application/xhtml+xml"/>' % (c[0], c[0]) for c in chapters)
    manifest += "".join('<item id="img%d" href="%s" media-type="image/png"%s/>'
                        % (i, n, ' properties="cover-image"' if n == "cover.png" else "") for i, n in enumerate(pics))
    spine = "".join('<itemref idref="%s"/>' % c[0] for c in chapters)
    opf = ('<?xml version="1.0" encoding="utf-8"?><package xmlns="http://www.idpf.org/2007/opf" version="3.0" '
           'unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">lab-epub'
           '</dc:identifier><dc:title>Лабораторная книга EPUB</dc:title><dc:creator>Лаборатория Стендова</dc:creator>'
           '<dc:language>ru</dc:language><meta name="cover" content="img0"/></metadata><manifest>'
           '<item id="css" href="style.css" media-type="text/css"/><item id="ncx" href="toc.ncx" '
           'media-type="application/x-dtbncx+xml"/><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" '
           'properties="nav"/>%s</manifest><spine toc="ncx">%s</spine></package>' % (manifest, spine))
    ncx = ('<?xml version="1.0" encoding="utf-8"?><ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">'
           '<head><meta name="dtb:uid" content="lab-epub"/></head><docTitle><text>Лабораторная книга EPUB</text></docTitle>'
           '<navMap>%s</navMap></ncx>'
           % "".join('<navPoint id="np%d" playOrder="%d"><navLabel><text>%s</text></navLabel><content src="%s.xhtml"/>'
                     '</navPoint>' % (i, i + 1, esc(c[1]), c[0]) for i, c in enumerate(chapters)))
    nav = page("Оглавление", '<nav epub:type="toc"><ol>%s</ol></nav>'
               % "".join('<li><a href="%s.xhtml">%s</a></li>' % (c[0], esc(c[1])) for c in chapters))
    with zipfile.ZipFile(path, "w") as z:
        stamp = (2026, 11, 17, 0, 0, 0)

        def add(name, data, method=zipfile.ZIP_DEFLATED):
            info = zipfile.ZipInfo(name, stamp)
            info.compress_type = method
            z.writestr(info, data)
        add("mimetype", "application/epub+zip", zipfile.ZIP_STORED)
        add("META-INF/container.xml", '<?xml version="1.0"?><container version="1.0" '
            'xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="OEBPS/content.opf" '
            'media-type="application/oebps-package+xml"/></rootfiles></container>')
        add("OEBPS/content.opf", opf)
        add("OEBPS/toc.ncx", ncx)
        add("OEBPS/nav.xhtml", nav)
        add("OEBPS/style.css", CSS)
        for c in chapters:
            add("OEBPS/%s.xhtml" % c[0], c[2])
        for n, d in pics.items():
            add("OEBPS/" + n, d)


out = sys.argv[1]
make_fb2(out + "/lab.fb2")
make_epub(out + "/lab.epub")
print("written: lab.fb2, lab.epub in", out)
