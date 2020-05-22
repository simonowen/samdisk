// MAME/MESS floppy image
//
// https://github.com/mamedev/mame/blob/master/src/lib/formats/mfi_dsk.cpp

#include "SAMdisk.h"
#include "DemandDisk.h"
#include "BitstreamDecoder.h"

#ifdef HAVE_ZLIB
#include "zlib.h"
#endif

// Note: all values are little-endian
struct MFI_FILE_HEADER
{
    char signature[16]; // "MESSFLOPPYIMAGE"
    uint32_t cyl_count, head_count;
    uint32_t form_factor, variant;
};

struct MFI_TRACK_HEADER
{
    uint32_t offset, compressed_size, uncompressed_size, write_splice;
};

//! Floppy format data
enum {
    TIME_MASK = 0x0fffffff,
    MG_MASK = 0xf0000000,
    MG_SHIFT = 28, //!< Bitshift constant for magnetic orientation data
    MG_A = (0 << MG_SHIFT),    //!< - 0, MG_A -> Magnetic orientation A
    MG_B = (1 << MG_SHIFT),    //!< - 1, MG_B -> Magnetic orientation B
    MG_N = (2 << MG_SHIFT),    //!< - 2, MG_N -> Non-magnetized zone (neutral)
    MG_D = (3 << MG_SHIFT),    //!< - 3, MG_D -> Damaged zone, reads as neutral but cannot be changed by writing
    RESOLUTION_SHIFT = 30,
    CYLINDER_MASK = 0x3fffffff
};

//! Form factors
enum {
    FF_UNKNOWN = 0x00000000, //!< Unknown, useful when converting
    FF_3 = 0x20202033, //!< "3   " 3 inch disk
    FF_35 = 0x20203533, //!< "35  " 3.5 inch disk
    FF_525 = 0x20353235, //!< "525 " 5.25 inch disk
    FF_8 = 0x20202038  //!< "8   " 8 inch disk
};

//! Variants
enum {
    SSSD = 0x44535353, //!< "SSSD", Single-sided single-density
    SSDD = 0x44445353, //!< "SSDD", Single-sided double-density
    SSQD = 0x44515353, //!< "SSQD", Single-sided quad-density
    DSSD = 0x44535344, //!< "DSSD", Double-sided single-density
    DSDD = 0x44445344, //!< "DSDD", Double-sided double-density (720K in 3.5, 360K in 5.25)
    DSQD = 0x44515344, //!< "DSQD", Double-sided quad-density (720K in 5.25, means DD+80 tracks)
    DSHD = 0x44485344, //!< "DSHD", Double-sided high-density (1440K)
    DSED = 0x44455344  //!< "DSED", Double-sided extra-density (2880K)
};


class MFIDisk final : public DemandDisk
{
public:
    void add_track_data(const CylHead& cylhead, std::vector<uint32_t>&& trackdata)
    {
        m_data[cylhead] = std::move(trackdata);
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
        for (auto time : data)
        {
            flux_times.push_back(time);
            total_time += time;
        }

        if (total_time != 200000000)
            throw util::exception("wrong total_time for ", cylhead, ": ", total_time);

        if (!flux_times.empty())
            flux_revs.push_back(std::move(flux_times));

        // causes random crashes
        //  m_data.erase(cylhead);

        return TrackData(cylhead, std::move(flux_revs));
    }

private:
    std::map<CylHead, std::vector<uint32_t>> m_data{};

};


bool ReadMFI(MemFile& file, std::shared_ptr<Disk>& disk)
{
    MFI_FILE_HEADER fh{};

    if (!file.rewind() || !file.read(&fh, sizeof(fh)))
        return false;

    if (memcmp(fh.signature, "MESSFLOPPYIMAGE", sizeof(fh.signature)))
        return false;

#ifndef HAVE_ZLIB
    throw util::exception("MFI disk images are not supported without ZLIB");
#else
    if ((fh.cyl_count >> RESOLUTION_SHIFT) > 0)
        throw util::exception("half- and quarter-track MFI images are not supported");

    if ((fh.cyl_count & CYLINDER_MASK) > 84 || fh.head_count > 2)
        return false;

    auto mfi_disk = std::make_shared<MFIDisk>();

    fh.cyl_count &= CYLINDER_MASK;

    for (unsigned int cyl = 0; cyl < fh.cyl_count; cyl++)
    {
        for (unsigned int head = 0; head < fh.head_count; head++)
        {
            MFI_TRACK_HEADER th;
            if (!file.read(&th, sizeof(th)))
                break;

            CylHead cylhead(cyl, head);

            Data compressed_data(util::letoh(th.compressed_size));
            std::vector<uint32_t> track_data(util::letoh(th.uncompressed_size) >> 2);

            auto o = file.tell();
            file.seek(util::letoh(th.offset));
            if (!file.read(compressed_data))
                throw util::exception("short file reading ", cylhead, " data");

            file.seek(o);

            uLongf size = util::letoh(th.uncompressed_size);
            int rc = uncompress((Bytef*)&track_data[0], &size, &compressed_data[0], util::letoh(th.compressed_size));
            if (rc != Z_OK)
            {
                util::cout << "decompress of " << cylhead << " failed, rc " << rc << "\n";
                util::cout << "sizes " << util::letoh(th.compressed_size) << " to " << util::letoh(th.uncompressed_size) << "\n";
                return false;
            }

            // XXX redo
            std::transform(track_data.begin(), track_data.end(), track_data.begin(),
                [](uint32_t c) -> uint32_t { return (util::letoh(c) & TIME_MASK); });

            mfi_disk->add_track_data(cylhead, std::move(track_data));
        }
    }

    mfi_disk->metadata["form_factor"] = util::fmt("%08X", fh.form_factor);
    mfi_disk->metadata["variant"] = util::fmt("%08X", fh.variant);

    mfi_disk->strType = "MFI";
    disk = mfi_disk;

    return true;
#endif
}

