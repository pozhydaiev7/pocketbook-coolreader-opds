/*
 * Reading statistics screens of CoolReader on PocketBook. See pb_statsview.h.
 * The screens are the full-screen touch lists of the OPDS catalog (opds/opdsmenu.h).
 */
#include "pb_statsview.h"

#include <inkview.h>
#include <cri18n.h>
#include <lvstring.h>
#include <time.h>

#include "mainwnd.h"
#include "cr3pocketbook.h"
#include "opdsmenu.h"

extern V3DocViewWin * main_win;

using namespace opdsui;

namespace {

lString16 tr(const char * s)
{
    return lString16(_(s));
}

lString16 num(long long n)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", n);
    return lString16(buf);
}

lString16 dateText(long long t)
{
    return u16(pbStatsDateText(pbStatsDay(t)));
}

lString16 dayText(int day)
{
    return u16(pbStatsDateText(day));
}

lString16 rangeText(int fromDay, int toDay)
{
    lString16 s = dayText(fromDay);
    if (toDay != fromDay)
        s << L" \x2013 " << dayText(toDay);
    return s;
}

lString16 timeText(long long seconds)
{
    return u16(pbStatsTimeText(seconds));
}

// "2 d. 03:04:05": whole days of 24 hours, then the rest
lString16 durationText(long long seconds)
{
    lString16 s;
    long long days = seconds / 86400;
    if (days > 0)
        s << num(days) << L" " << tr("d.") << L" ";
    s << timeText(seconds % 86400);
    return s;
}

// Rounded average; 0 when there are no days
lString16 average(long long total, int days)
{
    return num(days > 0 ? (total + days / 2) / days : 0);
}

int countWords(const lString16 & text)
{
    int words = 0;
    bool inWord = false;
    for (int i = 0; i < text.length(); i++) {
        lChar16 ch = text[i];
        bool letter = (lGetCharProps(ch) & (CH_PROP_ALPHA | CH_PROP_DIGIT)) != 0;
        if (letter && !inWord)
            words++;
        if (letter)
            inWord = true;
        else if (!(inWord && (ch == '-' || ch == '\'' || ch == 0x2019)))
            inWord = false;
    }
    return words;
}

std::string utf8(const lString16 & s)
{
    return std::string(UnicodeToUtf8(s).c_str());
}

// Key of the book file: survives moving the file to another folder
std::string bookKey(LVDocView * doc)
{
    CRPropRef props = doc->getDocProps();
    lString16 crc = props->getStringDef(DOC_PROP_FILE_CRC32, "");
    lString16 size = props->getStringDef(DOC_PROP_FILE_SIZE, "");
    if (crc.empty() || crc == "0")
        return utf8(props->getStringDef(DOC_PROP_FILE_NAME, ""));
    // lString16 concatenation: std::string operator+ would be exported to libinkview (see pb_stats.h)
    return utf8(crc + L"-" + size);
}

std::string bookTitle(LVDocView * doc)
{
    lString16 title = doc->getTitle();
    title.trim();
    if (title.empty())
        title = doc->getDocProps()->getStringDef(DOC_PROP_FILE_NAME, "");
    return utf8(title);
}

LVDocView * openBook()
{
    if (main_win == NULL)
        return NULL;
    LVDocView * doc = main_win->getDocView();
    if (doc == NULL || !doc->isDocumentOpened() || doc->getPageCount() <= 0)
        return NULL;
    return doc;
}

void periodKeyboardHandler(char * s)
{
    executeCommand((s && *s) ? PB_STATS_CMD_PERIOD_APPLY : PB_STATS_CMD_MENU, 0);
}

PbStatsView::Line line(const lString16 & label, const lString16 & text)
{
    PbStatsView::Line l;
    l.label = label;
    l.text = text;
    return l;
}

PbStatsView::Line heading(const lString16 & label)
{
    PbStatsView::Line l;
    l.label = label;
    l.newRow = true;
    return l;
}

// Empty rows keep the paging row at the bottom of the screen
void addFillers(OpdsMenu * menu, int count)
{
    for (int i = 0; i < count; i++)
        menu->addItem(new OpdsItem(menu, PB_STATS_CMD_LAST, lString16::empty_str, lString16::empty_str));
}

