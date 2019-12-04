#include "SAMdisk.h"
#include "Range.h"

std::string to_string(const Range& range)
{
    std::ostringstream ss;
    auto separator = ", ";

    if (range.empty())
        return "All Tracks";

    if (range.cyls() == 1)
        ss << "Cyl " << CylStr(range.cyl_begin);
    else if (range.cyl_begin == 0)
    {
        ss << std::setw(2) << range.cyl_end << " Cyls";
        separator = " ";
    }
    else
        ss << "Cyls " << CylStr(range.cyl_begin) << '-' << CylStr(range.cyl_end - 1);

    if (range.heads() == 1)
        ss << " Head " << range.head_begin;
    else if (range.head_begin == 0)
        ss << separator << range.head_end << " Heads";
    else
        ss << " Heads " << range.head_begin << '-' << (range.head_end - 1);

    return ss.str();
}


Range::Range(int num_cyls, int num_heads)
    : Range(0, num_cyls, 0, num_heads)
{
}

Range::Range(int cyl_begin_, int cyl_end_, int head_begin_, int head_end_)
    : cyl_begin(cyl_begin_), cyl_end(cyl_end_), head_begin(head_begin_), head_end(head_end_)
{
    assert(cyl_begin >= 0 && cyl_begin <= cyl_end);
    assert(head_begin >= 0 && head_begin <= head_end);
}

bool Range::empty() const
{
    return cyls() <= 0 || heads() <= 0;
}

int Range::cyls() const
{
    return cyl_end - cyl_begin;
}

int Range::heads() const
{
    return head_end - head_begin;
}

bool Range::contains(const CylHead& cylhead)
{
    return cylhead.cyl >= cyl_begin && cylhead.cyl < cyl_end &&
        cylhead.head >= head_begin && cylhead.head < head_end;
}

void Range::each(const std::function<void(const CylHead & cylhead)>& func, bool cyls_first/*=false*/) const
{
    if (cyls_first && heads() > 1)
    {
        for (auto head = head_begin; head < head_end; ++head)
            for (auto cyl = cyl_begin; cyl < cyl_end; ++cyl)
                func(CylHead(cyl, head));
    }
    else
    {
        for (auto cyl = cyl_begin; cyl < cyl_end; ++cyl)
            for (auto head = head_begin; head < head_end; ++head)
                func(CylHead(cyl, head));
    }
}
