// HFE format for HxC floppy emulator:
//  http://hxc2001.com/download/floppy_drive_emulator/SDCard_HxC_Floppy_Emulator_HFE_file_format.pdf

#include "SAMdisk.h"

// Note: currently only format revision 00 is supported.

constexpr std::string_view HFE_SIGNATURE{ "HXCPICFE" };

struct HFE_HEADER
{
    char header_signature[8];
    uint8_t format_revision;
    uint8_t number_of_tracks;
    uint8_t number_of_sides;
    uint8_t track_encoding;
    uint16_t bitrate_kbps;              // little-endian
    uint16_t floppy_rpm;
    uint8_t floppy_interface_mode;
    uint8_t do_not_use;
    uint16_t track_list_offset;         // in 512-byte blocks
    uint8_t write_allowed;
    uint8_t single_step;                // 0xff = normal, 0x00 = double-step
    uint8_t track0s0_altencoding;       // 0xff = ignore, otherwise use encoding below
    uint8_t track0s0_encoding;          // override encoding for track 0 head 0
    uint8_t track0s1_altencoding;       // 0xff = ignore, otherwise use encoding below
    uint8_t track0s1_encoding;          // override encoding for track 0 head 1
};

struct HFE_TRACK
{
    uint16_t offset;
    uint16_t track_len;
};

enum FloppyInterfaceMode
{
    IBMPC_DD_FLOPPYMODE = 0,
    IBMPC_HD_FLOPPYMODE,
    ATARIST_DD_FLOPPYMODE,
    ATARIST_HD_FLOPPYMODE,
    AMIGA_DD_FLOPPYMODE,
    AMIGA_HD_FLOPPYMODE,
    CPC_DD_FLOPPYMODE,
    GENERIC_SHUGART_DD_FLOPPYMODE,
    IBMPC_ED_FLOPPYMODE,
    MSX2_DD_FLOPPYMODE,
    C64_DD_FLOPPYMODE,
    EMU_SHUGART_FLOPPYMODE,
    S950_DD_FLOPPYMODE,
    S950_HD_FLOPPYMODE,
    DISABLE_FLOPPYMODE = 0xfe
};

enum TrackEncoding
{
    ISOIBM_MFM_ENCODING = 0,
    AMIGA_MFM_ENCODING,
    ISOIBM_FM_ENCODING,
    EMU_FM_ENCODING,
    UNKNOWN_ENCODING = 0xff
};

std::string to_string(FloppyInterfaceMode interface_mode)
{
    switch (interface_mode)
    {
    case IBMPC_DD_FLOPPYMODE:   return "IBMPC_DD_FLOPPYMODE";   break;
    case IBMPC_HD_FLOPPYMODE:   return "IBMPC_HD_FLOPPYMODE";   break;
    case ATARIST_DD_FLOPPYMODE: return "ATARIST_DD_FLOPPYMODE"; break;
    case ATARIST_HD_FLOPPYMODE: return "ATARIST_HD_FLOPPYMODE"; break;
    case AMIGA_DD_FLOPPYMODE:   return "AMIGA_DD_FLOPPYMODE";   break;
    case AMIGA_HD_FLOPPYMODE:   return "AMIGA_HD_FLOPPYMODE";   break;
    case CPC_DD_FLOPPYMODE:     return "CPC_DD_FLOPPYMODE";     break;
    case GENERIC_SHUGART_DD_FLOPPYMODE: return "GENERIC_SHUGART_DD_FLOPPYMODE"; break;
    case IBMPC_ED_FLOPPYMODE:   return "IBMPC_ED_FLOPPYMODE";   break;
    case MSX2_DD_FLOPPYMODE:    return "MSX2_DD_FLOPPYMODE";    break;
    case C64_DD_FLOPPYMODE:     return "C64_DD_FLOPPYMODE";     break;
    case EMU_SHUGART_FLOPPYMODE:return "EMU_SHUGART_FLOPPYMODE"; break;
    case S950_DD_FLOPPYMODE:    return "S950_DD_FLOPPYMODE";    break;
    case S950_HD_FLOPPYMODE:    return "S950_HD_FLOPPYMODE";    break;
    case DISABLE_FLOPPYMODE:    return "DISABLE_FLOPPYMODE";    break;
    }
    return "Unknown";
}

std::string to_string(TrackEncoding track_encoding)
{
    switch (track_encoding)
    {
    case ISOIBM_MFM_ENCODING:   return "ISOIBM_MFM_ENCODING";   break;
    case AMIGA_MFM_ENCODING:    return "AMIGA_MFM_ENCODING";    break;
    case ISOIBM_FM_ENCODING:    return "ISOIBM_FM_ENCODING";    break;
    case EMU_FM_ENCODING:       return "EMU_FM_ENCODING";       break;
    case UNKNOWN_ENCODING:      return "UNKNOWN_ENCODING";      break;
    }
    return "Unknown";
}


