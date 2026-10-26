// OPDS (Atom) catalog feed parser for the CoolReader PocketBook port.
// Self-contained C++11 (gcc 4.8 compatible). See opdsfeed.h.
#include "opdsfeed.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>

namespace opds {

namespace {

// ---------------------------------------------------------------- strings

bool startsWith(const std::string & s, const char * prefix)
{
    size_t n = strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

bool contains(const std::string & s, const char * part)
{
    return s.find(part) != std::string::npos;
}

std::string toLowerAscii(std::string s)
{
    for (size_t i = 0; i < s.size(); i++)
        if (s[i] >= 'A' && s[i] <= 'Z')
            s[i] = (char)(s[i] - 'A' + 'a');
    return s;
}

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string trim(const std::string & s)
{
    size_t b = 0, e = s.size();
    while (b < e && isSpace(s[b])) b++;
    while (e > b && isSpace(s[e - 1])) e--;
    return s.substr(b, e - b);
}

// Collapses runs of whitespace into one space, keeps single '\n' as paragraph breaks.
std::string collapseSpaces(const std::string & s)
{
    std::string out;
    out.reserve(s.size());
    bool pendingSpace = false, pendingBreak = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '\n') { pendingBreak = true; continue; }
        if (isSpace(c)) { pendingSpace = true; continue; }
        if (!out.empty()) {
            if (pendingBreak) out += '\n';
            else if (pendingSpace) out += ' ';
        }
        pendingSpace = pendingBreak = false;
        out += c;
    }
    return out;
}

void appendUtf8(std::string & out, unsigned long cp)
{
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

// Decodes XML character references and the predefined entities (plus &nbsp; for HTML text).
std::string decodeEntities(const std::string & s)
{
    if (s.find('&') == std::string::npos)
        return s;
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '&') { out += s[i]; continue; }
        size_t semi = s.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 10) { out += s[i]; continue; }
        std::string name = s.substr(i + 1, semi - i - 1);
        if (name == "amp") out += '&';
        else if (name == "lt") out += '<';
        else if (name == "gt") out += '>';
        else if (name == "quot") out += '"';
        else if (name == "apos") out += '\'';
        else if (name == "nbsp") out += ' ';
        else if (!name.empty() && name[0] == '#') {
            unsigned long cp = (name.size() > 1 && (name[1] == 'x' || name[1] == 'X'))
                ? strtoul(name.c_str() + 2, NULL, 16)
                : strtoul(name.c_str() + 1, NULL, 10);
            if (cp == 0) { out += s.substr(i, semi - i + 1); }
            else appendUtf8(out, cp);
        } else {
            out += s.substr(i, semi - i + 1);
        }
        i = semi;
    }
    return out;
}

// Turns an HTML fragment into plain text: block tags become line breaks, other tags are dropped.
std::string htmlToText(const std::string & html)
{
    std::string out;
    out.reserve(html.size());
    for (size_t i = 0; i < html.size(); i++) {
        if (html[i] != '<') { out += html[i]; continue; }
        size_t end = html.find('>', i + 1);
        if (end == std::string::npos) break;
        std::string tag = toLowerAscii(html.substr(i + 1, end - i - 1));
        if (startsWith(tag, "br") || startsWith(tag, "/p") || startsWith(tag, "p ") || tag == "p"
                || startsWith(tag, "/div") || startsWith(tag, "li"))
            out += '\n';
        i = end;
    }
    return collapseSpaces(decodeEntities(out));
}

// ---------------------------------------------------------------- tiny XML DOM

struct Node {
    std::string name;   // local name, namespace prefix removed
    std::map<std::string, std::string> attrs; // local attribute names
    std::string text;   // concatenated direct text content (entities decoded)
    std::vector<Node> children;

    const Node * child(const char * n) const
    {
        for (size_t i = 0; i < children.size(); i++)
            if (children[i].name == n) return &children[i];
        return NULL;
    }
    std::string childText(const char * n) const
    {
        const Node * c = child(n);
        return c ? trim(c->text) : std::string();
    }
    std::string attr(const char * n) const
    {
        std::map<std::string, std::string>::const_iterator it = attrs.find(n);
        return it == attrs.end() ? std::string() : it->second;
    }
};

