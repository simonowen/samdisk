// Basic support for SAM Coupe Pro-DOS images

#include "SAMdisk.h"
#include "types.h"

bool ReadCPM(MemFile& file, std::shared_ptr<Disk>& disk)
{
    Format fmt = RegularFormat::ProDos;

    // 720K images with a .cpm extension use the SAM Coupe Pro-Dos parameters
    if (file.size() != fmt.disk_size() || !IsFileExt(file.name(), "cpm"))
        return false;

    file.rewind();
    disk->format(fmt, file.data());
    disk->strType = "Pro-DOS";

    return true;
}

bool WriteCPM(FILE* f_, std::shared_ptr<Disk>& disk)
{
    return WriteRegularDisk(f_, *disk, RegularFormat::ProDos);
}
