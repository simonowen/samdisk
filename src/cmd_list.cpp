// List command

#include "SAMdisk.h"
#include "BlockDevice.h"

static const char* aszPartTypes[256] =
{
    "Empty", "FAT12", "Xenix root", "Xenix /usr", "FAT16 <32M", "(Extended)", "FAT16 32M+", "NTFS/HPFS",//00-07
    NULL, NULL, "OS/2 Boot", "FAT32", "FAT32 LBA", NULL, "VFAT LBA", "(Extended LBA)",                  //08-0f
    NULL, "FAT12 Hidden", "Compaq", NULL, "FAT16 <32M Hidden", NULL, "FAT16 32M+ Hidden", "NTFS Hidden",//10-17
    NULL, NULL, NULL, "FAT32 Hidden", "FAT32 LBA Hidden", NULL, "VFAT LBA Hidden", NULL,                //18-1f
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, "NTFS Hidden",                                            //20-27
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //28-2f
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //30-27
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //38-2f
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //40-47
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //48-4f
    NULL, NULL, "CP/M", NULL, NULL, NULL, NULL, NULL,                                                   //50-57
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //58-5f
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //60-67
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //68-6f
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //70-77
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //78-7f
    NULL, NULL, "Linux swap", "Linux native", NULL, NULL, NULL, NULL,                                   //80-87
    NULL, NULL, NULL, NULL, NULL, NULL, "Linux LVM", NULL,                                              //88-8f
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //90-97
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //98-9f
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //a0-a7
    "Mac OSX", NULL, NULL, NULL, NULL, NULL, NULL, "Mac OSX HFS+",                                      //a8-af
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //b0-b7
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //b8-bf
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //c0-c7
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //c8-cf
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //d0-d7
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //d8-df
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //e0-e7
    NULL, NULL, NULL, "BeOS BFS", NULL, NULL, "EFI Protective", NULL,                                   //e8-ef
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,                                                     //f0-f7
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL                                                      //f8-ff
};


std::string AbbreviateSize(int64_t total_bytes)
{
    static const std::vector<std::string> units = { "KB", "MB", "GB", "TB", "PB", "EB" };

    // Work up from Kilobytes
    auto unit_idx = 0;
    total_bytes /= 1000;

    // Loop while there are more than 1000 and we have another unit to move up to
    while (total_bytes >= 1000)
    {
        // Determine the percentage error/loss in the next scaling
        auto clip_percent = (total_bytes % 1000) * 100 / (total_bytes - (total_bytes % 1000));

        // Stop if it's at least 20%
        if (clip_percent >= 20)
            break;

        // Next unit, rounding to nearest
        ++unit_idx;
        total_bytes = (total_bytes + 500) / 1000;
    }

    return util::format(total_bytes, units[unit_idx]);
}

void OutputMbrPartitionInfo(uint8_t* pb_, int64_t base_sector = 0)
{
    uint8_t bStatus = pb_[0];
    uint8_t bType = pb_[4];
    int64_t start_sector = (pb_[11] << 24) | (pb_[10] << 16) | (pb_[9] << 8) | pb_[8];
    int64_t num_sectors = (pb_[15] << 24) | (pb_[14] << 16) | (pb_[13] << 8) | pb_[12];

    start_sector += base_sector;

    if (aszPartTypes[bType])
        util::cout << colour::green << aszPartTypes[bType] << colour::none;
    else
        util::cout << colour::green << util::fmt("%02X", bType) << colour::none;

    util::cout << ", start" << colour::yellow << ":" << colour::none << start_sector <<
        ", len" << colour::yellow << ':' << colour::none << num_sectors <<
        " = " << AbbreviateSize(num_sectors * SECTOR_SIZE);

    if (bStatus == 0x80)
        util::cout << ", " << colour::GREEN << "bootable" << colour::none;

    util::cout << '\n';
}

