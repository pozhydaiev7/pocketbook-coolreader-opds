/*
 * The contents as a tree whose branches open and close: which entries have entries under them,
 * which are open, which rows are seen. Both lists of the contents use it (the touch contents of
 * pb_toc.cpp and the plain list of pb_toclist.cpp), for a book and for the pages of a PDF or a
 * DjVu. A tester: a contents of many levels ran over ten screens with every level shown; the
 * contents window of the firmware, given up for its crashes on firmware 6.11, showed the first
 * level and opened a branch by a tap on the triangle before it.
 */
#ifndef PB_TOCTREE_H_INCLUDED
#define PB_TOCTREE_H_INCLUDED

#include <vector>

class PbTocTree
{
public:
    /// levels: the level of each entry in the order of the contents (any numbers, a larger one
    /// lies under the smaller one before it). current: the entry of the page read, -1 for none.
    /// collapsed: only the first level is seen, and the branch down to the current entry; else
    /// every branch is open.
    void init(const std::vector<int> & levels, int current, bool collapsed)
    {
        _levels = levels;
        _open.assign(levels.size(), !collapsed);
        if (collapsed && current >= 0 && current < (int)levels.size()) {
            const int first = top();
            int level = levels[current];
            for (int i = current - 1; i >= 0 && level > first; i--)
                if (levels[i] < level) {
                    _open[i] = true;
                    level = levels[i];
                }
        }
    }
    int count() const { return (int)_levels.size(); }
    /// Entries lie under it: the next entry has a larger level.
    bool hasChildren(int i) const { return i + 1 < count() && _levels[i + 1] > _levels[i]; }
    bool isOpen(int i) const { return i >= 0 && i < count() && _open[i]; }
    void toggle(int i)
    {
        if (i >= 0 && i < count() && hasChildren(i))
            _open[i] = !_open[i];
    }
    /// The entries seen, in their order: those whose every branch above is open.
    void rows(std::vector<int> & out) const
    {
        out.clear();
        // the level under which the entries are hidden by a closed branch; none while it is above all
        int hiddenUnder = 0x7fffffff;
        for (int i = 0; i < count(); i++) {
            if (_levels[i] > hiddenUnder)
                continue;
            hiddenUnder = 0x7fffffff;
            out.push_back(i);
            if (hasChildren(i) && !_open[i])
                hiddenUnder = _levels[i];
        }
    }
    /// The row of an entry among rows(), or of the nearest entry above it that is seen.
    static int rowOf(const std::vector<int> & rows, int entry)
    {
        int row = 0;
        for (int r = 0; r < (int)rows.size() && rows[r] <= entry; r++)
            row = r;
        return row;
    }
    /// True when some entry has entries under it: the rows then keep room for the triangle.
    bool branched() const
    {
        for (int i = 0; i + 1 < count(); i++)
            if (hasChildren(i))
                return true;
        return false;
    }

private:
    int top() const
    {
        int least = 0x7fffffff;
        for (int i = 0; i < count(); i++)
            if (_levels[i] < least)
                least = _levels[i];
        return least;
    }
    std::vector<int> _levels;
    std::vector<bool> _open;
};

#endif // PB_TOCTREE_H_INCLUDED
