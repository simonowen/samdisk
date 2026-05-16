// Dir command

#include "SAMdisk.h"
#include "JupiterAce.h"
#include "opd.h"
#include "trd.h"

#define MGT_DATE_COLUMN     48

template <typename T>
bool IsMgtDirEntry(const uint8_t* p, T& bitmap)
{
    auto dir = reinterpret_cast<const MGT_DIR*>(p);
    auto type = dir->bType & 0x3f;

    // Unused entries are valid
    if (!type)
        return true;

    // Reject unknown file types
    if (type >= 32 || !(0x003f3fff & (1 << type)))
        return false;

    // Add more checks here

    for (size_t i = 0; i < sizeof(dir->abSectorMap); ++i)
    {
        // Fail if the sector bitmap overlaps
        if (bitmap[i] & dir->abSectorMap[i])
            return false;

        // Merge in the new bits
        bitmap[i] |= dir->abSectorMap[i];
    }

    return true;
}

bool IsMgtDirSector(const Sector& sector)
{
    std::array<uint8_t, sizeof(MGT_DIR().abSectorMap)> disk_bitmap{};

    // Check sector number and size
    if (sector.header.sector != 1 || sector.header.size != 2 || sector.data_size() < 512)
        return false;

    auto& data = sector.data_copy();
    /*
        // If the first entry is unused, accept only if the whole sector is blank?
        if (!*pb)
        {
            for (size_t u = 1 ; u < ps_->uData ; ++u)
                if (pb[u])
                    return false;
        }
    */

    auto it = data.begin();
    if (*it)
    {
        // Reject if not matched
        if (!IsMgtDirEntry(&*it, disk_bitmap))
            return false;

        it = std::next(it, sizeof(MGT_DIR));

        // Examine second entry and reject if not matched
        if (*it && !IsMgtDirEntry(&*it, disk_bitmap))
            return false;
    }

    return true;
}

bool DirMgtEntry(int n_, const MGT_DIR* p_, bool fHidden_)
{
    std::stringstream ss;

    // Copy the filename so we can null-terminate it
    char szName[11];
    memcpy(szName, p_->abName, sizeof(p_->abName));
    szName[sizeof(p_->abName)] = '\0';

    char szVar[16];
    memcpy(szVar, p_->abFileInfo + 1, sizeof(p_->abFileInfo) - 1);
    szVar[p_->abFileInfo[0] & 0x0f] = '\0';

    auto sectors = (p_->bSectorsHigh << 8) | p_->bSectorsLow;
    uint8_t bFlags = ((p_->bType & 0xc0) == 0xc0) ? '*' : (p_->bType & 0x80) ? '-' : (p_->bType & 0x40) ? '+' : ' ';
    auto file_num = 1 + n_ - ((n_ > 80) ? 2 : 0);
    ss << util::fmt("%3d%c %-10s %4u  ", file_num, bFlags, szName, sectors);

    auto file_type = p_->bType & 0x3f;

    switch (file_type)
    {
    case 16: ss << "BASIC"; if (p_->bExecutePage < 0xff) ss << util::fmt(" %5d", (p_->bExecAddrHigh << 8) | p_->bExecAddrLow); break;
    case 17: ss << util::fmt("DATA  %s()", szVar); break;
    case 18: ss << util::fmt("DATA  %s$", szVar); if (p_->abFileInfo[0] & 0x20) ss << "()"; break;
    case 19: ss << util::fmt("CODE %6u,%u", TPeek(&p_->bStartPage, 16384), TPeek(&p_->bLengthInPages)); if (p_->bExecutePage < 0xff) ss << util::fmt(",%d", TPeek(&p_->bExecutePage)); break;
    case 20: ss << util::fmt("SCREEN$ [mode %u]", p_->abFileInfo[0] + 1); break;
    case 21: ss << util::fmt("<DIR>"); break;

    case  1: ss << "ZX BASIC"; if (p_->zx.abZXExec[1] < 0xff) ss << util::fmt(" %5d", (p_->zx.abZXExec[1] << 8) | p_->zx.abZXExec[0]); break;
    case  2: ss << util::fmt("ZX DATA  %c()", 'a' + (p_->zx.abZXUnk2[0] & 0x7f)); break;
    case  3: ss << util::fmt("ZX DATA  %c$()", 'a' + (p_->zx.abZXUnk2[0] & 0x3f) - 1); break;
    case  4: ss << util::fmt("ZX CODE %6u,%u", (p_->zx.abZXStart[1] << 8) | p_->zx.abZXStart[0], (p_->zx.abZXLength[1] << 8) | p_->zx.abZXLength[0]);
        if (p_->zx.abZXExec[1] && p_->zx.abZXExec[1] != 0xff)
            ss << util::fmt(",%u", (p_->zx.abZXExec[1] << 8) | p_->zx.abZXExec[0]);
        break;

    default:
    {
        static const char* szTypes[] = {
            nullptr, nullptr, nullptr, nullptr, nullptr, "ZX SNP 48K", "ZX MDRV", "ZX SCREEN$",
            "SPECIAL", "ZX SNP 128K", "OPENTYPE", "ZX EXECUTE", "UNIDOS DIR", "UNIDOS CREATE", "", "",
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "DRIVER APP", "DRIVER BOOT",
            "EDOS NOMEN", "EDOS SYSTEM", "EDOS OVERLAY", nullptr, "HDOS DOS", "HDOS DIR", "HDOS DISK", "HDOS TEMP"
        };

        if (file_type < static_cast<int>(arraysize(szTypes)) && szTypes[file_type])
            ss << szTypes[file_type];
        else
            ss << "WHAT?";

        break;
    }
    }

    struct tm t;
    std::string str = ss.str();
    if (str.size() < MGT_DATE_COLUMN && GetFileTime(p_, &t))
    {
        str += std::string(MGT_DATE_COLUMN - str.size(), ' ');
        str += util::fmt("%02u/%02u/%04u %02u:%02u", t.tm_mday, 1 + t.tm_mon, t.tm_year + 1900, t.tm_hour, t.tm_min);
    }

    if (fHidden_)
        util::cout << colour::cyan << str << colour::none << "\n";
    else
        util::cout << str << "\n";

    return true;
}