static uint32_t MfiVariant(const Track& track, int cyls, int heads)
{
    if (!track.empty())
    {
        switch (track[0].encoding)
        {
        case Encoding::FM:
            return heads == 2 ? DSSD : SSSD;
        case Encoding::Amiga:
        case Encoding::MFM:
            switch (track[0].datarate)
            {
            case DataRate::_250K:
            case DataRate::_300K:
                return cyls > 40 ? DSQD : DSDD;
            case DataRate::_500K:
                return DSHD;
            case DataRate::_1M:
                return DSED;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }

    return SSSD;
}


bool WriteMFI(FILE* f_, std::shared_ptr<Disk>& disk)
{
#ifndef HAVE_ZLIB
    throw util::exception("MFI disk images are not supported without ZLIB");
#else
    int tracks, heads;

    std::vector<uint8_t> header(sizeof(MFI_FILE_HEADER), 0);
    auto& fh = *reinterpret_cast<MFI_FILE_HEADER*>(header.data());
    auto& track0 = disk->read_track({ 0, 0 });

    tracks = static_cast<uint32_t>(disk->cyls());
    heads = static_cast<uint32_t>(disk->heads());

    strncpy(fh.signature, "MESSFLOPPYIMAGE", sizeof(fh.signature));
    fh.cyl_count = util::htole(tracks);
    fh.head_count = util::htole(heads);
    fh.form_factor = util::htole(FF_UNKNOWN);
    fh.variant = util::htole(MfiVariant(track0, disk->cyls(), disk->heads()));

    if (!fwrite(header.data(), header.size(), 1, f_))
        throw util::exception("write error");

    std::map<CylHead, MFI_TRACK_HEADER> track_lut;
    std::map<CylHead, FluxData> bitstreams;
    int pos = sizeof(MFI_FILE_HEADER) + tracks * heads * sizeof(MFI_TRACK_HEADER);

    if (fseek(f_, pos, SEEK_SET) < 0)
        throw util::exception("seek error");

    //

    auto max_disk_track_bytes = 0;
    for (int cyl = 0; cyl < tracks; ++cyl)
    {
        auto max_track_bytes = 0;
        for (int head = 0; head < heads; ++head)
        {
            CylHead cylhead(cyl, head);
            auto trackdata = disk->read(cylhead);
            auto bitstream = trackdata.preferred().flux();
            auto track_bytes = static_cast<int>(bitstream[0].size() * 4);
            max_track_bytes = std::max(track_bytes, max_track_bytes);
            max_disk_track_bytes = std::max(max_disk_track_bytes, max_track_bytes);
            bitstreams[cylhead] = std::move(bitstream);
        }
    }

    for (int cyl = 0; cyl < tracks; ++cyl)
    {
        for (int head = 0; head < heads; ++head)
        {
            CylHead cylhead(cyl, head);
            auto& bitstream = bitstreams[cylhead];
            auto track_size = bitstream[0].size() + 1;
            int orient = 0;
            unsigned int total_sum = 0;

            std::vector<uint32_t> track_data(track_size - 1);
            Data compressed_data(track_size * 4 + 1000);

            std::transform(bitstream[0].begin(), bitstream[0].end(), track_data.begin(),
                [&orient, &total_sum](uint32_t a) -> uint32_t {
                    orient ^= 1; total_sum += a; return (a) | (orient ? MG_B : MG_A);
                });

            // Normalize the times in a cell buffer to sum up to 200000000
            unsigned int current_sum = 0;
            for (unsigned int i = 0; i != track_data.size(); i++) {
                uint32_t time = track_data[i] & TIME_MASK;
                track_data[i] = (track_data[i] & MG_MASK) | (200000000ULL * time / total_sum);
                current_sum += (track_data[i] & TIME_MASK);
            }

            if (current_sum < 200000000)
            {
                track_data.push_back((200000000 - current_sum) | (orient ? MG_B : MG_A));
            }

            auto csize = static_cast<uLongf>(track_size * 4 + 1000);
            int rc = compress(&compressed_data[0], &csize, (const Bytef*)&track_data[0], static_cast<uLongf>(track_size * 4));
            if (rc != Z_OK) {
                util::cout << "compress of " << CylHead(cyl, head) << " failed, rc " << rc << "\n";
                return false;
            }

            track_lut[cylhead] = {
                util::htole(static_cast<uint32_t>(pos)),
                util::htole(static_cast<uint32_t>(csize)),
                util::htole(static_cast<uint32_t>(track_size * 4)),
                0 };

            if (!fwrite(compressed_data.data(), csize, 1, f_))
                throw util::exception("write error");
            pos += csize;
        }
    }

    if (fseek(f_, sizeof(MFI_FILE_HEADER), SEEK_SET) < 0)
        throw util::exception("seek error");

    for (int cyl = 0; cyl < tracks; ++cyl)
    {
        for (int head = 0; head < heads; ++head)
        {
            if (!fwrite(&track_lut[{cyl, head}], sizeof(MFI_TRACK_HEADER), 1, f_))
                throw util::exception("write error");
        }
    }

    return true;
#endif
}
