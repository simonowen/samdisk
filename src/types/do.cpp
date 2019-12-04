// Apple ][ DOS 3.3-ordered disk image

#include "SAMdisk.h"

// not used
bool ReadDO(MemFile& file, std::shared_ptr<Disk>& disk)
{
    Format fmt{ RegularFormat::DO };

    // For now, rely on the file size and extension
    if (file.size() != fmt.disk_size() || !IsFileExt(file.name(), "do"))
        return false;

    file.rewind();
    disk->format(fmt, file.data());
    disk->strType = "DO";

    return true;
}

bool WriteDO(FILE* f_, std::shared_ptr<Disk>& disk)
{
    return WriteAppleDODisk(f_, *disk, RegularFormat::DO);
}
