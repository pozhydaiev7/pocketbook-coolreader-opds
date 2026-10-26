// Host-side test for opds::Feed on saved Flibusta feeds (samples/).
// Build and run from this directory: g++ -std=c++11 -Wall -I.. opdsfeed_test.cpp ../opdsfeed.cpp -o opdsfeed_test && ./opdsfeed_test
#include "opdsfeed.h"

#include <cstdio>
#include <fstream>
#include <sstream>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static std::string readFile(const std::string & path)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static opds::Feed load(const std::string & dir, const char * name, const char * url)
{
    opds::Feed feed;
    std::string error;
    bool ok = feed.parse(readFile(dir + "/" + name), url, error);
    if (!ok) printf("parse error in %s: %s\n", name, error.c_str());
    CHECK(ok);
    return feed;
}

static void dump(const opds::Feed & f, size_t maxEntries)
{
    printf("== %s  [%u entries] next=%s search=%s\n", f.title.c_str(), (unsigned)f.entries.size(),
           f.nextHref.c_str(), f.searchTemplate.c_str());
    for (size_t i = 0; i < f.entries.size() && i < maxEntries; i++) {
        const opds::Entry & e = f.entries[i];
        printf("  - %s | %s | nav=%s | formats=", e.title.c_str(), e.authorsText().c_str(), e.navigationHref.c_str());
        for (size_t k = 0; k < e.formats.size(); k++) printf("%s ", e.formats[k].label.c_str());
        printf("| related=%u\n", (unsigned)e.related.size());
    }
}

int main(int argc, char ** argv)
{
    std::string dir = argc > 1 ? argv[1] : "samples";

    // URL helpers
    CHECK(opds::resolveUrl("https://flibusta.is/opds", "/opds/new") == "https://flibusta.is/opds/new");
    CHECK(opds::resolveUrl("https://flibusta.is/opds/new/0/new", "1/new") == "https://flibusta.is/opds/new/0/1/new");
    CHECK(opds::resolveUrl("https://flibusta.is/opds?x=1", "?y=2") == "https://flibusta.is/opds?y=2");
    CHECK(opds::resolveUrl("https://flibusta.is", "a") == "https://flibusta.is/a");
    CHECK(opds::resolveUrl("https://a.b/c", "http://x.y/z") == "http://x.y/z");
    CHECK(opds::resolveUrl("https://a.b/c", "//x.y/z") == "https://x.y/z");
    CHECK(opds::urlEncode("пелевин") == "%D0%BF%D0%B5%D0%BB%D0%B5%D0%B2%D0%B8%D0%BD");
    CHECK(opds::makeSearchUrl("https://flibusta.is/opds/search?searchTerm={searchTerms}", " a b ")
          == "https://flibusta.is/opds/search?searchTerm=a%20b");
    CHECK(opds::makeSearchUrl("https://h/s?q={searchTerms}&p={startPage?}", "x") == "https://h/s?q=x&p=");

    // Root: 5 navigation entries + search template
    opds::Feed root = load(dir, "root.xml", "https://flibusta.is/opds");
    dump(root, 10);
    CHECK(root.entries.size() == 5);
    CHECK(root.searchTemplate == "https://flibusta.is/opds/search?searchTerm={searchTerms}");
    CHECK(root.entries.size() > 0 && root.entries[0].navigationHref == "https://flibusta.is/opds/new");
    CHECK(root.entries.size() > 0 && !root.entries[0].isBook());

    // Search landing page: two navigation entries with &amp; decoded in href
    opds::Feed search = load(dir, "search.xml", "https://flibusta.is/opds/search?searchTerm=x");
    dump(search, 10);
    CHECK(search.entries.size() == 2);
    CHECK(search.entries.size() == 2 && search.entries[1].navigationHref.find("searchType=books&searchTerm=") != std::string::npos);

    // Books page: acquisition links, authors, related links, next page
    opds::Feed books = load(dir, "books.xml", "https://flibusta.is/opds/new/0/new");
    dump(books, 5);
    CHECK(!books.entries.empty());
    CHECK(books.nextHref == "https://flibusta.is/opds/new/1/new/");
    if (!books.entries.empty()) {
        const opds::Entry & b = books.entries[0];
        CHECK(b.isBook());
        CHECK(b.navigationHref.empty());
        CHECK(b.title == "Мастер");
        CHECK(b.authorsText() == "Шимохин Дмитрий");
        CHECK(b.formats.size() >= 3 && b.formats[0].label == "FB2" && b.formats[0].extension == "fb2.zip");
        CHECK(b.formats.size() >= 1 && b.formats[0].href == "https://flibusta.is/b/893694/fb2");
        CHECK(b.related.size() == 3);
        CHECK(b.summary.find("Статус и золото") == 0);
        CHECK(b.summary.find("&lt;") == std::string::npos && b.summary.find("<p") == std::string::npos);
        CHECK(opds::makeBookFileName(b, b.formats[0]) == "Шимохин Дмитрий - Мастер.fb2.zip");
        printf("summary: %.200s\n", b.summary.c_str());
    }
    size_t bookCount = 0;
    for (size_t i = 0; i < books.entries.size(); i++) if (books.entries[i].isBook()) bookCount++;
    CHECK(bookCount == books.entries.size());
    size_t cbz = 0;
    for (size_t i = 0; i < books.entries.size(); i++)
        if (!books.entries[i].canDownload() && !books.entries[i].unsupportedTypes.empty()
                && books.entries[i].unsupportedTypes[0] == "application/cbz") cbz++;
    CHECK(cbz == 5);

    opds::Feed sb = load(dir, "search_books.xml", "https://flibusta.is/opds/search?searchType=books&searchTerm=x");
    dump(sb, 3);
    CHECK(!sb.entries.empty() && sb.entries[0].isBook());

    // File name sanitizing and UTF-8 safe truncation
    opds::Entry e; e.title = "A/B: \"C\"?"; e.authors.push_back("X");
    opds::Acquisition a; a.extension = "epub";
    CHECK(opds::makeBookFileName(e, a) == "X - A_B_ _C__.epub");
    opds::Entry longE; for (int i = 0; i < 100; i++) longE.title += "ж";
    std::string ln = opds::makeBookFileName(longE, a, 11);
    CHECK(ln == "жжжжж.epub");

    // Error handling: HTML page instead of a feed
    opds::Feed bad; std::string err;
    CHECK(!bad.parse("<!DOCTYPE html><html><body>blocked</body></html>", "https://x/", err));
    printf("expected error: %s\n", err.c_str());
    CHECK(!bad.parse("", "https://x/", err));

    printf(failures ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
