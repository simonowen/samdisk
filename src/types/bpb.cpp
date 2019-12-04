// BIOS Parameter Block, for MS-DOS and compatible disks

#include "SAMdisk.h"
#include "bpb.h"

bool ReadBPB(MemFile& file, std::shared_ptr<Disk>& disk)
{
    BIOS_PARAMETER_BLOCK bpb{};
    if (!file.rewind() || !file.read(&bpb, sizeof(bpb)))
        return false;

    // Check for a sensible media byte
    if (bpb.bMedia != 0xf0 && bpb.bMedia < 0xf8)
        return false;

    // Extract the full geometry
    auto total_sectors = util::le_value(bpb.abSectors);
    auto sector_size = util::le_value(bpb.abBytesPerSec);
    auto sectors = util::le_value(bpb.abSecPerTrack);
    auto heads = util::le_value(bpb.abHeads);
    auto cyls = (sectors && heads) ? (total_sectors / (sectors * heads)) : 0;

    Format fmt{ RegularFormat::PC720 };
    fmt.cyls = static_cast<uint8_t>(cyls);
    fmt.heads = static_cast<uint8_t>(heads);
    fmt.sectors = static_cast<uint8_t>(sectors);
    fmt.size = SizeToCode(sector_size);
    fmt.gap3 = 0;   // auto
    if (!fmt.TryValidate())
        return false;

    if (fmt.track_size() < 6000)
        fmt.datarate = DataRate::_250K;
    else if (fmt.track_size() < 12000)
        fmt.datarate = DataRate::_500K;
    else
        fmt.datarate = DataRate::_1M;

    // Reject disks larger than geometry suggests, but accept space-saver truncated images
    if (file.size() > fmt.disk_size())
        return false;

    file.rewind();
    disk->format(fmt, file.data());
    disk->strType = "BPB";

    return true;
}