void addPager(OpdsMenu * menu, int screen, int screens)
{
    OpdsItem * pager = new OpdsItem(menu, PB_STATS_CMD_LAST, lString16::empty_str, lString16::empty_str,
                                    OpdsItem::PAGER);
    if (screen > 0) {
        pager->left = lString16(MARK_PREV) + tr("Back");
        pager->leftCommand = PB_STATS_CMD_PREV;
    }
    if (screen + 1 < screens) {
        pager->right = tr("Next") + lString16(MARK_NEXT);
        pager->rightCommand = PB_STATS_CMD_NEXT;
    }
    pager->center = tr("Page") + L" " + num(screen + 1) + L"/" + num(screens);
    pager->centerCommand = 0;
    menu->addItem(pager);
}

} // namespace

// ------------------------------------------------------------ book window side

void PbStatsView::init(const lString16 & stateDir)
{
    LVCreateDirectory(stateDir);
    lString16 path = stateDir;
    if (!path.endsWith("/"))
        path << L"/";
    path << PB_STATS_FILE_NAME;
    PbStatsRecorder::instance().setFile(utf8(path));
    CRLog::info("Reading statistics: %s", UnicodeToUtf8(path).c_str());
}

void PbStatsView::pageShown(LVDocView * doc)
{
    if (doc == NULL || !doc->isDocumentOpened())
        return;
    int pages = doc->getPageCount();
    int page = doc->getCurPage();
    if (pages <= 0 || page < 0)
        return;
    PbStatsRecorder & recorder = PbStatsRecorder::instance();
    std::string key = bookKey(doc);
    if (!recorder.isNewPage(key, page, pages))
        return;
    int visible = doc->getVisiblePageCount();
    if (visible < 1)
        visible = 1;
    int words = 0;
    for (int i = page; i < page + visible && i < pages; i++) {
        LVRef<ldomXRange> range = doc->getPageDocumentRange(i);
        if (!range.isNull())
            words += countWords(range->getRangeText());
    }
    recorder.pageShown(key, utf8(doc->getAuthors()), bookTitle(doc), page, pages, words,
                       page + visible >= pages, (long long)time(NULL));
}

void PbStatsView::flush()
{
    PbStatsRecorder::instance().flush((long long)time(NULL));
}

// ------------------------------------------------------------ screens

PbStatsView::PbStatsView()
    : _screen(0), _booksScreen(0), _mode(MODE_MENU), _reportBack(PB_STATS_CMD_MENU)
{
    keyBuffer[0] = 0;
}

PbStatsView & PbStatsView::instance()
{
    static PbStatsView view;
    return view;
}

void PbStatsView::reload()
{
    flush();
    _log.load(PbStatsRecorder::instance().file());
}

void PbStatsView::open(CRGUIWindowManager * wm)
{
    LVDocView * doc = openBook();
    _thisBookKey = doc ? bookKey(doc) : std::string();
    _thisBookTitle = doc ? bookTitle(doc) : std::string();
    reload();
    showMenu(wm);
}

void PbStatsView::showMenu(CRGUIWindowManager * wm)
{
    _mode = MODE_MENU;
    int today = pbStatsDay((long long)time(NULL));
    int y, m, d;
    pbStatsDate(today, y, m, d);
    int week = pbStatsWeekStart(today);
    int monthStart = pbStatsDayOf(y, m, 1);
    int monthEnd = (m == 12 ? pbStatsDayOf(y + 1, 1, 1) : pbStatsDayOf(y, m + 1, 1)) - 1;

    lvRect rc = wm->getScreen()->getRect();
    OpdsMenu * menu = new OpdsMenu(wm, tr("Reading statistics"), rc, false);
    menu->setupRows(true);
    menu->add(PB_STATS_CMD_CLOSE, lString16(MARK_PREV) + tr("Back to the book"));
    if (!_thisBookKey.empty())
        menu->add(PB_STATS_CMD_THIS_BOOK, tr("This book"), u16(_thisBookTitle));
    menu->add(PB_STATS_CMD_TODAY, tr("Today"), dayText(today));
    menu->add(PB_STATS_CMD_WEEK, tr("This week"), rangeText(week, week + 6));
    menu->add(PB_STATS_CMD_MONTH, tr("This month"), rangeText(monthStart, monthEnd));
    menu->add(PB_STATS_CMD_YEAR, tr("This year"), rangeText(pbStatsDayOf(y, 1, 1), pbStatsDayOf(y, 12, 31)));
    menu->add(PB_STATS_CMD_ALL_TIME, tr("All time"));
    menu->add(PB_STATS_CMD_PERIOD, tr("Choose a period..."), tr("dd.mm.yyyy-dd.mm.yyyy"));
    menu->add(PB_STATS_CMD_BOOKS, tr("All books"), tr("Reading time of every book"));
    activate(wm, menu);
}