std::string localName(const std::string & qname)
{
    size_t colon = qname.find(':');
    return colon == std::string::npos ? qname : qname.substr(colon + 1);
}

class XmlParser {
public:
    XmlParser(const std::string & src) : s(src), pos(0) {}

    bool parse(Node & root, std::string & error)
    {
        std::vector<Node *> stack;
        Node docNode;
        stack.push_back(&docNode);
        while (pos < s.size()) {
            if (s[pos] != '<') {
                size_t lt = s.find('<', pos);
                if (lt == std::string::npos) lt = s.size();
                stack.back()->text += decodeEntities(s.substr(pos, lt - pos));
                pos = lt;
                continue;
            }
            if (s.compare(pos, 4, "<!--") == 0) {
                if (!skipPast("-->")) break;
            } else if (s.compare(pos, 9, "<![CDATA[") == 0) {
                size_t end = s.find("]]>", pos + 9);
                if (end == std::string::npos) { error = "Unterminated CDATA"; return false; }
                stack.back()->text += s.substr(pos + 9, end - pos - 9);
                pos = end + 3;
            } else if (s.compare(pos, 2, "<?") == 0 || s.compare(pos, 2, "<!") == 0) {
                if (!skipPast(">")) break;
            } else if (s.compare(pos, 2, "</") == 0) {
                size_t gt = s.find('>', pos);
                if (gt == std::string::npos) { error = "Unterminated end tag"; return false; }
                std::string name = localName(trim(s.substr(pos + 2, gt - pos - 2)));
                pos = gt + 1;
                // Lenient: pop up to the matching element, ignore stray end tags.
                for (size_t k = stack.size(); k > 1; k--) {
                    if (stack[k - 1]->name == name) {
                        stack.resize(k - 1);
                        break;
                    }
                }
            } else {
                Node node;
                bool selfClosing = false;
                if (!parseStartTag(node, selfClosing, error)) return false;
                stack.back()->children.push_back(node);
                if (!selfClosing)
                    stack.push_back(&stack.back()->children.back());
            }
        }
        for (size_t i = 0; i < docNode.children.size(); i++) {
            root = docNode.children[i];
            return true;
        }
        error = "No root element";
        return false;
    }

private:
    const std::string & s;
    size_t pos;

    bool skipPast(const char * marker)
    {
        size_t end = s.find(marker, pos);
        if (end == std::string::npos) { pos = s.size(); return false; }
        pos = end + strlen(marker);
        return true;
    }

    bool parseStartTag(Node & node, bool & selfClosing, std::string & error)
    {
        pos++; // '<'
        size_t b = pos;
        while (pos < s.size() && !isSpace(s[pos]) && s[pos] != '>' && s[pos] != '/') pos++;
        node.name = localName(s.substr(b, pos - b));
        while (pos < s.size()) {
            while (pos < s.size() && isSpace(s[pos])) pos++;
            if (pos >= s.size()) break;
            if (s[pos] == '>') { pos++; return true; }
            if (s[pos] == '/') {
                selfClosing = true;
                size_t gt = s.find('>', pos);
                if (gt == std::string::npos) break;
                pos = gt + 1;
                return true;
            }
            size_t nb = pos;
            while (pos < s.size() && s[pos] != '=' && !isSpace(s[pos]) && s[pos] != '>' && s[pos] != '/') pos++;
            std::string an = localName(s.substr(nb, pos - nb));
            while (pos < s.size() && isSpace(s[pos])) pos++;
            if (pos < s.size() && s[pos] == '=') {
                pos++;
                while (pos < s.size() && isSpace(s[pos])) pos++;
                if (pos < s.size() && (s[pos] == '"' || s[pos] == '\'')) {
                    char q = s[pos++];
                    size_t end = s.find(q, pos);
                    if (end == std::string::npos) break;
                    node.attrs[an] = decodeEntities(s.substr(pos, end - pos));
                    pos = end + 1;
                } else {
                    size_t vb = pos;
                    while (pos < s.size() && !isSpace(s[pos]) && s[pos] != '>') pos++;
                    node.attrs[an] = decodeEntities(s.substr(vb, pos - vb));
                }
            } else if (!an.empty()) {
                node.attrs[an] = std::string();
            } else {
                pos++; // garbage, skip one char to guarantee progress
            }
        }
        error = "Unterminated start tag";
        return false;
    }
};

