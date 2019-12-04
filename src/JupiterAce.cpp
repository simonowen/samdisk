// Jupiter Ace helper functions for the Deep Thought floppy disk interface

#include "SAMdisk.h"

int GetDeepThoughtDataOffset(const Data& data)
{
    for (auto i = 0; i < data.size(); ++i)
    {
        // Check for 255 sync byte
        if (data[i] != 255)
            continue;

        // If 255 is followed by 42 we've found the data start
        if ((i + 2) < data.size() && data[i + 1] == 42)
            return i + 2;
    }

    // Not found
    return 0;
}

std::string GetDeepThoughtData(const Data& data)
{
    std::string str;

    auto offset = GetDeepThoughtDataOffset(data);
    if (offset != 0)
        str = std::string(reinterpret_cast<const char*>(data.data() + offset), data.size() - offset);

    return str;
}

bool IsDeepThoughtSector(const Sector& sector, int& offset)
{
    return sector.encoding == Encoding::Ace && sector.header.sector == 0 &&
        sector.header.size == SizeToCode(4096) &&
        (offset = GetDeepThoughtDataOffset(sector.data_copy())) > 0;
}

bool IsDeepThoughtDisk(Disk& disk, const Sector*& sector)
{
    // Try the primary catalogue location on cyl 0
    if (!disk.find(Header(0, 0, 0, SizeToCode(4096)), sector))
    {
        // Try the backup location on cyl 1
        if (!disk.find(Header(1, 0, 0, SizeToCode(4096)), sector))
            return false;
    }

    return true;
}

bool IsValidDeepThoughtData(const Data& data)
{
    auto offset = GetDeepThoughtDataOffset(data);
    if (!offset || offset == data.size())
        return false;

    uint8_t sum = 0;
    for (auto i = offset; i < data.size() - 1; ++i)
        sum += data[i];

    return sum == data[data.size() - 1];
}