void PbStatsView::showBooks(CRGUIWindowManager * wm)
{
    _mode = MODE_BOOKS;
    lvRect rc = wm->getScreen()->getRect();
    OpdsMenu * menu = new OpdsMenu(wm, tr("All books"), rc, false);
    int rows = menu->setupRows(true);
    int total = (int)_bookIndexes.size();
    int perScreen = rows - 1;
    if (total > perScreen)
        perScreen = rows - 2;
    if (perScreen < 1)
        perScreen = 1;
    int screens = total > 0 ? (total + perScreen - 1) / perScreen : 1;
    if (_booksScreen >= screens)
        _booksScreen = screens - 1;
    if (_booksScreen < 0)
        _booksScreen = 0;
    menu->setPageCommands(PB_STATS_CMD_PREV, PB_STATS_CMD_NEXT);

    menu->add(PB_STATS_CMD_MENU, lString16(MARK_PREV) + tr("Reading statistics"));
    int first = _booksScreen * perScreen;
    int shown = 0;
    for (int i = first; i < total && i < first + perScreen; i++, shown++) {
        const PbStatsBook & b = _log.books[_bookIndexes[i]];
        lString16 sub;
        if (!b.author.empty())
            sub << u16(b.author) << lString16(DOT);
        sub << durationText(_bookSeconds[i]);
        menu->add(PB_STATS_CMD_BOOK + _bookIndexes[i], u16(b.title.empty() ? b.key : b.title), sub);
    }
    if (screens > 1) {
        addFillers(menu, perScreen - shown);
        addPager(menu, _booksScreen, screens);
    }
    activate(wm, menu);
}

void PbStatsView::showReport(CRGUIWindowManager * wm)
{
    _mode = MODE_REPORT;
    lvRect rc = wm->getScreen()->getRect();
    OpdsMenu * menu = new OpdsMenu(wm, _reportCaption, rc, false);
    int rows = menu->setupRows(true);

    // two lines in a row; a heading or a book starts a new row
    std::vector<std::pair<int, int> > packed;
    for (int i = 0; i < (int)_report.size(); i++) {
        if (!packed.empty() && packed.back().second < 0 && !_report[i].newRow)
            packed.back().second = i;
        else
            packed.push_back(std::make_pair(i, -1));
    }
    int total = (int)packed.size();
    int perScreen = rows - 1;
    if (total > perScreen)
        perScreen = rows - 2;
    if (perScreen < 1)
        perScreen = 1;
    int screens = total > 0 ? (total + perScreen - 1) / perScreen : 1;
    if (_screen >= screens)
        _screen = screens - 1;
    if (_screen < 0)
        _screen = 0;
    menu->setPageCommands(PB_STATS_CMD_PREV, PB_STATS_CMD_NEXT);

    menu->add(PB_STATS_CMD_BACK, lString16(MARK_PREV) +
              (_reportBack == PB_STATS_CMD_BOOKS ? tr("All books") : tr("Reading statistics")));
    int indent = menu->subFont->getHeight();
    int first = _screen * perScreen;
    int shown = 0;
    for (int i = first; i < total && i < first + perScreen; i++, shown++) {
        OpdsItem * item = new OpdsItem(menu, PB_STATS_CMD_LAST, lString16::empty_str, lString16::empty_str,
                                       OpdsItem::TEXT);
        for (int k = 0; k < 2; k++) {
            int index = k == 0 ? packed[i].first : packed[i].second;
            if (index < 0)
                continue;
            const Line & l = _report[index];
            InfoLine & info = k == 0 ? item->first : item->second;
            info.label = l.label;
            info.text = l.text;
            info.font = l.title ? menu->titleFont : menu->subFont;
            info.indent = l.indent * indent;
        }
        menu->addItem(item);
    }
    if (screens > 1) {
        addFillers(menu, perScreen - shown);
        addPager(menu, _screen, screens);
    }
    activate(wm, menu);
}

