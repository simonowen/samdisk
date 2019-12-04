// Aley Keprt's SAM Coupe Disk format

#include "SAMdisk.h"

#define SAD_SIGNATURE       "Aley's disk backup"

typedef struct
{
    uint8_t abSignature[sizeof(SAD_SIGNATURE) - 1];
    uint8_t heads, cyls, sectors, size_div_64;
} SAD_HEADER;


bool ReadSAD(MemFile& file, std::shared_ptr<Disk>& disk)
{
    SAD_HEADER sh;
    if (!file.rewind() || !file.read(&sh, sizeof(sh)))
        return false;

    if (memcmp(sh.abSignature, SAD_SIGNATURE, sizeof(sh.abSignature)))
        return false;

    Format fmt{ RegularFormat::MGT };
    fmt.cyls = sh.cyls;
    fmt.heads = sh.heads;
    fmt.sectors = sh.sectors;
    fmt.size = SizeToCode(sh.size_div_64 << 6);
    fmt.Validate();

    // If it doesn't appear to be SAM format, clear the MGT skew+gap3
    if (fmt.sectors != MGT_SECTORS || fmt.sector_size() != SECTOR_SIZE)
        fmt.skew = fmt.gap3 = 0;

    Data data(file.data().begin() + sizeof(SAD_HEADER), file.data().end());
    if (data.size() != fmt.disk_size())
        Message(msgWarning, "data size (%zu) differs from expected size (%zu)", data.size(), fmt.disk_size());

    disk->format(fmt, data, true);
    disk->strType = "SAD";

    return true;
}

bool WriteSAD(FILE* f_, std::shared_ptr<Disk>& disk)
{
    const Track& track0 = disk->read_track(CylHead(0, 0));
    auto cyls = disk->cyls();
    auto heads = disk->heads();
    auto sectors = track0.size();
    auto size = sectors ? track0[0].header.size : 0;

    Format fmt{ RegularFormat::MGT };
    fmt.cyls = cyls;
    fmt.heads = heads;
    fmt.sectors = sectors;
    fmt.size = size;
    fmt.cyls_first = true;

    fmt.Override(true);
    fmt.Validate();

    SAD_HEADER sh;
    std::memcpy(sh.abSignature, SAD_SIGNATURE, sizeof(sh.abSignature));
    sh.cyls = static_cast<uint8_t>(fmt.cyls);
    sh.heads = static_cast<uint8_t>(fmt.heads);
    sh.sectors = static_cast<uint8_t>(fmt.sectors);
    sh.size_div_64 = static_cast<uint8_t>(fmt.sector_size() >> 6);

    if (!fwrite(&sh, sizeof(sh), 1, f_))
        throw posix_error();

    return WriteRegularDisk(f_, *disk, fmt);
}