void OutputIdedosPartitionInfo(uint8_t* pb_, int /*cyls*/, int heads, int sectors)
{
    auto type = pb_[0x10];
    const char* type_str = nullptr;

    switch (type)
    {
    case 0x00:  type_str = "Unused";    break;
    case 0x01:  type_str = "System";    break;
    case 0x02:  type_str = "Swap";      break;
    case 0x03:  type_str = "+3DOS";     break;
    case 0x04:  type_str = "CP/M";      break;
    case 0x05:  type_str = "Boot";      break;

    case 0x0f:  type_str = "Movie";     break;
    case 0x10:  type_str = "FAT-16";    break;

    case 0x20:  type_str = "UZI(X)";    break;

    case 0x30:  type_str = "TR-DOS Image";      break;
    case 0x31:  type_str = "+D/SAMDOS Image";   break;
    case 0x32:  type_str = "MB-02 Image";       break;
    case 0x33:  type_str = "TOS A.2 Image";     break;

    case 0x40:  type_str = "+3 Image";      break;
    case 0x41:  type_str = "Elwro Image";   break;
    case 0x48:  type_str = "CPC Image";     break;
    case 0x49:  type_str = "PCW Image";     break;

    case 0xfe:  type_str = "BAD";           break;
    case 0xff:  type_str = "Free";          break;
    }

    auto start_cyl = (pb_[0x12] << 8) | pb_[0x11];
    auto start_head = pb_[0x13];
    auto start_lba = (start_cyl * heads + start_head) * sectors;
    /*
        auto end_cyl = (pb_[0x15] << 8) | pb_[0x14];
        auto end_head = pb_[0x16];
    */
    int64_t total_sectors = ((pb_[0x1a] << 24) | (pb_[0x19] << 16) | (pb_[0x18] << 8) | pb_[0x17]) + 1; // +1 for MBR

    if (type_str)
        util::cout << colour::green << type_str << colour::none;
    else
        util::cout << colour::green << util::fmt("%02X ??", type) << colour::none;

    util::cout << ", start" << colour::yellow << ':' << colour::none <<
        start_lba << " (" << start_cyl << '/' << start_head << "/1)" <<
        ", len" << colour::yellow << ':' << colour::none << total_sectors << " = " <<
        AbbreviateSize(total_sectors * SECTOR_SIZE);

    if (type == 0x05)
        util::cout << ", " << colour::GREEN << "bootable" << colour::none;

    util::cout << '\n';
}


