// Catch recognised unsupported image types by file signatures.

#include "SAMdisk.h"

bool ReadUnsupported(MemFile& file, std::shared_ptr<Disk>&/*disk*/)
{
    std::array<char, SECTOR_SIZE> buf;
    if (!file.rewind() || !file.read(buf))
        return false;

    std::string strType;

    if (std::string(buf.data() + 0x00, 12) == "CPC-Emulator" && std::string(buf.data() + 0x10, 10) == "DiskImageV")
        strType = "CPCemu";
    else if (std::string(buf.data() + 0x00, 4) == "CPCD")
        strType = "EmuCPC";
    else if (std::string(buf.data() + 0x00, 8) == "NORMDISK")
        strType = "CPD";

    if (!strType.empty())
        throw util::exception(strType, " disk images are not currently supported");

    return false;
}