bool ReadHFE(MemFile& file, std::shared_ptr<Disk>& disk)
{
    HFE_HEADER hh;
    if (!file.rewind() || !file.read(&hh, sizeof(hh)) || std::string_view(hh.header_signature, sizeof(hh.header_signature)) != HFE_SIGNATURE)
        return false;

    if (hh.format_revision != 0)
        throw util::exception("unsupported HFE format revision (", hh.format_revision, ")");

    HFE_TRACK aTrackLUT[256];
    auto track_lut_offset = util::letoh(hh.track_list_offset) << 9;
    if (!file.seek(track_lut_offset) || !file.read(aTrackLUT, sizeof(aTrackLUT)))
        throw util::exception("failed to read track LUT (@", track_lut_offset, ")");

    auto datarate = DataRate::Unknown;
    auto data_bitrate = util::letoh(hh.bitrate_kbps);
    if (data_bitrate >= 240 && data_bitrate <= 260)
        datarate = DataRate::_250K;
    else if (data_bitrate >= 290 && data_bitrate <= 310)
        datarate = DataRate::_300K;
    else if (data_bitrate >= 490 && data_bitrate <= 510)
        datarate = DataRate::_500K;
    else if (data_bitrate == 0xffff)
        throw util::exception("variable bitrate images are not supported");
    else
        throw util::exception("unsupported data rate (", data_bitrate, "Kbps)");

    Format::Validate(hh.number_of_tracks, hh.number_of_sides);

    // 64K should be enough for maximum MFM track size, and we'll check later anyway
    MEMORY mem(0x10000);
    auto pbTrack = mem.pb;

    for (uint8_t cyl = 0; cyl < hh.number_of_tracks; ++cyl)
    {
        // Offset is in 512-byte blocks, data length covers both heads
        auto uTrackDataOffset = util::letoh(aTrackLUT[cyl].offset) << 9;
        auto uTrackDataLen = util::letoh(aTrackLUT[cyl].track_len) >> 1;

        if (uTrackDataLen > mem.size)
            throw util::exception("invalid track size (", uTrackDataLen, ") for track ", CylStr(cyl));

        for (uint8_t head = 0; head < hh.number_of_sides; ++head)
        {
            // Head 1 data starts 256 bytes in
            if (head == 1)
                uTrackDataOffset += 256;

            auto uRead = 0;
            while (uRead < uTrackDataLen)
            {
                auto chunk = std::min(uTrackDataLen - uRead, 256);

                // Read the next interleaved chunk
                if (!file.seek(uTrackDataOffset + (uRead * 2)) || !file.read(pbTrack + uRead, chunk))
                    throw util::exception("EOF reading track data for ", CH(cyl, head));

                uRead += chunk;
            }

            BitBuffer bitbuf(datarate, pbTrack, uTrackDataLen * 8);
            disk->write(CylHead(cyl, head), std::move(bitbuf));
        }
    }

    disk->metadata["interface_mode"] = to_string(static_cast<FloppyInterfaceMode>(hh.floppy_interface_mode));
    disk->metadata["track_encoding"] = to_string(static_cast<TrackEncoding>(hh.track_encoding));
    disk->metadata["data_bitrate"] = std::to_string(hh.bitrate_kbps) + "Kbps";
    if (hh.floppy_rpm)
        disk->metadata["floppy_rpm"] = std::to_string(hh.floppy_rpm);

    disk->strType = "HFE";
    return true;
}


static uint8_t HfeTrackEncoding(const Track& track)
{
    auto encoding = (opt.encoding != Encoding::Unknown) ? opt.encoding :
        !track.empty() ? track[0].encoding : Encoding::Unknown;

    switch (encoding)
    {
    case Encoding::MFM:
    case Encoding::RX02:
    case Encoding::MX:
    case Encoding::Agat:
        return ISOIBM_MFM_ENCODING;
    case Encoding::FM:
        return ISOIBM_FM_ENCODING;
    case Encoding::Amiga:
        return AMIGA_MFM_ENCODING;
    case Encoding::GCR:
    case Encoding::Ace:
    default:
        break;
    }

    return UNKNOWN_ENCODING;
}

static uint16_t HfeDataRate(const Track& track)
{
    auto datarate = (opt.datarate != DataRate::Unknown) ? opt.datarate :
        !track.empty() ? track[0].datarate : DataRate::Unknown;

    if (datarate != DataRate::Unknown)
    {
        auto kbps = bits_per_second(datarate) / 1000U;
        return static_cast<uint16_t>(kbps);
    }

    return 250U;
}

