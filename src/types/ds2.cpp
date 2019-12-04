// Velesoft's split-side transfer format for the SAM Coupe:
//  http://velesoft.speccy.cz/atom_hdd-cz.htm

#include "SAMdisk.h"

bool ReadDS2(MemFile& file, std::shared_ptr<Disk>& disk)
{
    std::string path = file.path();
    MemFile file2;

    Format fmt{ RegularFormat::MGT };

    // The input should be half a standard SAM disk in size
    if (!file.rewind() || file.size() != fmt.disk_size() / 2)
        return false;

    // Require the supplied file be head 0
    if (!IsFileExt(path, "dsk"))
        return false;

    // DSK->DS2
    size_t offset = path.length() - 1;
    path[offset] = '2';

    // The DS2 file must also be present
    try {
        file2.open(path);
    }
    catch (...) {
        return false;
    }

    // The companion file should also contain a single side
    if (file2.size() != fmt.disk_size() / 2)
        throw util::exception(path, " file size is incorrect");

    // Join the sides
    Data data(file.data().begin(), file.data().end());
    data.insert(data.end(), file2.data().begin(), file2.data().end());

    disk->format(fmt, data, true);
    disk->strType = "DS2";

    return true;
}
