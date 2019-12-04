// Andrew Collier's Sam BooTable image for the SAM Coupe

#include "SAMdisk.h"

bool ReadSBT(MemFile& file, std::shared_ptr<Disk>& disk)
{
    auto file_size = file.size();
    char abBoot[4]{};

    // The file must fit on an MGT format disk
    if (file_size > MAX_SAM_FILE_SIZE)
        return false;

    // Read the potential boot signature (optional)
    if (file.seek(0x100 - MGT_FILE_HEADER_SIZE) && file.read(abBoot, sizeof(abBoot)))
    {
        // Clear bits 7+5
        for (size_t i = 0; i < sizeof(abBoot); i++)
            abBoot[i] &= ~0xa0;
    }

    // File must be marked bootable or have an explicit .sbt file extension
    if (memcmp(abBoot, "BOOT", sizeof(abBoot)) && !IsFileExt(file.name(), "sbt"))
        return false;

    file.rewind();

    // Allocate a full disk, and determine where the file header starts
    MEMORY mem(MGT_DISK_SIZE);
    uint8_t* pbD = mem;
    uint8_t* pbF = pbD + MGT_DIR_TRACKS * MGT_SECTORS * SECTOR_SIZE;
    uint8_t cyl = MGT_DIR_TRACKS, head = 0, sector = 1;
    auto block = SECTOR_SIZE - 2 - MGT_FILE_HEADER_SIZE;

    for (uint8_t* pb = pbF + MGT_FILE_HEADER_SIZE; file.read(pb, block, 1); )
    {
        // Determine the next sector in the file
        if (++sector > MGT_SECTORS) sector = 1;
        if (sector == 1 && ++cyl == MGT_TRACKS) cyl = 0;
        if (cyl == 0) head++;

        // Link this sector to it
        pb[block] = cyl | (head ? 0x80 : 0x00);
        pb[block + 1] = sector;

        pb += block + 2;
        block = SECTOR_SIZE - 2;
    }

    // Create a suitable file header
    pbF[0] = 19;                                    // CODE file type
    pbF[1] = static_cast<uint8_t>(file_size);       // LSB of size mod 16384
    pbF[2] = static_cast<uint8_t>(file_size >> 8);  // MSB of size mod 16384
    pbF[3] = 0x00;                                  // LSB of offset start
    pbF[4] = 0x80;                                  // MSB of offset start
    pbF[5] = 0xff;                                  // Unused
    pbF[6] = 0xff;                                  // Unused
    pbF[7] = static_cast<uint8_t>(file_size >> 14); // Number of pages (size div 16384)
    pbF[8] = 0x01;                                  // Starting page number

    // Now create a suitable directory entry

    // CODE file type
    pbD[0] = 19;

    // Use a fixed filename, starting with "auto" so DOS tries to boot it
    memcpy(pbD + 1, "autoExec  ", 10);

    // Number of sectors required
    auto sectors = (file_size + SECTOR_SIZE - 3) / (SECTOR_SIZE - 2);
    pbD[11] = static_cast<uint8_t>(sectors >> 8);
    pbD[12] = sectors & 0xff;

    // Starting track and sector
    pbD[13] = MGT_DIR_TRACKS;
    pbD[14] = 1;

    // Sector address map
    memset(pbD + 15, 0xff, sectors >> 3);
    if (sectors & 7)
        pbD[15 + (sectors >> 3)] = (1U << (sectors & 7)) - 1;

    // Starting page number and offset
    pbD[236] = pbF[8];
    pbD[237] = pbF[3];
    pbD[238] = pbF[4];

    // Size in pages and mod 16384
    pbD[239] = pbF[7];
    pbD[240] = pbF[1];
    pbD[241] = pbF[2];

    // Auto-execute code
    pbD[242] = 2;  // Normal paging (see PDPSUBR in ROM0 for details)
    pbD[243] = pbF[3];
    pbD[244] = pbF[4];

    file.open(mem, mem.size, "file.sbt");

    disk->format(RegularFormat::MGT, file.data(), true);
    disk->strType = "SBT";

    return true;
}
