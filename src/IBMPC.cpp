// Calculations related to IBM PC format MFM/FM disks (System/34 compatible)

#include "SAMdisk.h"
#include "IBMPC.h"

typedef struct
{
    int drivespeed;
    DataRate datarate;
    Encoding encoding;
    int sectors, size;
    int gap3;
} FORMATGAP;

static const FORMATGAP standard_gaps[] =
{
    { RPM_TIME_300,  DataRate::_1M, Encoding::MFM,   36, 2,  0x53 },    // 2.88M DOS
    { RPM_TIME_300,  DataRate::_500K, Encoding::MFM, 18, 2,  0x65 },    // 1.44M DOS
    { RPM_TIME_300,  DataRate::_250K, Encoding::MFM,  9, 2,  0x50 },    // 720K DOS
#if 0
    { RPM_TIME_300,  DataRate::_250K, Encoding::MFM, 16, 1,  0x0e },    // PC98
    { RPM_TIME_300,  DataRate::_250K, Encoding::MFM,  9, 2,  0x1b },    // PC98
    { RPM_TIME_300,  DataRate::_250K, Encoding::MFM,  5, 3,  0x34 },    // PC98
    { RPM_TIME_300,  DataRate::_500K, Encoding::MFM, 26, 1,  0x0e },    // PC98
    { RPM_TIME_300,  DataRate::_500K, Encoding::MFM, 15, 2,  0x1b },    // PC98
    { RPM_TIME_300,  DataRate::_500K, Encoding::MFM,  8, 3,  0x34 },    // PC98
    { RPM_TIME_300,  DataRate::_250K, Encoding::FM,  16, 0,  0x07 },    // PC98
    { RPM_TIME_300,  DataRate::_250K, Encoding::FM,   9, 1,  0x0e },    // PC98
    { RPM_TIME_300,  DataRate::_250K, Encoding::FM,   5, 2,  0x1b },    // PC98
    { RPM_TIME_300,  DataRate::_250K, Encoding::FM,  26, 0,  0x07 },    // PC98
    { RPM_TIME_300,  DataRate::_250K, Encoding::FM,  15, 1,  0x0e },    // PC98
    { RPM_TIME_300,  DataRate::_250K, Encoding::FM,   8, 2,  0x1b },    // PC98
#endif
};

#ifdef _WIN32
std::string to_string(const MEDIA_TYPE& type)
{
    switch (type)
    {
    case F5_1Pt2_512:   return "5.25\" 1.2M";
    case F3_1Pt44_512:  return "3.5\" 1.44M";
    case F3_2Pt88_512:  return "3.5\" 2.88M";
    case F3_720_512:    return "3.5\" 720K";
    case F5_360_512:    return "5.25\" 360K";
    case F5_320_512:    return "5.25\" 320K";
    case F5_320_1024:   return "5.25\" 320K, 1024 bytes/sector";
    case F5_180_512:    return "5.25\" 180K";
    case F5_160_512:    return "5.25\" 160K";
    case F3_640_512:    return "3.5\" 640K";
    case F5_640_512:    return "5.25\" 640K";
    case F5_720_512:    return "5.25\" 720K";
    case F3_1Pt2_512:   return "3.5\" 1.2M";
    case F3_1Pt23_1024: return "3.5\" 1.23M, 1024 bytes/sector";
    case F5_1Pt23_1024: return "5.25\" 1.23M, 1024 bytes/sector";
    case F8_256_128:    return "8\" 256K";
    default:            return "Unknown";
    }
}
#endif

// Return the number of microseconds for 1 byte at the given rate
int GetDataTime(DataRate datarate, Encoding encoding, int len_bytes/*=1*/, bool add_drain_time/*=false*/)
{
    auto uTime = 1000000 / (bits_per_second(datarate) / 8);
    if (encoding == Encoding::FM) uTime <<= 1;
    return (uTime * len_bytes) + (add_drain_time ? (uTime * 69 / 100) : 0);     // 0.69 250Kbps bytes @300rpm = 86us = FDC data drain time
}

int GetTrackOverhead(Encoding encoding)
{
    return (encoding == Encoding::MFM) ? TRACK_OVERHEAD_MFM : TRACK_OVERHEAD_FM;
}

int GetSectorOverhead(Encoding encoding)
{
    return (encoding == Encoding::MFM) ? SECTOR_OVERHEAD_MFM : SECTOR_OVERHEAD_FM;
}

int GetDataOverhead(Encoding encoding)
{
    return (encoding == Encoding::MFM) ? DATA_OVERHEAD_MFM : DATA_OVERHEAD_FM;
}

int GetSyncOverhead(Encoding encoding)
{
    return (encoding == Encoding::MFM) ? SYNC_OVERHEAD_MFM : SYNC_OVERHEAD_FM;
}

int GetRawTrackCapacity(int drive_speed, DataRate datarate, Encoding encoding)
{
    auto len_bits = bits_per_second(datarate);
    assert(len_bits != 0);

    auto len_bytes = ((len_bits / 8) * (drive_speed / 10)) / 100000;
    if (encoding == Encoding::FM) len_bytes >>= 1;
    return len_bytes;
}

int GetTrackCapacity(int drive_speed, DataRate datarate, Encoding encoding)
{
    return GetRawTrackCapacity(drive_speed, datarate, encoding) * 1995 / 2000;
}

