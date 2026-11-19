"""A PDF to check the translator and the original view: a preface whose pages are numbered with
Roman numerals (the number alone at the bottom of a page, on one page at the top), then chapters
numbered with digits. None of the numbers may become a heading of the book. The file names its
title and its author and has contents of three entries, so the status line, "Book info", the
contents and the chapter marks of the page bar have something to show.

python3 make_roman_pdf.py <out.pdf>
"""
import sys


def text(x, y, s, size=11):
    return "BT /F1 %d Tf %d %d Td (%s) Tj ET\n" % (size, x, y, s)


PARA = ["The page holds a few paragraphs of plain words, each of them long enough to be a paragraph.",
        "A second line follows the first one so that the library sees a block of text and not a title.",
        "The third line ends the paragraph with a full stop, as the last line of a paragraph does."]


def body(top, paragraphs):
    c = ""
    y = top
    for p in range(paragraphs):
        for line in PARA:
            c += text(60, y, line)
            y -= 14
        y -= 12
    return c


def page(number, heading=None, number_at_top=False):
    c = ""
    top = 760
    if heading:
        c += text(60, 760, heading, 18)
        top = 725
    c += body(top, 12)
    # the page number alone, centred, in the size of the text
    c += text(275, 790 if number_at_top else 40, number, 10)
    return c


def build(pages, outline, title, author):
    """outline: (title, page index) of the entries of the contents."""
    objects = []
    n = len(pages)
    first_outline = 4 + 2 * n          # the object of the contents, its entries follow
    info = first_outline + 1 + len(outline)
    kids = " ".join("%d 0 R" % (4 + 2 * i) for i in range(n))
    objects.append("<< /Type /Catalog /Pages 2 0 R /Outlines %d 0 R /PageMode /UseOutlines >>" % first_outline)
    objects.append("<< /Type /Pages /Kids [%s] /Count %d >>" % (kids, n))
    objects.append("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    for i, content in enumerate(pages):
        objects.append("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 560 820] /Contents %d 0 R "
                       "/Resources << /Font << /F1 3 0 R >> >> >>" % (5 + 2 * i))
        objects.append("<< /Length %d >>\nstream\n%sendstream" % (len(content), content))
    objects.append("<< /Type /Outlines /First %d 0 R /Last %d 0 R /Count %d >>"
                   % (first_outline + 1, first_outline + len(outline), len(outline)))
    for k, (name, at) in enumerate(outline):
        item = "<< /Title (%s) /Parent %d 0 R /Dest [%d 0 R /Fit]" % (name, first_outline, 4 + 2 * at)
        if k > 0:
            item += " /Prev %d 0 R" % (first_outline + k)
        if k + 1 < len(outline):
            item += " /Next %d 0 R" % (first_outline + k + 2)
        objects.append(item + " >>")
    objects.append("<< /Title (%s) /Author (%s) >>" % (title, author))
    out = "%PDF-1.4\n"
    offsets = []
    for k, obj in enumerate(objects):
        offsets.append(len(out))
        out += "%d 0 obj\n%s\nendobj\n" % (k + 1, obj)
    xref = len(out)
    out += "xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
    for off in offsets:
        out += "%010d 00000 n \n" % off
    out += "trailer\n<< /Size %d /Root 1 0 R /Info %d 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objects) + 1, info, xref)
    return out.encode("latin-1")


pages = [page("xv", "Preface"), page("xvi"), page("xvii", number_at_top=True), page("xviii"), page("xix"),
         page("xx"), page("1", "Chapter 1. The first chapter"), page("2"), page("3"),
         page("4", "Chapter 2. The second chapter"), page("5"), page("6")]
outline = [("Preface", 0), ("Chapter 1. The first chapter", 6), ("Chapter 2. The second chapter", 9)]
open(sys.argv[1], "wb").write(build(pages, outline, "A test book with a preface", "CoolReader test files"))
print("written", sys.argv[1])