struct TRDOS_DIR
{
    uint8_t abName[8];      // Name
    uint8_t bExt;           // Type/extension
    uint8_t abStart[2];     // Start address
    uint8_t abLen[2];       // Length in bytes
    uint8_t bSectors;       // Length in sectors
    uint8_t bStartSector;   // Starting sector
    uint8_t bStartTrack;    // Starting track
};

bool IsTrDosDirEntry(const TRDOS_DIR* pd_)
{
    // accept unused (0x00) and deleted (0x01) entries
    if (pd_->abName[0] <= 0x01)
        return true;

    // Reject bad starting locations
    if (pd_->bStartSector >= 16 || !pd_->bStartTrack || pd_->bStartTrack >= TRD_MAX_TRACKS)
        return false;

    // Sector length shouldn't be zero
    if (!pd_->bSectors)
        return false;

    return true;
}

bool IsTrDosDirSector(const Sector& sector)
{
    auto& data = sector.data_copy();

    // Check sector number and size
    if (sector.header.sector != 1 || sector.header.size != 1)
        return false;

    auto dir_entries = sector.size() / sizeof(TRDOS_DIR);
    auto pd = reinterpret_cast<const TRDOS_DIR*>(data.data());

    for (size_t entry = 0; entry < dir_entries; ++entry, ++pd)
    {
        if (!IsTrDosDirEntry(pd))
            return false;
    }

    return true;
}

bool DirTrDos(Disk& disk)
{
    auto& sector9 = disk.get_sector(Header(0, 0, 9, 1));
    auto& data9 = sector9.data_copy();

    util::cout << util::fmt(" Title: %-11.11s\n\n", &data9[245]);
    util::cout << " File Name         Start Length Line\n";

    for (uint8_t i = 1; i <= 8; ++i)
    {
        auto& sector = disk.get_sector(Header(0, 0, i, 1));
        auto& data = sector.data_copy();

        auto dir_entries = sector.size() / sizeof(TRDOS_DIR);
        auto pd = reinterpret_cast<const TRDOS_DIR*>(data.data());

        for (size_t entry = 0; entry < dir_entries; ++entry, ++pd)
        {
            bool unused = pd->abName[0] == 0x00;
            bool hidden = pd->abName[0] == 0x01;

            if (unused)
                continue;

            auto uStart = (pd->abStart[1] << 8) | pd->abStart[0];
            auto uLength = (pd->abLen[1] << 8) | pd->abLen[0];

            if (!hidden)
            {
                // Show regular directory entry
                util::cout << util::fmt(" %-8.8s <%c> %3u  %05u %05u", pd->abName, pd->bExt, pd->bSectors, uStart, uLength);
            }
            else
            {
                // Show deleted entry in red, with first character of filename replaced by "?"
                util::cout << colour::red << util::fmt(" ?%-7.7s <%c> %3u  %05u %05u", pd->abName + 1, pd->bExt, pd->bSectors, uStart, uLength) << colour::none;
            }

            // If it's a BASIC program, check for auto-start line number
            // ToDo: skip this for real disks due to seek cost?
            if (pd->bExt == 'B')
            {
                // Locate the final sector for the file
                uint8_t block = pd->bStartSector + static_cast<uint8_t>((uLength + 255) / 256) - 1;
                auto offset = (uLength & 0xff) + 2;

                uint8_t cyl = pd->bStartTrack + (block >> 4);
                uint8_t sec = 1 + (block & 0xf);
                uint8_t head = (cyl >= TRD_NORM_TRACKS) ? 1 : 0;
                cyl %= TRD_NORM_TRACKS;

                auto& sectorE = disk.get_sector(Header(cyl, head, sec, 1));
                auto& dataE = sectorE.data_copy();

                if (offset < sectorE.size() - 1)
                {
                    auto line = (dataE[offset + 1] << 8) | dataE[offset];
                    if (line)
                    {
                        if (hidden)
                            util::cout << "  " << colour::red << line << colour::none << " : ";
                        else
                            util::cout << "  " << line;
                    }
                }
            }

            util::cout << '\n';
        }
    }

    util::cout << util::fmt("\n %u File(s)\t %u Track %c. Side\n", data9[228], (data9[227] & 1) ? 40 : 80, (data9[227] & 2) ? 'D' : 'S');
    util::cout << util::fmt(" %u Del. File(s)\t Free Sector %u\n", data9[244], (data9[230] << 8) | data9[229]);

    return true;
}


