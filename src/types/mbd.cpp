// MB-02 Spectrum disk interface:
//  http://busy.host.sk/tvorba/mb02form.htm
//  http://z00m.speccy.cz/docs/bsdos308-techman-en.txt (English)

#include "SAMdisk.h"

struct MBD_BOOTSECTOR
{
    uint8_t abJpBoot[2];        // Jump to boot routine
    uint8_t bUnused;
    uint8_t bSig1;              // 0x02 (MB-02 signature 1)
    uint8_t abTracks[2];        // Tracks per side
    uint8_t abSectors[2];       // Sectors per track
    uint8_t abSides[2];         // Disk sides
    uint8_t abSecPerClust[2];   // Sectors per cluster
    uint8_t abDirSectors[2];    // Sectors per directory
    uint8_t abFatSectors[2];    // Sectors per FAT
    uint8_t abFatLen[2];        // FAT length
    uint8_t abFAT1Pos[2];       // Logical sector for 1st FAT
    uint8_t abFat2Pos[2];       // Logical sector for 2nd FAT
    uint8_t abUnknown[10];
    uint8_t bSig2;              // 0x00 (MB-02 signature 2)
    uint8_t date[2];            // Creation date
    uint8_t time[2];            // Creation time
    uint8_t bSig3;              // 0x00 (MB-02 signature 3)
    char szDiskName[10];
    char szDiskNameEx[16];
    char szSystemId[32];
    uint8_t abBootCode[88];
};


bool ReadMBD(MemFile& file, std::shared_ptr<Disk>& disk)
{
    // Make sure we can read the boot information
    MBD_BOOTSECTOR bs;
    if (!file.rewind() || !file.read(&bs, sizeof(bs)))
        return false;

    // Check the 3 signature markers
    if (bs.bSig1 != 0x02 || bs.bSig2 != 0x00 || bs.bSig3 != 0x00)
        return false;

    // Pick up the geometry details from the boot sector
    auto cyls = (bs.abTracks[1] << 8) | bs.abTracks[0];
    auto heads = (bs.abSides[1] << 8) | bs.abSides[0];
    auto sectors = (bs.abSectors[1] << 8) | bs.abSectors[0];

    Format fmt820{ RegularFormat::MBD820 };
    Format fmt1804{ RegularFormat::MBD1804 };
    Format fmt = (sectors <= fmt820.sectors) ? fmt820 : fmt1804;
    fmt.cyls = cyls;
    fmt.heads = heads;
    fmt.sectors = sectors;

    // Check the image size is correct
    if (file.size() != fmt.disk_size())
        return false;

    fmt.Validate();

    file.rewind();
    disk->format(fmt, file.data());
    disk->strType = "MBD";

    return true;
}

bool WriteMBD(FILE* f_, std::shared_ptr<Disk>& disk)
{
    Format fmt{ RegularFormat::MBD820 };

    const Sector* ps = nullptr;
    if (!disk->find(Header(0, 0, fmt.base, fmt.size), ps) || ps->data_size() < static_cast<int>(sizeof(MBD_BOOTSECTOR)))
        return false;

    auto pbs = reinterpret_cast<const MBD_BOOTSECTOR*>(ps->data_copy().data());
    if (pbs->bSig1 != 0x02 || pbs->bSig2 != 0x00 || pbs->bSig3 != 0x00)
        return false;

    auto cyls = (pbs->abTracks[1] << 8) | pbs->abTracks[0];
    auto heads = (pbs->abSides[1] << 8) | pbs->abSides[0];
    auto sectors = (pbs->abSectors[1] << 8) | pbs->abSectors[0];

    if (sectors > fmt.sectors)
        fmt = RegularFormat::MBD1804;

    fmt.cyls = static_cast<uint8_t>(cyls);
    fmt.heads = static_cast<uint8_t>(heads);
    fmt.sectors = static_cast<uint8_t>(sectors);
    fmt.Validate();

    return WriteRegularDisk(f_, *disk, fmt);
}
