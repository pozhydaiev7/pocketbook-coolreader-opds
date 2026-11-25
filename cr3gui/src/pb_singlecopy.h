// One copy of the program at a time: the rules, with no firmware call in them (those are in
// cr3pocketbook.cpp, pbInstancePublish and what follows it), so that they are checked on a PC.
//
// The firmware does not end the program when the reader leaves a book by the Home key: its task
// stays in the background, and a tap on another book starts the launcher script again, which
// started another copy. Every copy keeps the settings, the history and the queue of the position
// sync as it read them and writes whole files, so the copy that writes last takes back what the
// others wrote (the place of a book just read was lost that way, 0.0.8.2-opds-alpha1).
//
// Now the copy that runs names itself in a file (its task and its process). A start of the program
// that finds it there leaves the book to open in a second file, gives the running copy the screen
// and ends; the running copy opens the book when it comes to the screen. KOReader does the same on
// these readers.
#ifndef PB_SINGLECOPY_H
#define PB_SINGLECOPY_H

#include <stdio.h>
#include <string>
#include <vector>

// A task as the firmware lists it (GetTaskList, GetTaskInfo)
struct PbTaskRow {
    int task;          // the number of the task
    int pid;           // its main process; for this program the launcher script, not the program
    int subtask;       // the number of its first subtask, 0 when it names none
    std::string app;   // the name of its application, empty when the firmware does not tell
    bool known;        // the firmware gave information about the task

    PbTaskRow() : task(0), pid(0), subtask(0), known(false) { }
    PbTaskRow(int t, int p, const std::string & a, bool k = true, int s = 0)
        : task(t), pid(p), subtask(s), app(a), known(k) { }
};

class PbSingleCopy {
public:
    /// The task this copy runs in: by the number the firmware names for it, or by its process
    /// (the task is that of the launcher script, which started the program).
    static bool isOwn(const PbTaskRow & row, int ownTask, int ownPid, int parentPid)
    {
        if (ownTask > 0 && row.task == ownTask)
            return true;
        return row.known && row.pid > 0 && (row.pid == ownPid || row.pid == parentPid);
    }

    /// The name of the application of this copy, as the firmware has it; empty when the list does
    /// not have the task of this copy or it has no name.
    static std::string ownApp(const std::vector<PbTaskRow> & tasks, int ownTask, int ownPid, int parentPid)
    {
        for (size_t i = 0; i < tasks.size(); i++)
            if (tasks[i].known && isOwn(tasks[i], ownTask, ownPid, parentPid) && !tasks[i].app.empty())
                return tasks[i].app;
        return std::string();
    }

    /// The tasks of the other copies of this program: the same application name, another task.
    /// None when the task of this copy is not known. For the log: none is expected to run.
    static std::vector<PbTaskRow> others(const std::vector<PbTaskRow> & tasks, int ownTask, int ownPid, int parentPid)
    {
        std::vector<PbTaskRow> found;
        const std::string app = ownApp(tasks, ownTask, ownPid, parentPid);
        if (app.empty())
            return found;
        for (size_t i = 0; i < tasks.size(); i++) {
            const PbTaskRow & row = tasks[i];
            if (!row.known || row.task <= 0 || row.app != app || isOwn(row, ownTask, ownPid, parentPid))
                continue;
            bool listed = false;
            for (size_t j = 0; j < found.size() && !listed; j++)
                listed = found[j].task == row.task;
            if (!listed)
                found.push_back(row);
        }
        return found;
    }

    /// The file the running copy names itself in: its task and its process, a line. The launcher
    /// script reads it with `read TASK PID`. Empty for numbers that are no task and no process.
    static std::string instanceText(int task, int pid)
    {
        if (task <= 0 || pid <= 0)
            return std::string();
        char buf[64];
        sprintf(buf, "%d %d\n", task, pid);
        return buf;
    }

    /// The task and the process of that file; false for anything but two numbers above zero on the
    /// first line, with nothing else on it.
    static bool parseInstance(const std::string & text, int & task, int & pid)
    {
        task = pid = 0;
        size_t i = 0;
        long values[2] = { 0, 0 };
        for (int n = 0; n < 2; n++) {
            if (n == 1) {
                if (i >= text.size() || text[i] != ' ')
                    return false;
                i++;
            }
            const size_t start = i;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9' && i - start < 9)
                values[n] = values[n] * 10 + (text[i++] - '0');
            if (i == start)
                return false;
        }
        if (i < text.size() && text[i] != '\n' && text[i] != '\r')
            return false;
        if (values[0] <= 0 || values[1] <= 0)
            return false;
        task = (int)values[0];
        pid = (int)values[1];
        return true;
    }

    /// The file a start leaves for the running copy: the path of the book, a line. Empty for a path
    /// that is empty or has a line break in it (nothing is written then).
    static std::string openText(const std::string & book)
    {
        if (book.empty() || book.find('\n') != std::string::npos || book.find('\r') != std::string::npos)
            return std::string();
        return book + "\n";
    }

    /// The path of that file: its first line, without the line break
    static std::string parseOpen(const std::string & text)
    {
        size_t end = text.find('\n');
        if (end == std::string::npos)
            end = text.size();
        while (end > 0 && text[end - 1] == '\r')
            end--;
        return text.substr(0, end);
    }

    /// Is a process this program, by its command line (/proc/<pid>/cmdline, the words parted by
    /// zero bytes): a word of it ends in the name of the program. The launcher script and the
    /// program have the same name; a process number is given to another program when the copy that
    /// named itself is gone and the reader has run long enough.
    static bool isOurProcess(const std::string & cmdline, const std::string & name = "cr3-pb.app")
    {
        size_t start = 0;
        while (start <= cmdline.size()) {
            size_t end = cmdline.find('\0', start);
            if (end == std::string::npos)
                end = cmdline.size();
            const std::string word = cmdline.substr(start, end - start);
            if (word.size() >= name.size() && word.compare(word.size() - name.size(), name.size(), name) == 0
                    && (word.size() == name.size() || word[word.size() - name.size() - 1] == '/'))
                return true;
            start = end + 1;
        }
        return false;
    }
};

#endif