bool IsOpusDirEntry(const OPUS_DIR* pd_, int entry)
{
    auto first_block = (pd_->first_block[1] << 8) | pd_->first_block[0];
    auto last_block = (pd_->last_block[1] << 8) | pd_->last_block[0];
    auto last_block_size = (pd_->last_block_size[1] << 8) | pd_->last_block_size[0];

    // Catalogue must start at block 0, with full final block (size-1)
    if (entry == 0 && (first_block != 0 || last_block_size != 0xff))
        return false;

    // Check block ranges are sensible
    if (first_block > last_block || last_block >= Format(RegularFormat::OPD).total_sectors() || (!last_block && !last_block_size))
        return false;

    // Name must not contain nulls
    if (std::memchr(pd_->szName, '\0', sizeof(pd_->szName)) != nullptr)
        return false;

    return true;
}

bool IsOpusDirSector(const Sector& sector)
{
    auto& data = sector.data_copy();

    // Check size code and data size
    if (sector.header.size != 1 || sector.has_shortdata())
        return false;

    auto pd = reinterpret_cast<const OPUS_DIR*>(data.data());
    auto dir_entries = static_cast<int>(data.size() / sizeof(OPUS_DIR));

    for (auto entry = 0; entry < dir_entries; ++entry, ++pd)
    {
        auto last_block = (pd->last_block[1] << 8) | pd->last_block[0];

        // Stop at the file end marker
        if (entry != 0 && last_block == 0xffff)
            break;

        if (!IsOpusDirEntry(pd, entry))
            return false;
    }

    return true;
}

bool DirOpus(Disk& disk)
{
    Format fmt{ RegularFormat::OPD };

    auto cyls = 1, heads = 1, sectors = 1;
    auto uFiles = 0, uBlocks = 0;
    auto cat_blocks = 1;

    for (auto i = 0; i <= cat_blocks; ++i)
    {
        auto& sector = disk.get_sector(Header(0, 0, i, 1));
        auto& data = sector.data_copy();

        // First sector has disk geometry
        if (i == 0)
        {
            auto& ob = *reinterpret_cast<const OPD_BOOT*>(data.data());
            cyls = ob.cyls;
            heads = (ob.flags & 0x10) ? 2 : 1;
            sectors = ob.sectors;

            Format::Validate(cyls, heads, sectors);
            continue;
        }

        auto pd = reinterpret_cast<const OPUS_DIR*>(data.data());
        auto dir_entries = fmt.sector_size() / static_cast<int>(sizeof(OPUS_DIR));

        for (auto j = 0; j < dir_entries; ++j, ++pd)
        {
            // Extract the catalogue entry contents
            auto uFirstBlock = ((pd->first_block[1] & 0x0f) << 8) | pd->first_block[0];
            auto uLastBlock = (pd->last_block[1] << 8) | pd->last_block[0];
            //          auto uLastBlockSize = (pd->last_block_size[1] << 8) | pd->last_block_size[0];   // unused here

                        // Directory terminator?
            if (uLastBlock == 0xffff)
            {
                // Don't include catalogue and end marker in file count
                util::cout << util::fmt("\n%u files, %u free slots\n%u blocks used, %u free (%uK)\n",
                    uFiles - 1, cat_blocks * dir_entries - uFiles - 1, uBlocks + 1,
                    uFirstBlock - uBlocks, (uFirstBlock - uBlocks) / (1024 / fmt.sector_size()));
                return true;
            }

            ++uFiles;
            uBlocks += uLastBlock - uFirstBlock + 1;

            // The first entry in the catalogue is the label
            if (i == 1 && !j)
            {
                auto name = util::trim(std::string(pd->szName, sizeof(pd->szName)));
                util::cout << util::fmt("%.10s:\n\n", name.c_str());

                // Update the real number of blocks in the catalogue
                // The first block isn't directory, so subtract an extra 1
                cat_blocks = uLastBlock - uFirstBlock + 1 - 1;
                continue;
            }

            // Output the filename (we don't expand keywords yet)
            util::cout << util::fmt("%-10.10s  ", pd->szName);

            // ToDo: suppress extra info on physical disks, due to seek overhead?
//          if (true)
            {
                // Locate the first sector of the file, which starts with a header
                auto cyl = (++uFirstBlock / sectors) % cyls;
                auto head = (uFirstBlock / sectors) / cyls;
                auto sec = (uFirstBlock % sectors) + fmt.base;

                auto& sectorS = disk.get_sector(Header(cyl, head, sec, 1));
                auto& dataS = sectorS.data_copy();

                // Extract the file type, start and length
                auto type = dataS[0];
                auto length = (dataS[2] << 8) | dataS[1];
                auto start = (dataS[4] << 8) | dataS[3];
                //              auto uUnk = (dataS[6] << 8) | dataS[5];

                                // Display type-dependent information
                switch (type)
                {
                case 0: util::cout << "BASIC  "; if (start < 10000) util::cout << start; break;
                case 1: util::cout << util::fmt("DATA   %c()", 'a' + ((start & 0x3f00) >> 8) - 1); break;
                case 2: util::cout << util::fmt("DATA   %c$()", 'a' + ((start & 0x3f00) >> 8) - 1); break;
                case 3: util::cout << util::fmt("CODE   %5u,%u", start, length); break;
                default: util::cout << "???"; break;
                }
            }

            util::cout << '\n';
        }
    }

    return true;
}