int GetFormatLength(Encoding encoding, int sectors, int size, int gap3)
{
    return ((Sector::SizeCodeToLength(size) + GetSectorOverhead(encoding) + gap3) * sectors);
}

int GetUnformatSizeCode(DataRate datarate)
{
    switch (datarate)
    {
    case DataRate::_250K:   return 6;
    case DataRate::_300K:   return 6;
    case DataRate::_500K:   return 7;
    case DataRate::_1M:     return 8;
    default:                break;
    }

    return 7;
}

int GetFormatGap(int drive_speed, DataRate datarate, Encoding encoding, int sectors, int size_)
{
    if (!sectors) return 0;

    // Check for common formats that use specific gap sizes
    for (auto& fg : standard_gaps)
    {
        // If the format matches exactly, return the known gap
        if (fg.drivespeed == drive_speed && fg.datarate == datarate && fg.encoding == encoding &&
            fg.sectors == sectors && fg.size == size_)
            return fg.gap3;
    }

    auto track_len = GetTrackCapacity(drive_speed, datarate, encoding) - GetTrackOverhead(encoding);
    auto chunk = track_len / sectors;
    auto overhead = Sector::SizeCodeToLength(size_) + GetSectorOverhead(encoding);
    auto gap3 = (chunk > overhead) ? chunk - overhead : 0;
    return (gap3 > MAX_GAP3) ? MAX_GAP3 : gap3;
}

bool FitTrackIBMPC(const CylHead& cylhead, const Track& track, int track_time_ms, FitDetails& details)
{
    if (track.empty() || track.is_mixed_encoding())
        return false;

    auto datarate = track[0].datarate;
    auto encoding = track[0].encoding;
    if (encoding != Encoding::MFM && encoding != Encoding::FM)
        return false;

    auto track_space = GetTrackCapacity(track_time_ms, datarate, encoding);
    auto sector_overhead = GetSectorOverhead(encoding);

    details.sector_units.resize(track.size());
    std::vector<int> sizes(track.size());
    int min_size, max_size;

    // Check both with real errors and dummy errors
    for (details.real_errors = true; ; details.real_errors = false)
    {
        bool all_errors = true;
        min_size = SIZE_MASK_765;
        max_size = 0;

        // Determine the largest and smallest true sector sizes.
        for (int i = 0; i < track.size(); ++i)
        {
            auto& s = track[i];

            if (!details.real_errors && !s.has_good_data())
                sizes[i] = -1;
            else
            {
                sizes[i] = Sector::SizeCodeToRealSizeCode(s.header.size);
                min_size = std::min(sizes[i], min_size);
                max_size = std::max(sizes[i], max_size);
                all_errors &= !s.has_good_data();
            }
        }

        // If all sectors are bad, consider the largest size that works.
        if (all_errors)
            max_size = SIZE_MASK_765;

        // Attempt to fit using simple size multiples, and either trusting or ignoring the size
        // of sectors that don't have a good data field.
        for (details.size_code = max_size; details.size_code >= 0; --details.size_code)
        {
            details.total_units = 0;
            for (int i = 0; i < track.size(); ++i)
            {
                details.sector_units[i] = (sizes[i] < 0 || sizes[i] < details.size_code) ? 1 :
                    (Sector::SizeCodeToLength(sizes[i]) / Sector::SizeCodeToLength(details.size_code));
                details.total_units += details.sector_units[i];
            }

            if (!details.total_units)
                continue;

            // Try standard format gaps, or calculate one that is suitable
            details.gap3 =
                GetFormatGap(track_time_ms, datarate, encoding, details.total_units, details.size_code);

            if (details.gap3)
            {
                // In debug, show the unit count wasn't obvious (unless forced)
                if (opt.debug && (details.total_units != track.size() || opt.verbose))
                    util::cout << "FitTrack: " << cylhead << " fits with " << details.total_units <<
                    " units of size=" << details.size_code << " gap3=" << details.gap3 << "\n";

                return true;
            }
        }

        // Attempt a more difficult fit using size and gap3 variations.
        for (details.size_code = min_size; details.size_code >= 0; --details.size_code)
        {
            for (details.gap3 = 255; details.gap3 > 0; --details.gap3)
            {
                auto unit_length =
                    sector_overhead + Sector::SizeCodeToLength(details.size_code) + details.gap3;

                details.total_units = 0;
                for (int i = 0; i < track.size(); ++i)
                {
                    auto needed_length = sector_overhead +
                        Sector::SizeCodeToLength(((sizes[i] < 0) ? details.size_code : sizes[i]));

                    details.sector_units[i] = (needed_length + unit_length - 1) / unit_length;
                    details.total_units += details.sector_units[i];
                }

                auto total_size =
                    GetFormatLength(encoding, details.total_units, details.size_code, details.gap3);
                if (total_size < track_space)
                {
                    if (opt.debug)
                        util::cout << "FitTrack: " << cylhead << " fits with " <<
                        details.total_units << " clipped units of size=" << details.size_code <<
                        " gap3=" << details.gap3 << "\n";
                    return true;
                }
            }
        }

        if (!details.real_errors)
            break;
    }

    if (opt.debug)
        util::cout << colour::RED << "FitTrack: unable to fit " << cylhead << colour::none << "\n";

    return false;
}
