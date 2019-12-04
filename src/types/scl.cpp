// Sinclair betadisk archive for Spectrum clones

#include "SAMdisk.h"
#include "trd.h"

#define SCL_SIGNATURE   "SINCLAIR"

typedef struct
{
    uint8_t szSig[8];   // SINCLAIR
    uint8_t bFiles;     // number of files in archive
} SCL_HEADER;

typedef struct
{
    uint8_t abName[8];      // filename
    uint8_t bType;          // file type
    uint8_t abStart[2];     // start address
    uint8_t abLength[2];    // length
    uint8_t bSectors;       // sector count
} SCL_FILE;


static size_t SumBlock(void* p, size_t len)
{
    size_t sum = 0;
    for (auto pb = reinterpret_cast<uint8_t*>(p); len--; sum += *pb++);
    return sum;
}


bool ReadSCL(MemFile& file, std::shared_ptr<Disk>& disk)
{
    if (!file.rewind() || file.size() < static_cast<int>(sizeof(SCL_HEADER) + sizeof(SCL_FILE) + 1))
        return false;

    SCL_HEADER sh;
    if (!file.read(&sh, sizeof(sh)))
        return false;

    // Check file signature and file count against TR-DOS maximum
    if (memcmp(sh.szSig, SCL_SIGNATURE, sizeof(sh.szSig)) || sh.bFiles > 128)
        return false;

    std::vector<uint8_t> mem(TRD_TRACK_SIZE);
    auto pb = mem.data();

    auto uDataLba = 16;     // data starts on track 1
    auto sum = SumBlock(&sh, sizeof(sh));

    for (int i = 0; i < sh.bFiles; ++i)
    {
        if (!file.read(pb, sizeof(SCL_FILE)))
            throw util::exception("short file reading file ", i, "  header");

        if (*pb == 0x01)
            Message(msgWarning, "file %d is marked as deleted", i);

        auto pscl = reinterpret_cast<SCL_FILE*>(pb);
        pb[14] = uDataLba & 0x0f;       // sector
        pb[15] = static_cast<uint8_t>(uDataLba >> 4);   // track
        uDataLba += pscl->bSectors;     // add on sector count

        // Sum the block we read
        sum += SumBlock(pscl, sizeof(*pscl));
        pb += 16;
    }

    // Ensure the file size matches what we're expecting
    auto calc_size = static_cast<int>(sizeof(SCL_HEADER) + sh.bFiles * sizeof(SCL_FILE) + (uDataLba - 16) * 256 + 4);
    if (!opt.force && file.size() != calc_size)
        throw util::exception("file size (", file.size(), " doesn't match content size (", calc_size, ")");

    // Calculate the image size, and check against the maximum
    auto lDiskSize = uDataLba * TRD_SECTOR_SIZE;
    if (lDiskSize > TRD_SIZE_128_2)
        Message(msgWarning, "SCL contents exceeds 1MB limit");

    Format fmt{ RegularFormat::TRDOS };
    fmt.cyls = static_cast<uint8_t>(SizeToCylsTRD(lDiskSize));
    fmt.Override();

    // Determine the cylinder count for the image, and the corresponding block count
    auto free_blocks = fmt.total_sectors() - uDataLba;

    pb = mem.data() + 8 * 256;
    pb[225] = uDataLba & 0x0f;                      // first free sector
    pb[226] = static_cast<uint8_t>(uDataLba >> 4);  // first free track
    pb[227] = 0x16;                                 // 80 tracks, 2 sides (or custom, including 1MB)
    pb[228] = sh.bFiles;                            // files on disk
    pb[229] = free_blocks & 0xff;                   // free sectors
    pb[230] = static_cast<uint8_t>(free_blocks >> 8);
    pb[231] = 0x10;                                 // TR-DOS id byte
    memset(pb + 234, ' ', 9);                       // spaces, or disk password?
    pb[244] = 0;                                    // deleted files

    // Copy source filename without extension, padded with spaces
    std::string filename = file.name();
    filename = filename.substr(0, filename.find_last_of('.'));
    filename = util::fmt("%-9.9s", filename.c_str());
    std::memcpy(pb + 245, filename.c_str(), 9);

    for (uint8_t cyl = 0; cyl < fmt.cyls; ++cyl)
    {
        for (uint8_t head = 0; head < fmt.heads; ++head)
        {
            CylHead cylhead(cyl, head);

            // The first track was constructed above, the rest are read from the SCL file
            if (cyl != 0 || head != 0)
            {
                auto uRead = file.read(mem.data(), 1, TRD_TRACK_SIZE);

                // SCL has a 32-bit checksum at the end of file.  If we read it as part of the
                // file data it should be removed from the byte count.
                if (file.eof() && uRead > static_cast<int>(sizeof(uint32_t)))
                    uRead -= sizeof(uint32_t);

                // Clear the unread part of the track data, then sum what we did read
                memset(mem.data() + uRead, 0, TRD_TRACK_SIZE - uRead);
                sum += SumBlock(mem.data(), uRead);
            }

            Track track;
            track.format(cylhead, fmt);
            track.populate(mem.begin(), mem.end());
            disk->write(cylhead, std::move(track));
        }
    }

    // Read the 4-byte file checksum
    uint8_t ab[4] = {};
    file.seek(file.size() - sizeof(uint32_t));
    file.read(ab, sizeof(ab), 1);

    uint32_t sum_check = ((ab[3] << 24) | (ab[2] << 16) | (ab[1] << 8) | ab[0]);
    if (sum != sum_check)
        Message(msgWarning, "file checksum does not match contents");

    disk->fmt = fmt;
    disk->strType = "SCL";

    return true;
}