/*
128-191 System variables with format information (this section is a BIG garbage, only few people can understand it)
192-201 Name of disk (10 chars)
202,203 randomly generated 16-bit number for recognize two disks with same name
204-207 MDOS identification bytes, here staying text "SDOS", so MDOS can identify his format on disk
177 bit3= log1 if 40 tracks drive is connected, bit4=always log1 - double sided disk
178 number of tracks on one side
179 number of sectors on one track
180 always 0
181-183 copy of bytes 177-179
*/

bool IsDidaktikDirSector(const Sector& sector)
{
    // Check sector number and size
    if (sector.header.sector != 1 || sector.header.size != 2 || sector.data_size() < 512)
        return false;

    auto& data = sector.data_copy();

    // Ensure MDOS signature is present
    if (memcmp(&data[204], "SDOS", 4))
        return false;

    // Check for duplicate geometry block
    if (memcmp(&data[177], &data[181], 3))
        return false;

    // Disk should be flagged as double-sided
    if (!(data[177] & 0x10))
        return false;

    // Check for unused byte, which should be zero
    if (data[188])
        return false;

    return true;
}


bool DirDidaktik(Disk& disk)
{
    auto& boot_sector = disk.get_sector(Header(0, 0, 1, 2));
    auto& boot_data = boot_sector.data_copy();

    auto heads = (boot_data[177] & 0x10) ? 2 : 1;
    int cyls = boot_data[178];
    int sectors = boot_data[179];
    //  auto track_size = sectors * SECTOR_SIZE;        unused??
    //  auto total_size = track_size * heads * cyls;    unused??

    util::cout << util::fmt(" Directory of %-*.*s\n\n", 10, 10, boot_data.data() + 192);
    auto num_files = 0, file_blocks = 0;
    bool done = false;

    for (auto i = 6; !done && i <= 13; ++i)
    {
        auto cyl = i / sectors;
        auto sec = 1 + (i % sectors);

        auto& sector = disk.get_sector(Header(cyl, 0, sec, 2));
        auto& data = sector.data_copy();

        for (int j = 0; !done && j < 16; ++j)
        {
            auto pb = data.data() + (32 * j);

            switch (*pb)
            {
                // Valid file types
            case 'P': case 'B': case 'N': case 'C': case 'S': case 'Q':
                break;

                // Ignore other values, but continue in case of deleted entries
            default:
                // Treat unused entry filler as an end marker?
                if (pb[0] == 0xe5 && pb[1] == 0xe5)
                    done = 1;
                continue;
            }

            auto file_size = (pb[21] << 16) | (pb[12] << 8) | pb[11];
            file_blocks += (file_size + 511) / 512;
            ++num_files;

            static const char szFlags[] = "HSPARWED";
            char szAttrs[9] = {};
            for (int k = 0; k < 8; ++k)
                szAttrs[k] = (pb[20] & (1 << k)) ? '-' : szFlags[k];

            util::cout << util::fmt(" %c %-*.*s    %6u %8s\n", pb[0], 10, 10, pb + 1, file_size, szAttrs);
        }
    }

    util::cout << util::fmt("\n %u File(s),   %6u Bytes free.\n", num_files, ((cyls * heads * sectors) - file_blocks - 14) * 512);

    return true;
}


