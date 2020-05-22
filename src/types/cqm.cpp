// Sydex CopyQM disk images:
//
// Implemented using reverse-engineered notes from:
//  http://rio.early8bitz.de/cqm/cqm-format.pdf

#include "SAMdisk.h"

#define CQM_SIGNATURE   "CQ\x14"

struct CQM_HEADER
{
    char sig[3];                    // CQ\x14 image signature   00-02
    uint8_t sector_size[2];         // bytes per sector         03-04
    uint8_t sec_per_cluster;        // sectors per cluster      05
    uint8_t res_sectors[2];         // reserved sectors         06-07
    uint8_t num_fats;               // number of fats           08
    uint8_t dir_entries[2];         // directory entries        09-0A
    uint8_t total_sectors[2];       // total disk sectors       0B-0C
    uint8_t media_byte;             // media type               0D
    uint8_t sec_per_fat[2];         // sectors per fat          0E-0F
    uint8_t sec_per_track[2];       // sectors per track        10-11
    uint8_t heads[2];               // disk head count          12-13
    uint8_t hidden_sectors[4];      // hidden sectors           14-17
    uint8_t total_sectors32[4];     // 32-bit total sectors     18-1B
    char    description[60];        // disk description         1C-57
    uint8_t read_mode;              // 0=DOS, 1=blind, 2=HFS    58
    uint8_t density;                // 0=DD, 1=HD, 2=ED         59
    uint8_t used_cyls;              // tracks present in image  5A
    uint8_t cyls;                   // total disk cyls          5B
    uint8_t data_crc[4];            // crc32 of decoded data    5C-5F
    char    vol_label[11];          // DOS volume label         60-6A
    uint8_t dos_time[2];            // DOS-format time          6B-6C
    uint8_t dos_date[2];            // DOS-format date          6D-6E
    uint8_t comment_len[2];         // optional comment len     6F-70
    uint8_t sector_base;            // first sector number - 1  71
    uint8_t unknown[2];             // unused?                  72-73
    uint8_t interleave;             // 1 for no interleave      74
    uint8_t skew;                   // 0 for no skew            75
    uint8_t drv_type;               // source drive type (1-6)  76
    uint8_t unknown2[13];           // unused?                  77-83
    uint8_t header_crc;             // 8-bit sum                84
};
static_assert(sizeof(CQM_HEADER) == 133, "CQM_HEADER size is wrong");

static std::string FormatTime(int dos_date, int dos_time)
{
    struct tm tm {};
    tm.tm_year = ((dos_date >> 9) & 0x7f) + 80;
    tm.tm_mon = ((dos_date >> 5) & 0xf) - 1;
    tm.tm_mday = dos_date & 0x1f;
    tm.tm_hour = ((dos_time >> 11) & 0x1f);
    tm.tm_min = ((dos_time >> 5) & 0x3f);
    tm.tm_sec = (dos_time & 0x1f) * 2;
    tm.tm_isdst = -1;

    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %T");
    return ss.str();
}

static uint32_t crc32(const uint8_t* buf, int len, uint32_t crc = 0)
{
    static std::vector<uint32_t> crc_table(0x40);   // 6-bit
    static std::once_flag flag;

    std::call_once(flag, [] {
        for (uint32_t i = 0; i < static_cast<uint32_t>(crc_table.size()); ++i)
        {
            auto entry = i;
            for (int j = 0; j < 8; ++j)
                entry = (entry >> 1) ^ ((entry & 1) ? 0xEDB88320 : 0);
            crc_table[i] = entry;
        }
        });

    while (len-- > 0)
        crc = crc_table[(crc ^ *buf++) & 0x3f] ^ (crc >> 8);

    return crc;
}

