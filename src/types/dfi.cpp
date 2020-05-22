// DiscFerret:
//  http://www.discferret.com/wiki/DFI_image_format

#include "SAMdisk.h"
#include "DemandDisk.h"
#include "BitstreamDecoder.h"

struct DFI_FILE_HEADER
{
    char signature[4];      // "DFE2" for new-style or "DFER" for (unsupported) old-style
};

struct DFI_TRACK_HEADER
{
    // Note: all values are big-endian
    uint16_t cyl;           // physical cylinder
    uint16_t head;          // physical head
    uint16_t sector;        // physical sector (hard-sectored disks only)
    uint8_t  datalen[4];    // data length, as bytes due to struct alignment
};


class DFIDisk final : public DemandDisk
{
public:
    void add_track_data(const CylHead& cylhead, Data&& data)
    {
        // Determine image clock rate if not yet known. It seems reasonable
        // to assume this will not vary between tracks.
        if (!m_tick_ns)
        {
            uint32_t index_pos = 0;
            for (auto byte : data)
            {
                index_pos += (byte & 0x7f);
                if (byte & 0x80)
                    break;
            }

            // Oddly, the clock frequency isn't stored in the image, so guess it.
            for (auto mhz = 25; !m_tick_ns && mhz <= 100; mhz *= 2)
            {
                auto rpm = 60'000'000ULL * mhz / index_pos;
                if (rpm >= 285 && rpm <= 380)
                    m_tick_ns = 1000 / mhz;
            }

            // Fail if we couldn't determine the clock rate
            if (!m_tick_ns)
                throw util::exception("failed to determine DFI clock frequency");
        }

        m_data[cylhead] = std::move(data);
        extend(cylhead);
    }

protected:
    TrackData load(const CylHead& cylhead, bool /*first_read*/) override
    {
        const auto& data = m_data[cylhead];
        if (data.empty())
            return TrackData(cylhead);

        FluxData flux_revs;
        std::vector<uint32_t> flux_times;
        flux_times.reserve(data.size());

        uint32_t total_time = 0;
        for (auto byte : data)
        {
            if (byte & 0x80)
            {
                flux_revs.push_back(std::move(flux_times));
                flux_times.clear();
                flux_times.reserve(data.size());
            }
            else
            {
                total_time += byte;

                if (byte != 0x7f)
                {
                    flux_times.push_back(total_time * m_tick_ns);
                    total_time = 0;
                }
            }
        }

        if (!flux_times.empty())
            flux_revs.push_back(std::move(flux_times));

        m_data.erase(cylhead);
        return TrackData(cylhead, std::move(flux_revs));
    }

private:
    std::map<CylHead, Data> m_data{};
    uint32_t m_tick_ns = 0;
};


bool ReadDFI(MemFile& file, std::shared_ptr<Disk>& disk)
{
    DFI_FILE_HEADER fh{};

    if (!file.rewind() || !file.read(&fh, sizeof(fh)))
        return false;

    if (!memcmp(fh.signature, "DFER", sizeof(fh.signature)))
        throw util::exception("old-style DiscFerret images are not supported");
    else if (memcmp(fh.signature, "DFE2", sizeof(fh.signature)))
        return false;

    auto dfi_disk = std::make_shared<DFIDisk>();

    for (;;)
    {
        DFI_TRACK_HEADER th;
        if (!file.read(&th, sizeof(th)))
            break;

        if (util::betoh(th.sector) != 1)
            throw util::exception("hard-sectored images are not supported");

        CylHead cylhead(util::betoh(th.cyl), util::betoh(th.head));
        auto data_length = (static_cast<uint32_t>(th.datalen[0]) << 24) | (th.datalen[1] << 16) | (th.datalen[2] << 8) | th.datalen[3];

        Data track_data(data_length);
        if (!file.read(track_data))
            throw util::exception("short file reading ", cylhead, " data");

        dfi_disk->add_track_data(cylhead, std::move(track_data));
    }

    dfi_disk->strType = "DFI";
    disk = dfi_disk;

    return true;
}