struct CPC_DPB
{
    // extended DPB info, needed for format, in DPB_store
    uint8_t id;         // Identifier
    uint8_t sec1[2];    // 1. SECtor number (0, >1, >41h, >C1h)
    uint8_t secs[2];    // number of sectors per track (8, >9)
    uint8_t trks[2];    // number of tracks per side (>40, 80)
    uint8_t hds[2];     // number of heads per disk (>1, 2)
    uint8_t bps[2];     // Bytes Per Sector (128, 256, >512, 1024)

// original Disk Parameter Block (> marks CPC defaults)
    uint8_t spt[2];     // records Per Track (18, 20, 30, 32, 34, >36, 40)
    uint8_t bsh;        // Block SHift ...      2^BSH = BLM+1 = Rec/Block
    uint8_t blm;        // BLock Mask (>3/7, 4/15)
    uint8_t exm;        // EXtent Mask (0, 1)   Number of Extents/Entry - 1
    uint8_t dsm[2];     // max. blocknumber = number of blocks - 1
    uint8_t drm[2];     // DiRectory size - 1 (31, >63, 127)
    uint8_t al0;        // \ DRM in binary (80h/0, >C0h/0, F0h/0)
    uint8_t al1;        // / (1 bit=1 block, 11000000/00000000 = 2 blocks)
    uint8_t cks[2];     // ChecK recordS,nb of rec in dir (8, >16, 32)
    uint8_t ofs[2];     // OFfSet, reserved tracks (1, >2, 3, 4)
};


struct CPM_DPB
{
    uint8_t bFormat;        // format number: 0=SSSD, 3=DSDD, others bad (1+2 are CPC, but don't use DPB)
    uint8_t bSidedness;     // bits 0-1: 0=single-sided, 1=double-sided (flip), 2=double-sided (up-and-over)
                            // bit6=high-density, bit7=double-track
    uint8_t bTracks;        // tracks per side
    uint8_t bSectors;       // sectors per track
    uint8_t bSize;          // sector size code
    uint8_t bResTracks;     // number of reserved tracks
    uint8_t bBlockShift;    // block shift
    uint8_t bDirBlocks;     // number of directory blocks
    uint8_t bRWGap;         // read/write gap
    uint8_t bGap3;          // format gap3
//  uint8_t abUnused[5];    // unused: should be zero
//  uint8_t bCheckSum;      // Check sum byte: 1=PCW9512, 3=+3, 255=PCW8256
};


bool IsCpmDpb(const uint8_t* pb_)
{
    auto p = reinterpret_cast<const CPM_DPB*>(pb_);

    // Allow "blank" boot sector
    if (!memcmp(pb_, "\xe5\xe5\xe5\xe5\xe5\xe5\xe5\xe5\xe5\xe5", 10))
        return true;

    // Only SSSD and DSDD disks only
    if (p->bFormat > 3)
        return false;

    // Bits 2 to 5 in sidedness are unused
    if (p->bSidedness & 0x3c)
        return false;

    // Check geometry is sensible
    if (p->bTracks < 2 || p->bTracks >= MAX_TRACKS || p->bSectors < 1 || p->bSectors > MAX_SECTORS || p->bSize >= 8)
        return false;

    // Sector and block shifts should be legal
    if (p->bSize >= 8 || p->bBlockShift >= 8 || p->bSize > p->bBlockShift)
        return false;

    // Must be at least 1 reserved track, but not too many
    if (!p->bResTracks || p->bResTracks >= MAX_TRACKS)
        return false;
    /*
        // Gap sizes should be non-zero?
        if (!p->bRWGap || !p->bGap3)
            return false;

        // Unused bytes should be zero?
        if (memcmp(p->abUnused, "\0\0\0\0\0", sizeof(p->abUnused)))
            return false;
    */
    return true;
}

struct CPM_DIR
{
    uint8_t user;       // 0x00 to 0x0F = user number of file
                        // 0x10 to 0x1F = password (CP/M+ only)
                        // 0x20 = disc name (CP/M+ only)
                        // 0x21 = datestamps (CP/M+ only)
                        // 0xE5 = deleted file or unused directory entry
    uint8_t file[8];    // Up to eight ASCII characters of file name, the rest filled with spaces, upper case.
    uint8_t ext[3];     // Up to three characters, the rest filled with spaces, upper case.
    uint8_t ex;         // Extent number
    uint8_t s1;         // Last record byte count (CP/M+ only)
    uint8_t s2;         // High byte of extent number, if required.
    uint8_t rc;         // Record count
    uint8_t blocks[16];
};

