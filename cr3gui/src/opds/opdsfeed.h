// OPDS (Atom) catalog feed model and parser for the CoolReader PocketBook port.
// Self-contained C++11 (gcc 4.8 compatible): no crengine or inkview dependencies,
// so it can be unit-tested on the build host.
#ifndef OPDSFEED_H
#define OPDSFEED_H

#include <string>
#include <vector>

namespace opds {

struct Link {
    std::string href;   // absolute URL after OpdsFeed::parse()
    std::string rel;
    std::string type;
    std::string title;
};

// Downloadable book format offered by an entry.
struct Acquisition {
    std::string href;       // absolute URL
    std::string type;       // MIME type from the feed
    std::string extension;  // file extension to save with, e.g. "fb2.zip"
    std::string label;      // short label for menus, e.g. "FB2"
    int priority;           // lower = preferred
};

struct Entry {
    std::string id;
    std::string title;
    std::vector<std::string> authors;
    std::string summary;               // plain text, HTML stripped
    std::string navigationHref;        // absolute URL of a sub-catalog, empty for books
    std::vector<Link> related;         // "all books of the author/series" links
    std::vector<Acquisition> formats;  // sorted by priority, only formats CoolReader can open
    std::vector<std::string> unsupportedTypes; // MIME types of offered formats CoolReader cannot open

    bool isBook() const { return !formats.empty() || !unsupportedTypes.empty(); }
    bool canDownload() const { return !formats.empty(); }
    std::string authorsText() const;   // "A, B"
};

struct Feed {
    std::string title;
    std::string baseUrl;       // URL the feed was loaded from
    std::string nextHref;      // absolute URL of the next page, may be empty
    std::string searchTemplate;// absolute URL with {searchTerms}, may be empty
    std::vector<Entry> entries;

    // Parses an Atom/OPDS document. Returns false and fills error on malformed input.
    bool parse(const std::string & xml, const std::string & url, std::string & error);
};

// Helpers, public for the UI layer and tests.
std::string resolveUrl(const std::string & base, const std::string & href);
std::string urlEncode(const std::string & utf8);
std::string makeSearchUrl(const std::string & searchTemplate, const std::string & terms);
// "Author - Title.ext" with characters forbidden on FAT/Linux replaced, UTF-8 safe length limit.
std::string makeBookFileName(const Entry & e, const Acquisition & a, size_t maxBytes = 120);
// Fills extension/label/priority from the MIME type; returns false for formats CoolReader cannot open.
bool classifyFormat(const std::string & mimeType, Acquisition & a);

} // namespace opds

#endif // OPDSFEED_H