// ---------------------------------------------------------------- OPDS helpers

bool isAtomType(const std::string & type)
{
    return contains(type, "atom+xml");
}

bool isNavigationRel(const std::string & rel)
{
    return rel.empty() || rel == "subsection" || startsWith(rel, "http://opds-spec.org/sort/")
        || rel == "http://opds-spec.org/featured" || rel == "http://opds-spec.org/recommended"
        || rel == "http://opds-spec.org/popular" || rel == "http://opds-spec.org/new";
}

bool byPriority(const Acquisition & a, const Acquisition & b)
{
    return a.priority < b.priority;
}

std::string schemeAndHost(const std::string & url)
{
    size_t sch = url.find("://");
    if (sch == std::string::npos) return std::string();
    size_t slash = url.find('/', sch + 3);
    return slash == std::string::npos ? url : url.substr(0, slash);
}

} // namespace

// ---------------------------------------------------------------- public helpers

std::string resolveUrl(const std::string & base, const std::string & href)
{
    if (href.empty()) return base;
    if (contains(href, "://")) return href;
    if (startsWith(href, "//")) {
        size_t sch = base.find("://");
        return (sch == std::string::npos ? std::string("https:") : base.substr(0, sch + 1)) + href;
    }
    std::string host = schemeAndHost(base);
    if (href[0] == '/') return host + href;
    std::string pathPart = base;
    size_t cut = pathPart.find_first_of("?#");
    if (cut != std::string::npos) pathPart = pathPart.substr(0, cut);
    if (href[0] == '?') return pathPart + href;
    size_t lastSlash = pathPart.rfind('/');
    if (lastSlash == std::string::npos || lastSlash < host.size()) return host + "/" + href;
    return pathPart.substr(0, lastSlash + 1) + href;
}