bool IsCpmDirEntry(const uint8_t* pb_)
{
    auto p = reinterpret_cast<const CPM_DIR*>(pb_);

    // Accept unused entries without looking further
    if (p->user == 0xe5)
        return true;

    // Reject unknown user values
    if (p->user > 0x21)
        return false;

    // Must be at least one valid block in size
    if (!p->blocks[0] || !p->rc)
        return false;

    // CPC special case - allow entries with a 2nd character of 6, used for some fancy effects
//  if (p->file[1] != 0x06)
    {
        // Ensure the file+ext are normal ASCII characters (ignoring bit 7)     illegal: !&()+,-./:;<=>[\]|
        for (size_t i = 0; i < sizeof(p->file) + sizeof(p->ext); ++i)
            if ((p->file[i] & 0x7f) < 0x20)
                return false;
    }

    // Ensure b7 of [7] is clear. b7 of [5] means hidden, b7 of [6] means read-only.
    // See http://cpctech.cpc-live.com/docs/catalog.html for details.
    if (p->file[7] & 0x80)
        return false;

    // Seems valid
    return true;
}

bool IsCpmDirSector(const Sector& sector)
{
    if (sector.data_size() < SECTOR_SIZE)
        return false;

    auto pb = sector.data_copy().data();

    if (sector.header.sector == 1 && IsCpmDpb(pb))
        return true;
    else if ((sector.header.sector & 0x40) == 0x40)
        return true;    // uReserved = 2, nDirBlocks = 4;
    else if ((sector.header.sector & 0xc0) != 0xc0)
        return false;

    // Check all the directory entries in the sector
    for (auto u = 0; u < SECTOR_SIZE; u += sizeof(CPM_DIR))
    {
        if (!IsCpmDirEntry(pb + u))
            return false;
    }

    return true;
}


static const CPM_DPB asDPB[] =
{
    { 0x00,0x00,0x28,0x09,0x02,0x01,0x03,0x02,0x2a,0x52 },  // +3
    { 0x01,0x00,0x28,0x09,0x02,0x02,0x03,0x02,0x2a,0x52 },  // CPC System
    { 0x02,0x00,0x28,0x09,0x02,0x00,0x03,0x02,0x2a,0x52 },  // CPC Data
    { 0x03,0x81,0x50,0x09,0x02,0x01,0x04,0x04,0x2a,0x52 }   // PCW and Pro-Dos
};


