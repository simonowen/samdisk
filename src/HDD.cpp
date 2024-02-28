// Base class for HDD devices and image files

#include "SAMdisk.h"

#include "BlockDevice.h"
#include "HDFHDD.h"

#define SECTOR_BLOCK    2048    // access CF/HDD devices in 1MB chunks


/*static*/ bool HDD::IsRecognised(const std::string& path)
{
    return  BlockDevice::IsRecognised(path) ||
        HDFHDD::IsRecognised(path);
}


/*static*/ std::shared_ptr<HDD> HDD::OpenDisk(const std::string& path)
{
    std::shared_ptr<HDD> hdd;

    std::string open_path = path;

    if (BlockDevice::IsBlockDevice(path))
    {
#ifdef _WIN32
        auto ulDevice = std::strtoul(path.c_str(), nullptr, 0);
        open_path = util::fmt(R"(\\.\PhysicalDrive%lu)", ulDevice);
#endif
        hdd.reset(new BlockDevice());
    }
    else if (BlockDevice::IsFileHDD(path))
        hdd.reset(new BlockDevice());
    else if (HDFHDD::IsRecognised(path))
        hdd.reset(new HDFHDD());

    if (hdd && !hdd->Open(open_path))
        hdd.reset();

    return hdd;
}


void HDD::Reset()
{
    h = -1;

    cyls = heads = sectors = 0;
    total_sectors = total_bytes = 0;
    sector_size = data_offset = 0;

    sIdentify = {};

    strMakeModel = strSerialNumber = strFirmwareRevision = "";
}

int64_t HDD::Tell() const
{
#ifdef _WIN32
    int64_t llOffset = _telli64(h) - data_offset;
#else
    off_t llOffset = lseek(h, 0L, SEEK_CUR) - data_offset;
#endif
    return llOffset / sector_size;
}

/*static*/ std::shared_ptr<HDD> HDD::CreateDisk(const std::string& path, int64_t llTotalBytes_, const IDENTIFYDEVICE* pIdentify_, bool fOverwrite_)
{
    std::shared_ptr<HDD> hdd;

    if (IsHddImage(path))
        hdd = std::shared_ptr<HDD>(new HDFHDD());

    if (hdd && !hdd->Create(path, llTotalBytes_, pIdentify_, fOverwrite_))
        hdd.reset();

    return hdd;
}


bool HDD::Seek(int64_t sector) const
{
    auto llOffset = sector * sector_size + data_offset;

#ifdef _WIN32
    return _lseeki64(h, llOffset, SEEK_SET) == llOffset;
#elif defined(__linux__)
    return lseek(h, llOffset, SEEK_SET) == llOffset;
#else
    return lseek(h, llOffset, SEEK_SET) == llOffset;
#endif
}

int HDD::Read(void* pv, int sectors_, bool byte_swap) const
{
    unsigned want = sectors_ * sector_size;
    auto n = read(h, pv, want);
    if (n < 0) n = 0;
    if (byte_swap) ByteSwap(pv, n);
    return n / sector_size;
}

int HDD::Write(void* pv, int sectors_, bool byte_swap)
{
    unsigned have = sectors_ * sector_size;
    if (byte_swap) ByteSwap(pv, have);
    auto n = write(h, pv, have);
    if (n < 0) n = 0;
    if (byte_swap) ByteSwap(pv, have);
    return n / sector_size;
}


