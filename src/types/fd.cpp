// FD - Thomson (TO8/TO8D/TO9/TO9+) sector dump

#include "SAMdisk.h"

#define THOMSON_SECTORS_PER_TRACK   16

struct FD_FORMAT
{
    int cyls;
    int size_code;
    Encoding encoding;
};

bool ReadFD(MemFile& file, std::shared_ptr<Disk>& disk)
{
    Format fmt;

    if (!IsFileExt(file.name(), "fd"))
        return false;

    switch (file.size())
    {
    case 655360:
        fmt = RegularFormat::TO_640K_MFM;
        break;

    case 327680:
        fmt = RegularFormat::TO_320K_MFM;
        break;

    case 163840:
        if (opt.encoding == Encoding::FM)
            fmt = RegularFormat::TO_160K_FM;
        else
            fmt = RegularFormat::TO_160K_MFM;
        break;

    case 81920:
        fmt = RegularFormat::TO_80K_FM;
        break;

    default:
        return false;
    }

    assert(fmt.disk_size() == file.size());
    disk->format(fmt, file.data(), fmt.cyls_first);
    disk->strType = "FD";
    return true;
}

bool WriteFD(FILE* f_, std::shared_ptr<Disk>& disk)
{
    auto track0 = disk->read_track(CylHead(0, 0));
    if (track0.size() != THOMSON_SECTORS_PER_TRACK)
        return false;

    auto track0_1 = disk->read_track(CylHead(0, 1));
    auto track40 = disk->read_track(CylHead(40, 0));

    Format fmt{ RegularFormat::TO_640K_MFM };
    fmt.cyls = track40.empty() ? 40 : 80;
    fmt.heads = track0_1.empty() ? 1 : 2;
    fmt.size = track0[0].header.size;
    fmt.encoding = track0[0].encoding;

    if (fmt.size != 0 && fmt.size != 1)
        return false;

    return WriteRegularDisk(f_, *disk, fmt);
}