bool DirCpm(Disk& disk, const Sector& s)
{
    const CPM_DPB* pdpb = nullptr;

    if (disk.cyls() >= NORMAL_TRACKS && disk.heads() == NORMAL_SIDES)   // PCW and Pro-Dos
        pdpb = &asDPB[3];
    else if ((s.header.sector & 0xc0) == 0x40)  // CPC System
        pdpb = &asDPB[1];
    else if ((s.header.sector & 0xc0) == 0xc0)  // CPC Data
        pdpb = &asDPB[2];
    else if ((s.data_copy()[0] & 0x3f) < arraysize(asDPB))  // On-disk DPB?
        pdpb = reinterpret_cast<const CPM_DPB*>(s.data_copy().data());
    else
        pdpb = &asDPB[0];   // +3

    int nSectorsPerBlock = 1 << (pdpb->bBlockShift - pdpb->bSize);
    int nDirSectors = static_cast<uint8_t>(pdpb->bDirBlocks * nSectorsPerBlock);
    uint8_t bSectorBase = (s.header.sector & 0xc0) + 1;
    int total_blocks = 0;
    int num_files = 0;

    struct CPM_DIR_DATA
    {
        int file_blocks{ 0 };
        int file_attrs{ 0 };
    };
    std::map<std::string, CPM_DIR_DATA> dir_entries;

    for (int pass = 1; pass <= 2; ++pass)
    {
        for (uint8_t i = 0; i < nDirSectors; ++i)
        {
            bool side_flip = (pdpb->bSidedness & 1) == 1;
            uint8_t track = pdpb->bResTracks + (i / pdpb->bSectors);

            uint8_t cyl = track >> (side_flip ? 1 : 0);
            uint8_t head = track & (side_flip ? 1 : 0);
            uint8_t sec = bSectorBase + (i % pdpb->bSectors);

            const Sector* sector = nullptr;
            if (!disk.find(Header(cyl, head, sec, 2), sector) || sector->has_shortdata())
                throw util::exception(CHR(cyl, head, sec), " not found");

            // Process the sector data, ignoring gap data.
            auto data = sector->data_copy();
            if (data.size() > sector->size())
                data.resize(sector->size());

            for (auto j = 0; j < data.size() / static_cast<int>(sizeof(CPM_DIR)); ++j)
            {
                auto p = &reinterpret_cast<const CPM_DIR*>(data.data())[j];

                // Skip unused/deleted entries
                if (p->user == 0xe5)
                    continue;

                int file_blocks = (p->rc + ((1 << pdpb->bBlockShift) - 1)) >> pdpb->bBlockShift;

                auto name = std::string(p->file, p->file + sizeof(p->file));
                name += '.' + std::string(p->ext, p->ext + sizeof(p->ext));

                // Strip bit 7 from each character, and convert to upper-case.
                auto name_masked = name;
                std::transform(name.begin(), name.end(), name_masked.begin(), [](char ch) {
                    ch &= 0x7f;
                    if (ch >= 'a' && ch <= 'z')
                        ch ^= 0x20;
                    return ch;
                    });

                // Reject filenames containing non-printable or +3DOS illegal characters.
                if (!std::all_of(name_masked.begin(), name_masked.end(), [](char ch) {
                    static const std::string illegals = R"("!&()+,-/:;<=>[\]|")";
                    return std::isprint(ch) && illegals.find(ch) == std::string::npos;
                    })) {
                    continue;
                }

                if (pass == 1)
                {
                    dir_entries[name_masked].file_blocks += file_blocks;
                    total_blocks += file_blocks;
                }
                else if (p->ex)
                    continue;
                else
                {
                    int attrs = (p->ext[0] & 0x80) | ((p->ext[1] & 0x80) >> 1);
                    dir_entries[name_masked].file_attrs = attrs;

                    ++num_files;
                }
            }
        }
    }

    for (auto it = dir_entries.begin(); it != dir_entries.end(); ++it)
    {
        bool readonly = (it->second.file_attrs & 0x80) != 0;
        bool hidden = (it->second.file_attrs & 0x40) != 0;

        std::stringstream ss;
        if (hidden) ss << "Hidden";
        if (readonly && hidden) ss << ", ";
        if (readonly) ss << "Read-Only";

        if (hidden) util::cout << colour::cyan;
        util::cout << " " << it->first <<
            util::fmt(" %3uK", it->second.file_blocks * Sector::SizeCodeToLength(pdpb->bBlockShift) / 1024);
        if (hidden)
        {
            if (!util::is_stdout_a_tty())
                util::cout << " (" << ss.str() << ")";
            else
                util::log << " (" << ss.str() << ")";
        }

        util::cout << "\n";
        if (hidden) util::cout << colour::none;
    }

    auto n = ((pdpb->bTracks * (1 + !!(pdpb->bSidedness & 3))) - pdpb->bResTracks); // tracks*sides - reserved
    n = (n * pdpb->bSectors) / nSectorsPerBlock;                                        // convert to blocks
    n -= (pdpb->bDirBlocks + total_blocks);                                             // subtract directory and used blocks
    n = n * Sector::SizeCodeToLength(pdpb->bBlockShift) / 1024;                         // convert to K
    if (n < 0) n = 0;

    util::cout << util::fmt("\n %d File%s, %dK free\n", num_files, (num_files == 1) ? "" : "s", n);
    return true;
}


bool DirAce(Disk& disk)
{
    const Sector* ps = nullptr;

    // Try the primary catalogue location on cyl 0
    if (!disk.find(Header(0, 0, 0, SizeToCode(4096)), ps))
    {
        // Try the backup location on cyl 1
        if (!disk.find(Header(1, 0, 0, SizeToCode(4096)), ps))
            return false;
    }

    auto data_offset = 0;
    if (!IsDeepThoughtSector(*ps, data_offset))
        return false;

    if (ps->header.cyl == 1)
        Message(msgWarning, "primary catalogue is bad, using backup");

    const Data& data = ps->data_copy();
    const uint8_t* pb = data.data() + data_offset;

    // The next byte is the track count, followed by the track block size in bytes
    int tracks = pb[0];
    auto block = (pb[2] << 8) | pb[1];
    pb += 3;

    // One entry per track follows, which appears to hold a file number, or zero for unused
    auto free = 0;
    for (uint8_t track = 2; track < tracks; ++track, ++pb)
    {
        // If the entry is zero, add a track worth of bytes to the free bytes counter
        if (!*pb)
            free += block;
    }

    util::cout << " Drive : 0\n";
    util::cout << util::fmt(" Format: %u tracks of %u bytes\n\n", tracks, block);
    util::cout << " File name        Length   Type\n\n";

    // The filenames are stored back-to-back, each starting with a length count byte
    while (*pb)
    {
        int len = *pb++;
        util::cout << util::fmt(" %-18.*s", len, pb);
        pb += len;

        auto size = (pb[1] << 8) | pb[0];
        util::cout << util::fmt("%5u  ", size);

        auto type = (pb[3] << 8) | pb[2];
        if (type == 0)
            util::cout << " dict\n";
        else
            util::cout << util::fmt("%5u\n", type);

        pb += 4;
    }

    util::cout << util::fmt("\n %u bytes free\n", free);
    return true;
}