void PbStatsView::bookReport(int bookIndex, int back)
{
    const PbStatsBook & b = _log.books[bookIndex];
    PbStatsBookReport r = PbStatsReports::book(_log, bookIndex);
    _reportBack = back;
    _reportCaption = tr("Book statistics");
    _report.clear();
    _screen = 0;

    Line title = line(lString16::empty_str, u16(b.title.empty() ? b.key : b.title));
    title.title = true;
    title.newRow = true;
    _report.push_back(title);
    _report.push_back(line(lString16::empty_str, u16(b.author)));

    _report.push_back(line(tr("Reading started: "), r.started ? dateText(r.started) : lString16(L"\x2014")));
    _report.push_back(line(tr("Reading finished: "), r.finished ? dateText(r.finished) : lString16(L"\x2014")));
    _report.push_back(line(tr("Reading days: "), num((long long)r.days.size())));
    char buf[128];
    snprintf(buf, sizeof(buf), _("%d of %d pages (%d%%)"), r.pagesRead, r.pagesTotal, r.percent());
    _report.push_back(line(tr("Read: "), lString16(buf)));
    _report.push_back(line(tr("Total reading time: "), timeText(r.seconds)));
    _report.push_back(line(tr("Time to finish (estimate): "),
                           r.finished ? lString16(L"\x2014") : timeText(r.secondsToFinish())));
    _report.push_back(line(tr("Average time per page: "), timeText(r.secondsPerPage())));
    _report.push_back(line(tr("Average reading time per day: "), timeText(r.secondsPerDay())));

    if (!r.days.empty()) {
        _report.push_back(heading(tr("Reading days")));
        for (size_t i = 0; i < r.days.size(); i++)
            _report.push_back(line(lString16::empty_str, dayText(r.days[i]) + L" \x2014 " + timeText(r.daySeconds[i])));
    }
}

void PbStatsView::periodReport(const lString16 & caption, int fromDay, int toDay)
{
    int today = pbStatsDay((long long)time(NULL));
    PbStatsPeriodReport r = PbStatsReports::period(_log, fromDay, toDay, today);
    _reportBack = PB_STATS_CMD_MENU;
    _reportCaption = caption;
    _report.clear();
    _screen = 0;

    Line title = line(lString16::empty_str, rangeText(r.fromDay, r.toDay));
    title.title = true;
    title.newRow = true;
    _report.push_back(title);
    _report.push_back(line(tr("Reading days: "), num(r.readingDays()) + L" / " + num(r.periodDays)));

    _report.push_back(line(tr("Pages read: "), num(r.pages)));
    _report.push_back(line(tr("Pages per reading day: "), average(r.pages, r.readingDays())));
    _report.push_back(line(tr("Pages per day of the period: "), average(r.pages, r.periodDays)));
    _report.push_back(line(tr("Words read: "), num(r.words)));
    _report.push_back(line(tr("Words per reading day: "), average(r.words, r.readingDays())));
    _report.push_back(line(tr("Words per day of the period: "), average(r.words, r.periodDays)));
    _report.push_back(line(tr("Books read: "), num(r.books)));
    _report.push_back(line(tr("Books read to the end: "), num(r.finishedBooks)));
    _report.push_back(line(tr("Total reading time: "), durationText(r.seconds)));

    if (!r.days.empty()) {
        _report.push_back(heading(tr("Reading time by days")));
        for (size_t i = 0; i < r.days.size(); i++)
            _report.push_back(line(lString16::empty_str, dayText(r.days[i]) + L" \x2014 " + timeText(r.daySeconds[i])));
    }
    if (!r.bookIndexes.empty()) {
        _report.push_back(heading(tr("Books")));
        for (size_t i = 0; i < r.bookIndexes.size(); i++) {
            const PbStatsBook & b = _log.books[r.bookIndexes[i]];
            lString16 name;
            if (!b.author.empty())
                name << u16(b.author) << lString16(DASH);
            name << u16(b.title.empty() ? b.key : b.title);
            Line book = line(lString16::empty_str, name);
            book.newRow = true;
            _report.push_back(book);
            Line time = line(lString16::empty_str, durationText(r.bookSeconds[i]));
            time.indent = 1;
            _report.push_back(time);
        }
    }
}