bool ReadCQM(MemFile& file, std::shared_ptr<Disk>& disk)
{
    CQM_HEADER dh{};
    if (!file.rewind() || !file.read(&dh, sizeof(dh)))
        return false;

    if (memcmp(&dh.sig, CQM_SIGNATURE, sizeof(dh.sig)))
        return false;

    auto header_checksum = std::accumulate(
        file.data().begin(), file.data().begin() + sizeof(dh), 0);
    if (header_checksum & 0xff)
        throw util::exception("bad header checksum");

    Format fmt;
    fmt.fdc = FdcType::PC;
    fmt.encoding = Encoding::MFM;
    fmt.datarate = (dh.density == 2) ? DataRate::_1M :
        (dh.density == 1) ? DataRate::_500K : DataRate::_250K;
    fmt.interleave = (dh.interleave <= 1) ? 1 : dh.interleave;
    fmt.skew = dh.skew;
    fmt.base = (dh.sector_base + 1) & 0xff;
    fmt.fill = (dh.read_mode == 0) ? 0xe5 : 0x00;
    fmt.cyls = dh.cyls;
    fmt.heads = util::le_value(dh.heads);
    fmt.sectors = util::le_value(dh.sec_per_track);
    fmt.size = SizeToCode(util::le_value(dh.sector_size));
    fmt.Validate();

    int comment_len = util::le_value(dh.comment_len);
    if (comment_len)
    {
        std::vector<char> comment(comment_len);
        if (!file.read(comment))
            throw util::exception("short file reading comment");
        disk->metadata["comment"] = util::trim(std::string(comment.data(), comment.size()));
    }

    if (std::accumulate(std::begin(dh.unknown), std::end(dh.unknown), 0) ||
        std::accumulate(std::begin(dh.unknown2), std::end(dh.unknown2), 0))
    {
        Message(msgWarning, "unused header fields contain non-zero values");
    }

    Data data;
    data.reserve(fmt.disk_size());

    auto data_size = fmt.track_size() * dh.used_cyls * fmt.heads;
    while (data.size() < data_size)
    {
        uint16_t ulen;
        if (!file.read(&ulen, sizeof(ulen)))
            throw util::exception("short file reading disk data");

        auto len = static_cast<int16_t>(util::letoh(ulen));
        if (len <= 0)
        {
            uint8_t fill = 0;
            file.read(&fill, sizeof(fill));
            data.insert(data.end(), -len, fill);
        }
        else
        {
            Data block(len);
            file.read(block);
            data.insert(data.end(), block.begin(), block.end());
        }
    }

    auto data_crc = util::le_value(dh.data_crc);
    auto calc_data_crc = crc32(data.data(), data.size());
    if (calc_data_crc != data_crc)
        throw util::exception("invalid data crc");

    if (!file.eof())
        Message(msgWarning, "extra data found at end of file");

    auto desc = util::trim(std::string(dh.description, sizeof(dh.description)));
    if (!desc.empty())
        disk->metadata["description"] = desc;

    auto label = util::trim(std::string(dh.vol_label, sizeof(dh.vol_label)));
    if (!label.empty() && label != "** NONE **")
        disk->metadata["label"] = label;

    if (dh.cyls != dh.used_cyls)
        disk->metadata["used_cyls"] = std::to_string(dh.used_cyls);

    static const char* read_modes[] = { "DOS", "blind", "HFS" };
    if (dh.read_mode < 3)
        disk->metadata["read_mode"] = read_modes[dh.read_mode];

    static const char* drv_types[] = {
        "5.25\" 360KB", "5.25\" 1.2MB", "3.5\" 720KB", "3.5\" 1.44MB", "8\"", "3.5\" 2.88MB" };
    if (dh.drv_type >= 1 && dh.drv_type <= 6)
        disk->metadata["drive"] = drv_types[dh.drv_type - 1];

    auto dos_date = util::le_value(dh.dos_date);
    auto dos_time = util::le_value(dh.dos_time);
    disk->metadata["created"] = FormatTime(dos_date, dos_time);

    data.resize(fmt.disk_size(), fmt.fill);
    disk->format(fmt, data);
    disk->strType = "CQM";

    return true;
}