void HDD::SetIdentifyData(const IDENTIFYDEVICE* pIdentify_)
{
    // Use any supplied identify data
    if (pIdentify_)
    {
        // Copy the supplied data
        memcpy(&sIdentify, pIdentify_, sizeof(sIdentify));

        // Update CHS from the identify data
        cyls = sIdentify.word[1];
        heads = sIdentify.word[3];
        sectors = sIdentify.word[6];

        // Invalidate the identify length if we're told to ignore it
        if (opt.noidentify)
            sIdentify.len = 0;
    }
    else
    {
        // Generate CHS values from the total sector count
        CalculateGeometry(total_sectors, cyls, heads, sectors);

        // Clear any existing data and set the full data size
        sIdentify = {};
        sIdentify.len = sizeof(sIdentify.byte);

        sIdentify.word[0] = (1 << 6);     // fixed device

        // CHS values
        sIdentify.word[1] = static_cast<uint16_t>(cyls);
        sIdentify.word[3] = static_cast<uint16_t>(heads);
        sIdentify.word[6] = static_cast<uint16_t>(sectors);

        // Form 8-character date string from SAMdisk build date, to use as firmware revision
        std::string strDate = util::fmt("%04u%02u%02u", YEAR, MONTH + 1, DAY);

        // Serial number, firmware revision and model number
        SetIdentifyString("", &sIdentify.word[10], 20);
        SetIdentifyString(strDate, &sIdentify.word[23], 8);
        SetIdentifyString("SAMdisk Device", &sIdentify.word[27], 40);

        sIdentify.word[47] = 1;                     // read/write multiple supports 1 sector blocks
        sIdentify.word[49] = (1 << 9);              // LBA supported

        // Current override CHS values
        sIdentify.word[53] = (1 << 0);              // words 54-58 are valid
        sIdentify.word[54] = sIdentify.word[1];     // current cyls
        sIdentify.word[55] = sIdentify.word[3];     // current heads
        sIdentify.word[56] = sIdentify.word[6];     // current sectors

        // Max CHS sector count is just C*H*S with maximum values for each
        auto uMaxSectorsCHS = 16383 * 16 * 63;
        auto uTotalSectorsCHS = (total_sectors > uMaxSectorsCHS) ? uMaxSectorsCHS : total_sectors;
        sIdentify.word[57] = static_cast<uint16_t>(uTotalSectorsCHS & 0xffff);
        sIdentify.word[58] = static_cast<uint16_t>((uTotalSectorsCHS >> 16) & 0xffff);

        // Max LBA28 sector count is 0x0fffffff
        auto uMaxSectorsLBA28 = (1 << 28) - 1;
        auto uTotalSectorsLBA28 = (total_sectors > uMaxSectorsLBA28) ? uMaxSectorsLBA28 : total_sectors;
        sIdentify.word[60] = uTotalSectorsLBA28 & 0xffff;
        sIdentify.word[61] = (uTotalSectorsLBA28 >> 16) & 0xffff;

        // Max LBA48 sector count is 0x0000ffffffffffff
        auto llTotalSectors = total_bytes / sector_size;
        auto llMaxSectorsLBA48 = (1LL << 48) - 1;
        auto llTotalSectorsLBA48 = (llTotalSectors > llMaxSectorsLBA48) ? llMaxSectorsLBA48 : llTotalSectors;
        sIdentify.word[100] = static_cast<uint16_t>(llTotalSectorsLBA48 & 0xffff);
        sIdentify.word[101] = static_cast<uint16_t>((llTotalSectorsLBA48 >> 16) & 0xffff);
        sIdentify.word[102] = static_cast<uint16_t>((llTotalSectorsLBA48 >> 32) & 0xffff);
        sIdentify.word[103] = 0;

        // If CFA ((CompactFlash Association) support isn't explicitly disabled, enable it
        if (!opt.nocfa)
        {
            sIdentify.word[0] = 0x848a;     // special value used to indicate CFA feature set support

            sIdentify.word[83] |= (1 << 2) | (1 << 14); // CFA feature set supported, feature bits are valid
            sIdentify.word[84] |= (1 << 14);            // indicate feature bits are valid
            sIdentify.word[86] |= (1 << 2);         // CFA feature set enabled
            sIdentify.word[87] |= (1 << 14);            // indicate features enabled are valid
        }
    }

    // Read the strings for serial number, firmware revision and make/model
    strSerialNumber = GetIdentifyString(&sIdentify.word[10], 20);
    strFirmwareRevision = GetIdentifyString(&sIdentify.word[23], 8);
    strMakeModel = GetIdentifyString(&sIdentify.word[27], 40);
}


bool HDD::SafetyCheck()
{
    if (!util::is_stdout_a_tty())
        return false;

    util::cout << "Are you sure? (y/N) ";

    auto ch = getchar();
    return (ch == 'Y' || ch == 'y');
}


bool HDD::Copy(HDD* phSrc_, int64_t uSectors_, int64_t uSrcOffset_/*=0*/, int64_t uDstOffset_/*=0*/, int64_t uTotal_/*=0*/, const char* pcszAction_)
{
    MEMORY mem(SECTOR_BLOCK * sector_size);

    if (!uTotal_) uTotal_ = uSectors_;

    for (int64_t uPos = 0;;)
    {
        // Sync source and destinations
        if (phSrc_) phSrc_->Seek(uSrcOffset_ + uPos);
        Seek(uDstOffset_ + uPos);

        // Determine how much to transfer in one go
        if (uPos > uSectors_) uPos = uSectors_;
        auto uBlock = std::min(static_cast<int>(uSectors_ - uPos), SECTOR_BLOCK);

        Message(msgStatus, "%s... %d%%", pcszAction_ ? pcszAction_ : "Copying",
            static_cast<int>((static_cast<uint64_t>(uDstOffset_ + uPos) * 100 / uTotal_)));

        if (!uBlock)
            break;

        // Read from source disk
        auto uRead = phSrc_ ? phSrc_->Read(mem, uBlock) : uBlock;
        if (uRead != uBlock)
        {
            Message(msgStatus, "Read error at sector %lu: %s", uSrcOffset_ + uPos + uRead, LastError());

            // Clear the bad block, but include it in the read data
            memset(mem + (uRead * sector_size), 0, sector_size);
            ++uRead;
        }

        // Forced byte-swapping?
        if (opt.byteswap)
            ByteSwap(mem, uRead * sector_size);

        // Write to target disk
        auto uWritten = Write(mem, uRead);
        if (uWritten != uRead)
        {
            Message(msgStatus, "Write error at sector %lu: %s", uDstOffset_ + uPos + uWritten, LastError());

            // Skip past the write error
            ++uWritten;
        }

        // Advance by the amount written
        uPos += uWritten;
    }

    return true;
}


std::string HDD::GetIdentifyString(void* p, size_t n)
{
    // Buffer length must be even
    n &= ~1;

    auto pcsz = reinterpret_cast<const char*>(p);
    std::string s(pcsz, n);

    // Copy the string, up to the size of the buffer
    for (size_t i = 0; i < n; i += 2)
        std::swap(s[i], s[i + 1]);

    // Trim leading and trailing spaces
    s = util::trim(s);

    return s;
}

void HDD::SetIdentifyString(const std::string& str, void* p, size_t n)
{
    // Buffer length must be even
    n &= ~1;

    // Unused entries are space-filled
    auto pb = reinterpret_cast<char*>(p);
    memset(pb, ' ', n);

    // Copy the string, up to the size of the buffer
    for (size_t i = 0; i < n && str[i]; ++i)
    {
        // Swap the order of each character pair
        pb[i ^ 1] = str[i];
    }
}