void ListDrive(const std::string& path, const HDD& hdd, int verbose)
{
    // Show the device name
    if (!path.empty())
        util::cout << colour::YELLOW << path << ':' << colour::none << "\n";

    // Show make/model, if available
    if (!hdd.strMakeModel.empty())
        util::cout << " Name:      " << colour::CYAN << hdd.strMakeModel << colour::none << '\n';

    // Show serial number, if available
    if (verbose && !hdd.strSerialNumber.empty())
        util::cout << " Serial:    " << hdd.strSerialNumber << '\n';

    // Show firmware revision, if available
    if (verbose && !hdd.strFirmwareRevision.empty())
        util::cout << " Firmware:  " << hdd.strFirmwareRevision << '\n';

    util::cout << " Capacity:  ";

    if (hdd.total_bytes)
    {
        std::string sSize = AbbreviateSize(hdd.total_bytes);

        if (hdd.sector_size == SECTOR_SIZE)
            util::cout << util::fmt("%llu bytes = %llu sectors = ", hdd.total_bytes, hdd.total_bytes / hdd.sector_size) << colour::WHITE << sSize << colour::none << '\n';
        else if (hdd.sector_size == 4096)
            util::cout << util::fmt("%llu bytes = %llu 4K sectors = ", hdd.total_bytes, hdd.total_bytes / hdd.sector_size) << colour::WHITE << sSize << colour::none << '\n';
        else
            util::cout << util::fmt("%llu bytes = %llu %u-byte sectors = ", hdd.total_bytes, hdd.total_bytes / hdd.sector_size, hdd.sector_size) << colour::WHITE << sSize << colour::none << '\n';

        // Show CHS unless it's the maximum value, as found on 8GB+ disks
        if (verbose && (hdd.cyls != 16383 || hdd.heads != 16 || hdd.sectors != 63))
        {
            util::cout << util::fmt(" Geometry:  %u Cyls, %u Heads, %u Sectors\n", hdd.cyls, hdd.heads, hdd.sectors);

            // In debug mode check the geometry against the calculated method
            if (opt.debug)
            {
                int uC, uH, uS;
                CalculateGeometry(hdd.total_sectors, uC, uH, uS);

                if (uC != hdd.cyls || uH != hdd.heads || uS != hdd.sectors)
                    util::cout << util::fmt(" Old Geom:  %u Cyls, %u Heads, %u Sectors\n", uC, uH, uS);
            }
        }
    }

    BDOS_CAPS bdc;
    bool fBDOS = IsBDOSDisk(hdd, bdc);

    PRODOS_CAPS pdc;
    bool fProDOS = IsProDOSDisk(hdd, pdc);

    if (fProDOS)
    {
        util::cout << " Format:    Pro-DOS, " << colour::GREEN << pdc.records << " records" << colour::none;
        if (pdc.bootable) util::cout << ", bootable";
        util::cout << '\n';
    }
    else if (fBDOS)
    {
        auto type = bdc.lba ? "Trinity" : (bdc.need_byteswap ? "Atom" : "Atom Lite");
        util::cout << " Format:    BDOS (" << type << "), " << colour::GREEN << bdc.records << " records" << colour::none;
        if (bdc.records == 65535) util::cout << " (MAX)";
        if (bdc.bootable) util::cout << ", bootable";
        util::cout << '\n';
    }
    else if (IsSDIDEDisk(hdd))
        util::cout << " Format:    HDOS\n";

    // Only show the volume list in verbose mode
    if (verbose)
    {
        // Fetch the list of volumes located on the current device
        auto lVolumes = hdd.GetVolumeList();

        if (lVolumes.size())
        {
            std::string sVolumes;
            for (size_t i = 0; i < lVolumes.size(); ++i)
            {
                std::string sVolume = lVolumes.at(i);

                char szVolName[64] = "";
#ifdef _WIN32
                if (!GetVolumeInformation(sVolume.c_str(), szVolName, sizeof(szVolName), NULL, NULL, NULL, NULL, 0))
#endif
                    szVolName[0] = '\0';

                if (szVolName[0])
                    sVolumes += sVolume + " (" + szVolName + ")  ";
                else
                    sVolumes += sVolume + "  ";
            }

            util::cout << " Volumes:   " << sVolumes << '\n';
        }
    }

    // Only show the partition list in extra verbose mode
    if (verbose)
    {
        MEMORY mbr(hdd.sector_size);

        // Read MBR and check for AA55 signature, but ignore suspected boot sectors
        if (hdd.Seek(0) && hdd.Read(mbr, 1) &&
            mbr[mbr.size - 2] == 0x55 && mbr[mbr.size - 1] == 0xaa &&
            memcmp(mbr.pb + 3, "MS", 2) && memcmp(mbr.pb + 3, "NT", 2) &&   // Microsoft signatures
            memcmp(mbr.pb + 3, "AL", 2) && memcmp(mbr.pb + 3, "PRODOS", 6)) // Atom Lite and Pro-DOS signatures
        {
            // Process the 4 primary partition entries
            for (int p = 0; p < 4; ++p)
            {
                // Calculate offset to partition entry
                size_t i = 446 + p * 16;

                // Read the partition type, skip if it's zero (unused)
                uint8_t bType = mbr[i + 4];
                if (!bType)
                    continue;

                util::cout << util::fmt("%s%d = ", p ? "            " : " MBR:       ", p + 1);
                OutputMbrPartitionInfo(mbr.pb + i);

                // Extended partition?
                if (bType == 0x05 || bType == 0x0f)
                {
                    MEMORY ebr(SECTOR_SIZE);

                    // Locate the base EBR starting the extended partition chain
                    int64_t base_sector = (mbr[i + 11] << 24) | (mbr[i + 10] << 16) | (mbr[i + 9] << 8) | mbr[i + 8];
                    int64_t next_offset = 0;

                    // Loop as long as we can follow the EBR chain
                    while (hdd.Seek(base_sector + next_offset) && hdd.Read(ebr, 1))
                    {
                        // First partition entry (logical partition)
                        i = 446;
                        util::cout << "                ";
                        OutputMbrPartitionInfo(ebr.pb + i, base_sector + next_offset);

                        // Advance to 2nd entry (chain pointer)
                        i += 16;

                        // Read the offset for the next link in the chain, relative to the extended partition
                        next_offset = (ebr[i + 11] << 24) | (ebr[i + 10] << 16) | (ebr[i + 9] << 8) | ebr[i + 8];
                        if (!next_offset)
                            break;
                    }
                }
            }
        }

        // Check for IDEDOS system partition label+type in LBR sector 0 or 1
        if (hdd.Seek(0) && hdd.Read(mbr, 1) &&
            (!memcmp(mbr.pb, "PLUSIDEDOS      \x01", 17) ||
            (hdd.Read(mbr, 1) && !memcmp(mbr.pb, "PLUSIDEDOS      \x01", 17))))
        {
            uint8_t* pb = mbr;

            auto cyls = (pb[0x21] << 8) | pb[0x20];
            auto heads = pb[0x22];
            auto sectors = pb[0x23];
            auto partitions = (pb[0x27] << 8) | pb[0x26];

            // Show geometry if different from HDD, plus maximum partition count
            if (cyls != hdd.cyls || heads != hdd.heads || sectors != hdd.sectors)
                util::cout << util::fmt(" IDEDOS:    %u Cyls, %u Heads, %u Sectors, %u partitions (max)\n", cyls, heads, sectors, partitions);
            else
                util::cout << util::fmt(" IDEDOS:    %u partition slots\n", partitions);

            // Loop over each partition slot
            for (auto i = 0; i < partitions; ++i)
            {
                // Check the partition exists
                if (pb[0x10] != 0x00)
                {
                    // Extract the partition name
                    std::string sName = util::trim(std::string(reinterpret_cast<const char*>(pb), 16));

                    // Indent and write the partition number and name
                    util::cout << "             " << i << ": " << colour::CYAN << sName << colour::none << '\n';

                    // Show the partition details
                    OutputIdedosPartitionInfo(pb, cyls, heads, sectors);
                }

                // Advance to the next partition
                pb += 0x40;

                // Are we beyond the current sector?
                if ((pb - mbr) >= hdd.sector_size)
                {
                    // Read the next sector, and move the pointer back
                    hdd.Read(mbr, 1);
                    pb -= hdd.sector_size;
                }
            }
        }
    }

    // Tag the identify data size on the end
    // If called from the 'info' command the data will be displayed after it
    if (verbose && hdd.sIdentify.len)
    {
        util::cout << " Identify:  " << hdd.sIdentify.len << " bytes\n";
    }
}