void PbStatsView::askPeriod()
{
    if (keyBuffer[0] == 0) {
        int today = pbStatsDay((long long)time(NULL));
        int y, m, d;
        pbStatsDate(today, y, m, d);
        snprintf(keyBuffer, sizeof(keyBuffer), "%s-%s", pbStatsDateText(pbStatsDayOf(y, m, 1)).c_str(),
                 pbStatsDateText(today).c_str());
    }
    pbOpenKeyboard(_("Period: dd.mm.yyyy-dd.mm.yyyy"), keyBuffer, sizeof(keyBuffer) - 1, periodKeyboardHandler);
}

bool PbStatsView::onCommand(CRGUIWindowManager * wm, int command, int params)
{
    (void)params;
    CRLog::trace("PbStatsView::onCommand(%d)", command);
    int today = pbStatsDay((long long)time(NULL));
    int y, m, d;
    pbStatsDate(today, y, m, d);

    switch (command) {
    case PB_STATS_CMD_MENU:
        showMenu(wm);
        return true;
    case PB_STATS_CMD_CLOSE:
        return true;
    case PB_STATS_CMD_THIS_BOOK: {
        int index = _log.find(_thisBookKey);
        if (index < 0) {
            Message(ICON_INFORMATION, const_cast<char *>("CoolReader"),
                    const_cast<char *>(_("No statistics for this book yet")), 2000);
            showMenu(wm);
            return true;
        }
        bookReport(index, PB_STATS_CMD_MENU);
        showReport(wm);
        return true;
    }
    case PB_STATS_CMD_TODAY:
        periodReport(tr("Today"), today, today);
        showReport(wm);
        return true;
    case PB_STATS_CMD_WEEK: {
        int week = pbStatsWeekStart(today);
        periodReport(tr("This week"), week, week + 6);
        showReport(wm);
        return true;
    }
    case PB_STATS_CMD_MONTH: {
        int monthEnd = (m == 12 ? pbStatsDayOf(y + 1, 1, 1) : pbStatsDayOf(y, m + 1, 1)) - 1;
        periodReport(tr("This month"), pbStatsDayOf(y, m, 1), monthEnd);
        showReport(wm);
        return true;
    }
    case PB_STATS_CMD_YEAR:
        periodReport(tr("This year"), pbStatsDayOf(y, 1, 1), pbStatsDayOf(y, 12, 31));
        showReport(wm);
        return true;
    case PB_STATS_CMD_ALL_TIME:
        periodReport(tr("All time"), -1, today);
        showReport(wm);
        return true;
    case PB_STATS_CMD_PERIOD:
        askPeriod();
        return true;
    case PB_STATS_CMD_PERIOD_APPLY: {
        int from, to;
        if (!pbStatsParsePeriod(keyBuffer, from, to)) {
            Message(ICON_WARNING, const_cast<char *>("CoolReader"),
                    const_cast<char *>(_("Enter the period as dd.mm.yyyy-dd.mm.yyyy")), 3000);
            showMenu(wm);
            return true;
        }
        periodReport(tr("Period"), from, to);
        showReport(wm);
        return true;
    }
    case PB_STATS_CMD_BOOKS:
        PbStatsReports::allBooks(_log, _bookIndexes, _bookSeconds);
        _booksScreen = 0;
        showBooks(wm);
        return true;
    case PB_STATS_CMD_PREV:
    case PB_STATS_CMD_NEXT: {
        int delta = command == PB_STATS_CMD_NEXT ? 1 : -1;
        if (_mode == MODE_BOOKS) {
            _booksScreen += delta;
            showBooks(wm);
        } else if (_mode == MODE_REPORT) {
            _screen += delta;
            showReport(wm);
        } else {
            showMenu(wm);
        }
        return true;
    }
    case PB_STATS_CMD_BACK:
        if (_reportBack == PB_STATS_CMD_BOOKS)
            showBooks(wm);
        else
            showMenu(wm);
        return true;
    case PB_STATS_CMD_LAST:
        // a tap on a text or empty row: the list closed, show the same screen again
        if (_mode == MODE_BOOKS)
            showBooks(wm);
        else if (_mode == MODE_REPORT)
            showReport(wm);
        else
            showMenu(wm);
        return true;
    default:
        break;
    }
    if (command >= PB_STATS_CMD_BOOK && command < PB_STATS_CMD_LAST) {
        int index = command - PB_STATS_CMD_BOOK;
        if (index < (int)_log.books.size()) {
            bookReport(index, PB_STATS_CMD_BOOKS);
            showReport(wm);
        } else {
            showMenu(wm);
        }
        return true;
    }
    return false;
}
