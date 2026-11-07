/*
 * Reading statistics of CoolReader on PocketBook. See pb_stats.h.
 *
 * Log lines, fields separated by tabs:
 *   B <key> <author> <title>                      book description, written once per run
 *   P <key> <start> <seconds> <page> <pages> <words>  a page that was read
 *   F <key> <time>                                the last page was shown
 */
#include "pb_stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>

// ------------------------------------------------------------ dates

int pbStatsDayOf(int year, int month, int day)
{
    // days_from_civil, H. Hinnant
    int y = year - (month <= 2 ? 1 : 0);
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void pbStatsDate(int dayNumber, int & year, int & month, int & day)
{
    int z = dayNumber + 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    day = doy - (153 * mp + 2) / 5 + 1;
    month = mp + (mp < 10 ? 3 : -9);
    year = yoe + era * 400 + (month <= 2 ? 1 : 0);
}

int pbStatsDay(long long t)
{
    time_t tt = (time_t)t;
    struct tm tmv;
    localtime_r(&tt, &tmv);
    return pbStatsDayOf(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
}

std::string pbStatsDateText(int dayNumber)
{
    int y, m, d;
    pbStatsDate(dayNumber, y, m, d);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d.%02d.%04d", d, m, y);
    return buf;
}

std::string pbStatsTimeText(long long seconds)
{
    if (seconds < 0)
        seconds = 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%02lld:%02d:%02d", seconds / 3600, (int)(seconds / 60 % 60), (int)(seconds % 60));
    return buf;
}

int pbStatsWeekStart(int dayNumber)
{
    // 1970-01-01 was a Thursday: Monday-based weekday of day 0 is 3
    int weekday = ((dayNumber % 7) + 7 + 3) % 7;
    return dayNumber - weekday;
}

static bool pbStatsValidDate(int y, int m, int d, int & dayNumber)
{
    if (y < 1970 || y > 2200 || m < 1 || m > 12 || d < 1 || d > 31)
        return false;
    dayNumber = pbStatsDayOf(y, m, d);
    int y2, m2, d2;
    pbStatsDate(dayNumber, y2, m2, d2);
    return y2 == y && m2 == m && d2 == d;
}

bool pbStatsParsePeriod(const std::string & text, int & fromDay, int & toDay)
{
    std::vector<int> numbers;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] >= '0' && text[i] <= '9') {
            int n = 0, digits = 0;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9' && digits < 5) {
                n = n * 10 + (text[i] - '0');
                i++;
                digits++;
            }
            numbers.push_back(n);
        } else {
            i++;
        }
    }
    if (numbers.size() != 3 && numbers.size() != 6)
        return false;
    if (!pbStatsValidDate(numbers[2], numbers[1], numbers[0], fromDay))
        return false;
    toDay = fromDay;
    if (numbers.size() == 6 && !pbStatsValidDate(numbers[5], numbers[4], numbers[3], toDay))
        return false;
    if (toDay < fromDay)
        std::swap(fromDay, toDay);
    return true;
}

// ------------------------------------------------------------ log