bool DirMgt(Disk& disk)
{
    MGT_DISK_INFO di{};
    di.dir_tracks = MGT_DIR_TRACKS;

    auto used_sectors = 0;
    auto num_files = 0;
    bool fDone = false;

    for (auto cyl = 0; !fDone && cyl < di.dir_tracks; ++cyl)
    {
        for (auto sec = 1; !fDone && sec <= MGT_SECTORS; ++sec)
        {
            auto& sector = disk.get_sector(Header(cyl, 0, sec, 2));
            auto& data = sector.data_copy();

            // If this is the first sector, show the catalogue header
            if (cyl == 0 && sec == 1)
            {
                GetDiskInfo(data.data(), di);

                if (di.dos_type == SamDosType::SAMDOS)
                    util::cout << " * DRIVE 1 - DIRECTORY *\n\n";
                else if (di.dos_type == SamDosType::MasterDOS)
                {
                    if (!di.disk_label.empty())
                        util::cout << " " << di.disk_label << ":\n\n";
                    else
                        util::cout << " MasterDOS:\n\n";
                }
                else
                {
                    //ss << " Record " << disk.uRecord << ": " << di.szLabel;   //ToDo
                    util::cout << " Record: " << di.disk_label << "\n\n";
                }

            }

            // Skip the reserved boot sector in MasterDOS disks
            if (cyl == 4 && sec == 1)
                continue;

            for (int entry = 0; !fDone && entry < 2; ++entry)
            {
                auto pdi = reinterpret_cast<const MGT_DIR*>(data.data()) + entry;

                if (pdi->bType & 0x3f)
                {
                    ++num_files;

#if 0
                    // Trust the sector count in the directory entry
                    uSectorTotal += (pdi->bSectorsHigh << 8) | pdi->bSectorsLow;
#else
                    // Count the bits set in the sector map
                    for (size_t i = 0; i < sizeof(pdi->abSectorMap); ++i)
                    {
                        uint8_t b = pdi->abSectorMap[i];
                        for (int j = 0; j < 8; ++j)
                            if (b & (1 << j))
                                ++used_sectors;
                    }
#endif

                    // Show the entry, passing in whether it's a hidden file
                    bool fHidden = (pdi->bType & 0x80) != 0;
                    DirMgtEntry((20 * cyl) + (2 * (sec - 1)) + entry, pdi, fHidden);
                }
                else
                    fDone = !pdi->abName[0];
            }
        }
    }

    auto reserved_sectors = (di.dir_tracks > 4) ? 1 : 0;
    auto max_slots = (di.dir_tracks * MGT_SECTORS  - reserved_sectors) * 2;
    auto free_slots = max_slots - num_files;

#if 0 // ToDo: replace legacy variable
    auto total_sectors = disk.uRecordSize ? disk.uRecordSize : (MGT_DISK_SIZE / SECTOR_SIZE);
#else
    auto total_sectors = MGT_DISK_SIZE / SECTOR_SIZE;
#endif
    auto dir_sectors = di.dir_tracks * MGT_SECTORS;
    auto free_sectors = total_sectors - dir_sectors - used_sectors;

    /*
        // Cap the min and max values
        if (free_sectors > total_sectors) free_sectors = 0;
        if (uUsedSectors > total_sectors - dir_sectors) used_sectors = total_sectors - dir_sectors;
    */

    if (num_files) util::cout << '\n';
    util::cout << util::fmt(" %u file%s, %u free slot%s,  %u%sK used, %u%sK free\n",
        num_files, (num_files == 1) ? "" : "s", free_slots, (free_slots == 1) ? "" : "s",
        used_sectors / 2, (used_sectors & 1) ? ".5" : "", free_sectors / 2, (free_sectors & 1) ? ".5" : "");

    return true;
}


bool Dir(Disk& disk)
{
    const Sector* sector = nullptr;

    if (disk.find(Header(0, 0, 0, 5), sector))
    {
        if (IsDeepThoughtDisk(disk, sector))
            return DirAce(disk);
    }

    if (disk.find(Header(0, 0, 1, 2), sector))
    {
        if (IsDidaktikDirSector(*sector))
            return DirDidaktik(disk);
        else if (IsCpmDirSector(*sector))
            return DirCpm(disk, *sector);
        else if (IsMgtDirSector(*sector))
            return DirMgt(disk);
    }
    else if (disk.find(Header(0, 0, 1, 1), sector))
    {
        if (IsOpusDirSector(*sector))
            return DirOpus(disk);
        else if (IsTrDosDirSector(*sector))
            return DirTrDos(disk);
    }
    else if (disk.find(Header(0, 0, 0x41, 2), sector) ||
        disk.find(Header(0, 0, 0xc1, 2), sector))
    {
        if (IsCpmDirSector(*sector))
            return DirCpm(disk, *sector);
    }

    throw util::exception("unrecognised directory format");
}

bool DirImage(const std::string& path)
{
    auto disk = std::make_shared<Disk>();
    return ReadImage(path, disk) && Dir(*disk);
}