static uint8_t HfeInterfaceMode(const Track& track)
{
    if (!track.empty())
    {
        switch (track[0].encoding)
        {
        case Encoding::Amiga:
            switch (track[0].datarate)
            {
            case DataRate::_250K:
                return AMIGA_DD_FLOPPYMODE;
            case DataRate::_500K:
                return AMIGA_HD_FLOPPYMODE;
            default:
                break;
            }
            break;
        case Encoding::MFM:
        case Encoding::FM:
            switch (track[0].datarate)
            {
            case DataRate::_250K:
            case DataRate::_300K:
                // Use general DD rather than IBMPC_DD_FLOPPYMODE.
                return GENERIC_SHUGART_DD_FLOPPYMODE;
            case DataRate::_500K:
                return IBMPC_HD_FLOPPYMODE;
            case DataRate::_1M:
                return IBMPC_ED_FLOPPYMODE;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }

    return GENERIC_SHUGART_DD_FLOPPYMODE;
}

bool WriteHFE(FILE* f_, std::shared_ptr<Disk>& disk)
{
    std::vector<uint8_t> header(256, 0xff);
    auto& hh = *reinterpret_cast<HFE_HEADER*>(header.data());

    auto& track0 = disk->read_track({ 0, 0 });

    std::copy(HFE_SIGNATURE.begin(), HFE_SIGNATURE.end(), hh.header_signature);
    hh.format_revision = 0x00;
    hh.number_of_tracks = static_cast<uint8_t>(disk->cyls());
    hh.number_of_sides = static_cast<uint8_t>(disk->heads());
    hh.track_encoding = HfeTrackEncoding(track0);
    hh.bitrate_kbps = util::htole(HfeDataRate(track0));
    hh.floppy_rpm = 0;
    hh.floppy_interface_mode = HfeInterfaceMode(track0);
    hh.do_not_use = 0x01;
    hh.track_list_offset = util::htole(static_cast<uint16_t>(0x200 >> 9));
    hh.write_allowed = 0xff;
    hh.single_step = 0xff;
    hh.track0s0_altencoding = 0xff;
    hh.track0s0_encoding = 0xff;
    hh.track0s1_altencoding = 0xff;
    hh.track0s1_encoding = 0xff;

    if (!fwrite(header.data(), header.size(), 1, f_))
        throw util::exception("write error");
    if (fseek(f_, hh.track_list_offset << 9, SEEK_SET))
        throw util::exception("seek error");

    std::array<HFE_TRACK, MAX_TRACKS> aTrackLUT{};
    std::map<CylHead, BitBuffer> bitstreams;
    int data_offset = 2;

    auto max_disk_track_bytes = 0;
    for (uint8_t cyl = 0; cyl < hh.number_of_tracks; ++cyl)
    {
        auto max_track_bytes = 0;
        for (uint8_t head = 0; head < hh.number_of_sides; ++head)
        {
            CylHead cylhead(cyl, head);
            auto trackdata = disk->read(cylhead);
            auto bitstream = trackdata.preferred().bitstream();
            auto track_bytes = (bitstream.track_bitsize() + 7) / 8;
            max_track_bytes = std::max(track_bytes, max_track_bytes);
            max_disk_track_bytes = std::max(max_disk_track_bytes, max_track_bytes);
            bitstreams[cylhead] = std::move(bitstream);
        }

        aTrackLUT[cyl].offset = util::htole(static_cast<uint16_t>(data_offset));
        aTrackLUT[cyl].track_len = util::htole(static_cast<uint16_t>(max_track_bytes * 2));

        data_offset += ((max_track_bytes * 2) / 512) + 1;
    }

    if (fwrite(aTrackLUT.data(), sizeof(aTrackLUT[0]), aTrackLUT.size(), f_) != aTrackLUT.size())
        throw util::exception("write error");

    MEMORY mem(max_disk_track_bytes * 2 + 512);
    for (uint8_t cyl = 0; cyl < hh.number_of_tracks; ++cyl)
    {
        uint8_t* pbTrack{};
        for (uint8_t head = 0; head < hh.number_of_sides; ++head)
        {
            auto& bitstream = bitstreams[{ cyl, head }];
            auto track_bytes = (bitstream.track_bitsize() + 7) / 8;
            bitstream.seek(0);

            pbTrack = mem.pb + head * 256;
            while (track_bytes > 0)
            {
                auto chunk_size = std::min(track_bytes, 0x100);
                for (int i = 0; i < chunk_size; ++i)
                    *pbTrack++ = bitstream.read8_lsb();
                memset(pbTrack, 0x55, 0x100 - chunk_size);
                pbTrack += 0x200 - chunk_size;
                track_bytes -= chunk_size;
            }
        }

        fseek(f_, util::letoh(aTrackLUT[cyl].offset) * 512, SEEK_SET);
        auto track_len = (util::letoh(aTrackLUT[cyl].track_len) + 511) & ~0x1ff;
        if (fwrite(mem.pb, 1, track_len, f_) != static_cast<size_t>(track_len))
            throw util::exception("write error");
    }

    return true;
}