static void pbStatsSplit(const std::string & line, std::vector<std::string> & fields)
{
    fields.clear();
    size_t start = 0;
    for (;;) {
        size_t tab = line.find('\t', start);
        if (tab == std::string::npos) {
            fields.push_back(line.substr(start));
            return;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

int PbStatsLog::find(const std::string & key) const
{
    std::map<std::string, int>::const_iterator it = _index.find(key);
    return it == _index.end() ? -1 : it->second;
}

int PbStatsLog::book(const std::string & key)
{
    int index = find(key);
    if (index >= 0)
        return index;
    PbStatsBook b;
    b.key = key;
    books.push_back(b);
    index = (int)books.size() - 1;
    _index[key] = index;
    return index;
}

void PbStatsLog::parseLine(const std::string & line)
{
    std::vector<std::string> f;
    pbStatsSplit(line, f);
    if (f.size() < 2 || f[0].size() != 1 || f[1].empty())
        return;
    switch (f[0][0]) {
    case 'B':
        if (f.size() >= 4) {
            PbStatsBook & b = books[book(f[1])];
            b.author = f[2];
            b.title = f[3];
        }
        break;
    case 'P':
        if (f.size() >= 7) {
            PbStatsPage p;
            p.book = book(f[1]);
            p.start = atoll(f[2].c_str());
            p.seconds = atoi(f[3].c_str());
            p.page = atoi(f[4].c_str());
            p.pages = atoi(f[5].c_str());
            p.words = atoi(f[6].c_str());
            if (p.start > 0 && p.seconds > 0 && p.pages > 0 && p.page >= 0)
                pages.push_back(p);
        }
        break;
    case 'F':
        if (f.size() >= 3) {
            long long t = atoll(f[2].c_str());
            PbStatsBook & b = books[book(f[1])];
            if (t > 0 && (b.finished == 0 || t < b.finished))
                b.finished = t;
        }
        break;
    default:
        break;
    }
}

bool PbStatsLog::load(const std::string & path)
{
    books.clear();
    pages.clear();
    _index.clear();
    FILE * f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::string line;
    char buf[4096];
    while (fgets(buf, sizeof(buf), f)) {
        line += buf;
        if (!line.empty() && line[line.size() - 1] == '\n') {
            line.erase(line.size() - 1);
            if (!line.empty() && line[line.size() - 1] == '\r')
                line.erase(line.size() - 1);
            parseLine(line);
            line.clear();
        }
    }
    if (!line.empty())
        parseLine(line);
    fclose(f);
    return true;
}

// ------------------------------------------------------------ reports

// Page number recounted to the page count of the book now (the font size changes it)
static int pbStatsNormPage(const PbStatsPage & p, int pagesTotal)
{
    if (pagesTotal <= 0 || p.pages <= 0)
        return p.page;
    int n = (int)((long long)p.page * pagesTotal / p.pages);
    if (n >= pagesTotal)
        n = pagesTotal - 1;
    return n < 0 ? 0 : n;
}

long long PbStatsBookReport::secondsToFinish() const
{
    if (finished != 0 || pagesTotal <= 0)
        return 0;
    int left = pagesTotal - (lastPage + 1);
    return left > 0 ? (long long)left * secondsPerPage() : 0;
}

PbStatsBookReport PbStatsReports::book(const PbStatsLog & log, int bookIndex)
{
    PbStatsBookReport r;
    if (bookIndex < 0 || bookIndex >= (int)log.books.size())
        return r;
    r.finished = log.books[bookIndex].finished;
    for (size_t i = 0; i < log.pages.size(); i++)
        if (log.pages[i].book == bookIndex)
            r.pagesTotal = log.pages[i].pages;
    std::set<int> read;
    std::map<int, long long> days;
    for (size_t i = 0; i < log.pages.size(); i++) {
        const PbStatsPage & p = log.pages[i];
        if (p.book != bookIndex)
            continue;
        if (r.started == 0)
            r.started = p.start;
        int n = pbStatsNormPage(p, r.pagesTotal);
        read.insert(n);
        r.lastPage = n;
        r.seconds += p.seconds;
        r.records++;
        days[pbStatsDay(p.start)] += p.seconds;
    }
    r.pagesRead = (int)read.size();
    for (std::map<int, long long>::const_iterator it = days.begin(); it != days.end(); ++it) {
        r.days.push_back(it->first);
        r.daySeconds.push_back(it->second);
    }
    return r;
}

struct PbStatsBySeconds {
    const std::vector<long long> * seconds;
    bool operator()(int a, int b) const { return (*seconds)[a] > (*seconds)[b]; }
};

static void pbStatsSortBooks(const std::map<int, long long> & totals, std::vector<int> & indexes,
                             std::vector<long long> & seconds)
{
    std::vector<int> order;
    std::vector<long long> values;
    std::vector<int> books;
    for (std::map<int, long long>::const_iterator it = totals.begin(); it != totals.end(); ++it) {
        order.push_back((int)books.size());
        books.push_back(it->first);
        values.push_back(it->second);
    }
    PbStatsBySeconds cmp;
    cmp.seconds = &values;
    std::stable_sort(order.begin(), order.end(), cmp);
    indexes.clear();
    seconds.clear();
    for (size_t i = 0; i < order.size(); i++) {
        indexes.push_back(books[order[i]]);
        seconds.push_back(values[order[i]]);
    }
}

PbStatsPeriodReport PbStatsReports::period(const PbStatsLog & log, int fromDay, int toDay, int today)
{
    PbStatsPeriodReport r;
    if (fromDay < 0)
        fromDay = log.pages.empty() ? today : pbStatsDay(log.pages[0].start);
    r.fromDay = fromDay;
    r.toDay = toDay;
    int last = toDay < today ? toDay : today;
    r.periodDays = last >= fromDay ? last - fromDay + 1 : 0;

    std::map<int, long long> days, books;
    std::set<std::pair<long long, long long> > counted;
    for (size_t i = 0; i < log.pages.size(); i++) {
        const PbStatsPage & p = log.pages[i];
        int day = pbStatsDay(p.start);
        if (day < fromDay || day > toDay)
            continue;
        r.seconds += p.seconds;
        days[day] += p.seconds;
        books[p.book] += p.seconds;
        std::pair<long long, long long> key((long long)p.book * 1000000 + day, (long long)p.page * 1000000 + p.pages);
        if (counted.insert(key).second) {
            r.pages++;
            r.words += p.words;
        }
    }
    for (std::map<int, long long>::const_iterator it = days.begin(); it != days.end(); ++it) {
        r.days.push_back(it->first);
        r.daySeconds.push_back(it->second);
    }
    r.books = (int)books.size();
    for (size_t i = 0; i < log.books.size(); i++) {
        if (log.books[i].finished == 0)
            continue;
        int day = pbStatsDay(log.books[i].finished);
        if (day >= fromDay && day <= toDay)
            r.finishedBooks++;
    }
    pbStatsSortBooks(books, r.bookIndexes, r.bookSeconds);
    return r;
}

void PbStatsReports::allBooks(const PbStatsLog & log, std::vector<int> & bookIndexes, std::vector<long long> & seconds)
{
    std::map<int, long long> books;
    for (size_t i = 0; i < log.pages.size(); i++)
        books[log.pages[i].book] += log.pages[i].seconds;
    pbStatsSortBooks(books, bookIndexes, seconds);
}

// ------------------------------------------------------------ recorder

PbStatsRecorder & PbStatsRecorder::instance()
{
    static PbStatsRecorder recorder;
    return recorder;
}

static std::string pbStatsField(const std::string & s)
{
    std::string r = s;
    for (size_t i = 0; i < r.size(); i++)
        if (r[i] == '\t' || r[i] == '\n' || r[i] == '\r')
            r[i] = ' ';
    return r;
}

// Writes "<type>\t<key><rest>\n"; rest starts with a tab
void PbStatsRecorder::append(const char * type, const std::string & key, const std::string & rest)
{
    if (_file.empty())
        return;
    FILE * f = fopen(_file.c_str(), "a");
    if (!f)
        return;
    fprintf(f, "%s\t%s%s\n", type, key.c_str(), rest.c_str());
    fclose(f);
}

bool PbStatsRecorder::isNewPage(const std::string & key, int page, int pages) const
{
    return !_active || key != _key || page != _page || pages != _pages;
}

void PbStatsRecorder::pageShown(const std::string & key, const std::string & author, const std::string & title,
                                int page, int pages, int words, bool lastPage, long long now)
{
    flush(now);
    if (key.empty() || pages <= 0 || page < 0)
        return;
    std::string k = pbStatsField(key);
    if (_described.find(k) == _described.end()) {
        _described[k] = true;
        std::string rest;
        rest.append("\t").append(pbStatsField(author)).append("\t").append(pbStatsField(title));
        append("B", k, rest);
    }
    if (lastPage && _finished.find(k) == _finished.end()) {
        _finished[k] = true;
        char buf[32];
        snprintf(buf, sizeof(buf), "\t%lld", now);
        append("F", k, buf);
    }
    _active = true;
    _key = k;
    _page = page;
    _pages = pages;
    _words = words;
    _start = now;
}

void PbStatsRecorder::flush(long long now)
{
    if (!_active)
        return;
    _active = false;
    long long seconds = now - _start;
    if (seconds < PB_STATS_MIN_SECONDS)
        return;
    if (seconds > PB_STATS_MAX_SECONDS)
        seconds = PB_STATS_MAX_SECONDS;
    char buf[128];
    snprintf(buf, sizeof(buf), "\t%lld\t%lld\t%d\t%d\t%d", _start, seconds, _page, _pages, _words);
    append("P", _key, buf);
}