std::string urlEncode(const std::string & utf8)
{
    static const char * hex = "0123456789ABCDEF";
    std::string out;
    for (size_t i = 0; i < utf8.size(); i++) {
        unsigned char c = (unsigned char)utf8[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string makeSearchUrl(const std::string & searchTemplate, const std::string & terms)
{
    std::string url = searchTemplate;
    size_t p = url.find("{searchTerms}");
    if (p != std::string::npos)
        url.replace(p, 13, urlEncode(trim(terms)));
    // Drop any other (optional) OpenSearch parameters such as {startPage?}.
    while ((p = url.find('{')) != std::string::npos) {
        size_t e = url.find('}', p);
        if (e == std::string::npos) break;
        url.erase(p, e - p + 1);
    }
    return url;
}

bool classifyFormat(const std::string & mimeType, Acquisition & a)
{
    std::string t = toLowerAscii(mimeType);
    bool zipped = contains(t, "+zip") || contains(t, "zip");
    if (contains(t, "fb2")) {
        a.extension = zipped ? "fb2.zip" : "fb2"; a.label = "FB2"; a.priority = 0;
    } else if (contains(t, "epub")) {
        a.extension = "epub"; a.label = "EPUB"; a.priority = 1;
    } else if (contains(t, "mobipocket")) {
        a.extension = "mobi"; a.label = "MOBI"; a.priority = 3;
    } else if (contains(t, "rtf")) {
        a.extension = zipped ? "rtf.zip" : "rtf"; a.label = "RTF"; a.priority = 4;
    } else if (contains(t, "txt") || t == "text/plain") {
        a.extension = zipped ? "txt.zip" : "txt"; a.label = "TXT"; a.priority = 5;
    } else if (contains(t, "html")) {
        a.extension = zipped ? "html.zip" : "html"; a.label = "HTML"; a.priority = 6;
    } else if (contains(t, "msword") || contains(t, "x-doc")) {
        a.extension = "doc"; a.label = "DOC"; a.priority = 7;
    } else if (contains(t, "chm")) {
        a.extension = "chm"; a.label = "CHM"; a.priority = 8;
    } else {
        return false; // PDF, DjVu and others: CoolReader cannot open them
    }
    a.type = mimeType;
    return true;
}

std::string makeBookFileName(const Entry & e, const Acquisition & a, size_t maxBytes)
{
    std::string base;
    if (!e.authors.empty()) base = e.authors[0] + " - ";
    base += e.title.empty() ? std::string("book") : e.title;
    std::string clean;
    for (size_t i = 0; i < base.size(); i++) {
        unsigned char c = (unsigned char)base[i];
        if (c < 0x20 || strchr("/\\:*?\"<>|", c) != NULL) clean += '_';
        else clean += (char)c;
    }
    clean = trim(clean);
    if (clean.size() > maxBytes) {
        size_t cut = maxBytes;
        while (cut > 0 && (((unsigned char)clean[cut]) & 0xC0) == 0x80) cut--; // UTF-8 boundary
        clean = trim(clean.substr(0, cut));
    }
    while (!clean.empty() && clean[clean.size() - 1] == '.') clean.erase(clean.size() - 1);
    if (clean.empty()) clean = "book";
    return clean + "." + a.extension;
}

std::string Entry::authorsText() const
{
    std::string s;
    for (size_t i = 0; i < authors.size(); i++) {
        if (i) s += ", ";
        s += authors[i];
    }
    return s;
}

// ---------------------------------------------------------------- feed parsing

bool Feed::parse(const std::string & xml, const std::string & url, std::string & error)
{
    title.clear(); nextHref.clear(); searchTemplate.clear(); entries.clear();
    baseUrl = url;

    Node root;
    XmlParser parser(xml);
    if (!parser.parse(root, error))
        return false;
    if (root.name != "feed") {
        error = "Not an OPDS catalog (root element <" + root.name + ">)";
        return false;
    }
    title = collapseSpaces(root.childText("title"));

    for (size_t i = 0; i < root.children.size(); i++) {
        const Node & n = root.children[i];
        if (n.name == "link") {
            std::string rel = n.attr("rel"), type = n.attr("type"), href = n.attr("href");
            if (rel == "next" && nextHref.empty())
                nextHref = resolveUrl(url, href);
            else if (rel == "search" && isAtomType(type) && contains(href, "{searchTerms}") && searchTemplate.empty())
                searchTemplate = resolveUrl(url, href);
            continue;
        }
        if (n.name != "entry")
            continue;

        Entry e;
        e.id = n.childText("id");
        e.title = collapseSpaces(n.childText("title"));
        for (size_t k = 0; k < n.children.size(); k++) {
            const Node & c = n.children[k];
            if (c.name == "author") {
                std::string name = collapseSpaces(c.childText("name"));
                if (!name.empty()) e.authors.push_back(name);
            } else if ((c.name == "content" || c.name == "summary") && e.summary.empty()) {
                std::string type = c.attr("type");
                e.summary = contains(type, "html") ? htmlToText(c.text) : collapseSpaces(trim(c.text));
            } else if (c.name == "link") {
                Link l;
                l.href = resolveUrl(url, c.attr("href"));
                l.rel = c.attr("rel");
                l.type = c.attr("type");
                l.title = collapseSpaces(c.attr("title"));
                if (startsWith(l.rel, "http://opds-spec.org/acquisition")) {
                    Acquisition a;
                    a.href = l.href;
                    if (classifyFormat(l.type, a)) {
                        bool dup = false;
                        for (size_t f = 0; f < e.formats.size(); f++)
                            if (e.formats[f].extension == a.extension) dup = true;
                        if (!dup) e.formats.push_back(a);
                    } else {
                        e.unsupportedTypes.push_back(l.type);
                    }
                } else if (isAtomType(l.type)) {
                    if (l.rel == "related")
                        e.related.push_back(l);
                    else if (isNavigationRel(l.rel) && e.navigationHref.empty())
                        e.navigationHref = l.href;
                }
            }
        }
        std::stable_sort(e.formats.begin(), e.formats.end(), byPriority);
        if (e.title.empty() && !e.isBook() && e.navigationHref.empty())
            continue;
        entries.push_back(e);
    }
    return true;
}

} // namespace opds