bool ListDrives(int nVerbose_)
{
    int num_opened = 0;

    std::vector<std::string> lDevices = BlockDevice::GetDeviceList();

    for (size_t u = 0; u < lDevices.size(); ++u)
    {
        std::string sDevice = lDevices.at(u);

        auto hdd = HDD::OpenDisk(sDevice);
        if (hdd)
        {
            ++num_opened;
            ListDrive(sDevice, *hdd, nVerbose_);
        }
    }

    if (!lDevices.size())
        util::cout << "No devices found.\n";

    else if (!num_opened)
#ifdef _WIN32
        util::cout << "No drives found. Run as Administrator to list system drives.\n";
#else
        util::cout << "No drives found -- need root?\n";
#endif

    return true;
}


bool ListRecords(const std::string& path)
{
    auto hdd = HDD::OpenDisk(path);

    if (!hdd)
    {
        Error("open");
        return false;
    }

    BDOS_CAPS bdc;
    if (!IsBDOSDisk(*hdd, bdc))
        util::cout << "BDOS disk signature not found\n";
    else
    {
        int nNamed = 0;
        util::cout << util::fmt("Atom%s, %d records:\n\n", bdc.need_byteswap ? "" : " Lite", bdc.records);

        for (auto i = 0; i < bdc.list_sectors; ++i)
        {
            MEMORY mem(hdd->sector_size);
            hdd->Seek(bdc.base_sectors - bdc.list_sectors + i);
            hdd->Read(mem, 1, bdc.need_byteswap);

            for (auto j = 0; j < (hdd->sector_size / BDOS_LABEL_SIZE); ++j)
            {
                uint8_t abRecord[BDOS_LABEL_SIZE + 1] = {};
                memcpy(abRecord, mem + (j << 4), BDOS_LABEL_SIZE);

                // Bit 7 should be ignored on label names
                for (int k = 0; k < BDOS_LABEL_SIZE; ++k)
                    abRecord[k] &= 0x7f;

                // Label in use?
                if (abRecord[0])
                {
                    util::cout << util::fmt("%5u : %s\n", 1 + (i << 5) + j, abRecord);
                    ++nNamed;
                }
            }
        }

        if (!nNamed)
            util::cout << " No named records found\n";
    }

    return true;
}
