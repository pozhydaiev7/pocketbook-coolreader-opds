/*
 * Reading statistics of CoolReader on PocketBook: the page log and the reports.
 *
 * Every page that stays on the screen for PB_STATS_MIN_SECONDS or longer is written to a text log
 * (one line per page, the time is capped at PB_STATS_MAX_SECONDS: the reader went away or the
 * device slept). The reports read the whole log.
 * Plain C++ without crengine types, so it can be checked on a desktop.
 */
#ifndef PB_STATS_H_INCLUDED
#define PB_STATS_H_INCLUDED

#include <map>
#include <set>
#include <string>
#include <vector>

/// a page shown for a shorter time was only turned over
#define PB_STATS_MIN_SECONDS 5
/// longer time on one page is counted as this much
#define PB_STATS_MAX_SECONDS 120

/// Days since 1970-01-01 of the local date of the time t
int pbStatsDay(long long t);
/// Day number of a date; month 1..12
int pbStatsDayOf(int year, int month, int day);
/// Date of a day number
void pbStatsDate(int dayNumber, int & year, int & month, int & day);
/// "dd.mm.yyyy"
std::string pbStatsDateText(int dayNumber);
/// "hh:mm:ss"; hours go over 24
std::string pbStatsTimeText(long long seconds);
/// Monday of the week of the day
int pbStatsWeekStart(int dayNumber);
/// "dd.mm.yyyy-dd.mm.yyyy" or one date "dd.mm.yyyy"; spaces and any of - – — between the dates
bool pbStatsParsePeriod(const std::string & text, int & fromDay, int & toDay);

/// One book of the log
struct PbStatsBook {
    std::string key;     // "<crc32>-<size>" of the book file
    std::string author;
    std::string title;
    long long finished;  // first time the last page was shown, 0 when never
    PbStatsBook() : finished(0) {}
};

/// One page that was read
struct PbStatsPage {
    int book;            // index in PbStatsLog::books
    long long start;     // unix time when the page was shown
    int seconds;
    int page;            // 0-based
    int pages;           // pages of the book then (the font size changes it)
    int words;
};

/// The whole log, read for the reports
class PbStatsLog
{
public:
    std::vector<PbStatsBook> books;
    std::vector<PbStatsPage> pages;  // in the log order, that is by time
    bool load(const std::string & path);
    void parseLine(const std::string & line);
    int find(const std::string & key) const;
private:
    std::map<std::string, int> _index;
    int book(const std::string & key);
};

/// Report of one book
struct PbStatsBookReport {
    long long started;     // first page time, 0 when the book has no pages
    long long finished;
    int pagesTotal;        // pages of the book in the last record
    int pagesRead;         // different pages, recounted to pagesTotal
    int lastPage;          // last read page, recounted to pagesTotal
    long long seconds;
    int records;
    std::vector<int> days;               // reading days, in order
    std::vector<long long> daySeconds;   // reading time of each of the days
    PbStatsBookReport() : started(0), finished(0), pagesTotal(0), pagesRead(0), lastPage(-1), seconds(0), records(0) {}
    int percent() const { return pagesTotal > 0 ? (int)((long long)pagesRead * 100 / pagesTotal) : 0; }
    long long secondsPerPage() const { return records > 0 ? seconds / records : 0; }
    long long secondsPerDay() const { return days.empty() ? 0 : seconds / (long long)days.size(); }
    /// time to read the pages after the last read one at the average page time; 0 when finished
    long long secondsToFinish() const;
};

/// Report of a period of days, both ends included
struct PbStatsPeriodReport {
    int fromDay, toDay;
    int periodDays;      // days of the period up to today
    int pages;           // a page of a book counts once a day
    long long words;
    long long seconds;
    int books;           // books read in the period
    int finishedBooks;   // books with the last page shown for the first time in the period
    std::vector<int> days;               // reading days, in order
    std::vector<long long> daySeconds;
    std::vector<int> bookIndexes;        // books of the period, most read first
    std::vector<long long> bookSeconds;
    PbStatsPeriodReport() : fromDay(0), toDay(0), periodDays(0), pages(0), words(0), seconds(0), books(0), finishedBooks(0) {}
    int readingDays() const { return (int)days.size(); }
};

class PbStatsReports
{
public:
    static PbStatsBookReport book(const PbStatsLog & log, int bookIndex);
    /// fromDay < 0: from the first day of the log
    static PbStatsPeriodReport period(const PbStatsLog & log, int fromDay, int toDay, int today);
    /// books by the total reading time, most read first
    static void allBooks(const PbStatsLog & log, std::vector<int> & bookIndexes, std::vector<long long> & seconds);
};

/// Writes the log while a book is read. One instance for the reader.
class PbStatsRecorder
{
public:
    static PbStatsRecorder & instance();
    void setFile(const std::string & path) { _file = path; }
    const std::string & file() const { return _file; }
    /// true when the page is not the one being timed (then pageShown is to be called)
    bool isNewPage(const std::string & key, int page, int pages) const;
    /// a page is on the screen: finishes the previous one and starts timing this one
    void pageShown(const std::string & key, const std::string & author, const std::string & title,
                   int page, int pages, int words, bool lastPage, long long now);
    /// the page is no longer read (exit, sleep, statistics window): writes it
    void flush(long long now);
private:
    PbStatsRecorder() : _active(false), _page(0), _pages(0), _words(0), _start(0) {}
    std::string _file;
    bool _active;
    std::string _key;
    int _page, _pages, _words;
    long long _start;
    // Maps, not std::set<std::string>, and no std::string operator+ in this module: libinkview
    // uses those templates, and the binary would export its own copies of them to the library.
    std::map<std::string, bool> _described;  // books with a B line written in this run
    std::map<std::string, bool> _finished;   // books with an F line written in this run
    void append(const char * type, const std::string & key, const std::string & rest);
};

#endif
