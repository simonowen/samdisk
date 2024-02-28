// Built-in disk images used for testing

#include "SAMdisk.h"
#include "IBMPC.h"
#include "BitstreamTrackBuilder.h"
#include "BitstreamDecoder.h"
#include "FluxTrackBuilder.h"
#include "SpecialFormat.h"

static Track& complete(Track& track)
{
    uint8_t fill = 0;

    for (auto& sector : track)
    {
        // Add test data to sectors that lack it
        if (!sector.copies())
            sector.add(Data(sector.size(), fill));

        // Remove data from sectors with 0 bytes of data (for no-data sectors)
        else if (!sector.data_size())
            sector.datas().clear();

        ++fill;
    }

    return track;
}


template<typename T>
void fill(T& x, uint8_t val) {
    std::fill(x.begin(), x.end(), val);
}

template<typename T>
void fill(T& x, size_t from, uint8_t val) {
    std::fill(x.begin() + from, x.end(), val);
}

template<typename T>
void fill(T& x, size_t from, size_t to, uint8_t val) {
    std::fill(x.begin() + from, x.begin() + to, val);
}

template<typename T>
void iota(T& x, uint8_t first_val) {
    std::iota(x.begin(), x.end(), first_val);
}

template<typename T>
void iota(T& x, size_t from, uint8_t first_val) {
    std::iota(x.begin() + from, x.end(), first_val);
}

template<typename T>
void iota(T& x, size_t from, size_t to, uint8_t first_val) {
    std::iota(x.begin() + from, x.begin() + to, first_val);
}


bool ReadBuiltIn(const std::string& path, std::shared_ptr<Disk>& disk)
{
    if (!IsBuiltIn(path))
        return false;

    CylHead cylhead(0, 0);
    uint8_t i;

    auto type = std::strtol(path.c_str() + 1, NULL, 0);
    switch (type)
    {
        // 500Kbps examples
    case 0:
    {
        disk->metadata["comment"] = "500Kbps examples";
#if 0
        // 21 sectors/track
        {
            Track track(21);

            for (i = 0; i < 21; ++i)
            {
                Sector sector(DataRate::_500K, Encoding::MFM, Header(cylhead, 1 + i, 2));
                track.add(std::move(sector));
            }

            disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Sega System 24
        {
            Track track(6);

            for (i = 0; i < 6; ++i)
            {
                static const uint8_t sizes[] = { 6,3,3,3,2,1 };
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, sizes[i]));
                track.add(std::move(sector));
            }

            disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
        }
#endif
        // Missing data fields
        {
            Track track(18);

            for (i = 0; i < 18; ++i)
            {
                Sector sector(DataRate::_500K, Encoding::MFM, Header(cylhead, 1 + i, 2));
                if (!(i % 10)) sector.add(Data());
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Data CRC errors
        {
            Track track(18);

            for (i = 0; i < 18; ++i)
            {
                Sector sector(DataRate::_500K, Encoding::MFM, Header(cylhead, 1 + i, 2));
                if (!((i + 5) % 10)) sector.add(Data(sector.size(), i), true);
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // 65 x 128-byte sectors
        {
            Track track(65);

            for (i = 0; i < 65; ++i)
            {
                Sector sector(DataRate::_500K, Encoding::MFM, Header(cylhead, 1 + i, 0));
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        break;
    }

    // 300Kbps examples
    case 1:
    {
        disk->metadata["comment"] = "300Kbps examples";

        // BBC FM
        {
            Track track(10);

            for (i = 0; i < 10; ++i)
            {
                Sector sector(DataRate::_300K, Encoding::FM, Header(cylhead, 1 + i, 1));
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        break;
    }

    // 250Kbps examples
    case 2:
    {
        disk->metadata["comment"] = "250Kbps examples";
#if 0
        // 11-sector Atari ST
        {
            Track track(11);

            for (i = 0; i < 11; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, i + 1, 2));
                track.add(std::move(sector));
            }

            disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
        }
#endif
        // +3 Speedlock sector (fully weak - Arctic Fox)
        {
            Track track(9);

            for (i = 0; i < 9; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 2));

                if (i >= 2 && i <= 7)
                    sector.add(Data(sector.size(), i), false, 0xf8);

                track.add(std::move(sector));
            }

            // Add Speedlock signature to first sector
            Data data0(512, 0);
            std::string sig{ "SPEEDLOCK" };
            std::copy(sig.begin(), sig.end(), data0.begin() + 304);
            track[0].add(Data(data0));

            // Create 3 different copies of the weak sector
            iota(data0, 0);
            track[1].add(Data(data0), true);
            iota(data0, 1);
            track[1].add(Data(data0), true);
            iota(data0, 2);
            track[1].add(Data(data0), true);

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }


        // +3 Speedlock sector (partially weak - Robocop)
        {
            Track track(9);

            for (i = 0; i < 9; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 2));

                if (i >= 2 && i <= 7)
                    sector.add(Data(sector.size(), i), false, 0xf8);

                track.add(std::move(sector));
            }

            // Add Speedlock signature to first sector
            Data data0(512, 0);
            const std::string sig{ "SPEEDLOCK" };
            std::copy(sig.begin(), sig.end(), data0.begin() + 304);
            track[0].add(std::move(data0));

            // Add 3 copies with differences matching the Robocop weakness (and SAMdisk v3)
            Data data1(512, 0);
            fill(data1, 0, 256, 0xe5);
            track[1].add(Data(data1), true);
            iota(data1, 256, 1);
            fill(data1, 256 + 32, 256 + 32 + 48, 2);
            track[1].add(Data(data1), true);
            iota(data1, 256, 0);
            fill(data1, 256 + 32, 256 + 32 + 48, 1);
            track[1].add(Data(data1), true);

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // CPC Speedlock sector (partially weak)
        {
            Track track(9);

            for (i = 0; i < 9; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 64 + i, 2));
                track.add(std::move(sector));
            }

            // Add Speedlock signature to first sector
            Data data0(512, 0);
            const std::string sig{ "SPEEDLOCK" };
            std::copy(sig.begin(), sig.end(), data0.begin() + 257);
            track[0].add(std::move(data0));

            // Add 3 copies with differences matching the typical weak sector
            Data data7(512, 0);
            fill(data7, 0, 256, 0xe5);
            track[7].add(Data(data7), true);
            iota(data7, 256, 1);
            fill(data7, 256 + 40, 256 + 40 + 40, 2);
            track[7].add(Data(data7), true);
            iota(data7, 256, 0);
            fill(data7, 256 + 40, 256 + 40 + 40, 1);
            track[7].add(Data(data7), true);

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Rainbow Arts partially weak sector
        {
            static constexpr uint8_t ids[]{ 193,198,194,109,195,200,196,201,197 };
            Track track(arraysize(ids));

            for (auto id : ids)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, id, 2));
                track.add(std::move(sector));
            }

            // Add Speedlock signature to first sector
            Data data0(512, 0);
            const std::string sig{ "SPEEDLOCK" };
            std::copy(sig.begin(), sig.end(), data0.begin() + 9);
            track[0].add(std::move(data0));

            // Add 3 copies with differences matching the typical weak sector
            Data data1(512, 0);
            fill(data1, 0, 256, 0xe5);
            track[1].add(Data(data1), true);
            iota(data1, 100, 100 + 207, 101);
            fill(data1, 100 + 207, 2);
            track[1].add(Data(data1), true);
            iota(data1, 100, 100 + 207, 101);
            fill(data1, 100 + 207, 1);
            track[1].add(Data(data1), true);

            Data data3(512, 0);
            const std::string codesig{ "\x2a\x6d\xa7\x01\x30\x01\xaf\xed\x42\x4d\x44\x21\x70\x01" };
            std::copy(codesig.begin(), codesig.end(), data3.begin());
            track[3].add(std::move(data3));

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // KBI-10 weak sector
        {
            static constexpr uint8_t ids[]{ 193,198,194,109,195,200,196,201,197,202 };
            Track track(arraysize(ids));

            for (i = 0; i < 10; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, ids[i], (i == 9) ? 1 : 2));
                track.add(std::move(sector));
            }

            Data data9(256, 0xe5);
            const std::string sig{ "KBI." };
            std::copy(sig.begin(), sig.end(), data9.begin());
            track[9].add(Data(data9), true);

            iota(data9, 4, 4 + 4, 5);
            iota(data9, 4 + 4 + 124, 4 + 4 + 124 + 4, 125);
            track[9].add(Data(data9), true);
            iota(data9, 4, 4 + 4, 4);
            iota(data9, 4 + 4 + 124, 4 + 4 + 124 + 4, 124);
            track[9].add(Data(data9), true);

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // KBI-10 weak sector (alt)
        {
            static constexpr uint8_t ids[]{ 65,70,66,71,67,72,68,73,69,74 };
            Track track(arraysize(ids));

            for (i = 0; i < 10; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, ids[i], (i == 9) ? 1 : 2));
                track.add(std::move(sector));
            }

            Data data9(256, 0xe5);
            const std::string sig{ "KBI." };
            std::copy(sig.begin(), sig.end(), data9.begin());
            track[9].add(Data(data9), true);

            iota(data9, 4, 4 + 4, 5);
            iota(data9, 4 + 4 + 124, 4 + 4 + 124 + 4, 125);
            track[9].add(Data(data9), true);
            iota(data9, 4, 4 + 4, 4);
            iota(data9, 4 + 4 + 124, 4 + 4 + 124 + 4, 124);
            track[9].add(Data(data9), true);

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Missing data fields
        {
            Track track(10);

            for (i = 0; i < 10; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 2));
                if (!(i % 5)) sector.add(Data());
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // ID field CRC error
        {
            Track track(10);

            for (i = 0; i < 10; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 2));
                if (!(i % 5)) sector.set_badidcrc();
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Data CRC error
        {
            Track track(10);

            for (i = 0; i < 10; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 2));
                if (!(i % 5)) sector.add(Data(sector.size(), i), true);
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }


        // Jim Power with data hidden in gap after first sector (broken in SAMdisk v3.7+ 23???)
        {
            Track track(10);

            for (i = 0; i < 10; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 2));
                track.add(std::move(sector));
            }

            Data data0(512, 0x01);

            CRC16 crc("\xa1\xa1\xa1", 3);
            crc.add(0xfb);
            crc.add(data0.data(), 512);
            data0.insert(data0.end(), crc.msb());
            data0.insert(data0.end(), crc.lsb());

            data0.insert(data0.end(), 1, 0x4e);
            data0.insert(data0.end(), 9, 0xf7);
            track[0].add(std::move(data0));

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Single sector with gap data
        {
            Track track(1);
            Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1, 0));
            track.add(std::move(sector));

            const std::string sig{ "HELLO!" };
            Data data0(6144, 0x01);
            fill(data0, 0, sector.size(), 0x00);
            std::copy(sig.begin(), sig.end(), std::prev(data0.end(), sig.size()));

            CRC16 crc("\xa1\xa1\xa1", 3);
            crc.add(0xfb);
            crc.add(data0.data(), sector.size());
            data0[sector.size() + 0] = crc >> 8;
            data0[sector.size() + 1] = crc & 0xff;
            track[0].add(std::move(data0));

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Le Necromancien typical track, with gap data conflicting with EDSK multiple copies extension
        {
            static constexpr uint8_t ids[]{ 1,1,6,2,7,3,8,4,9,5 };
            Track track(arraysize(ids));

            for (i = 0; i < 10; ++i)
            {
                Header header((i == 1) ? 1 : cylhead.cyl, (i == 1) ? 4 : cylhead.head, ids[i], i ? 2 : 1);
                Sector sector(DataRate::_250K, Encoding::MFM, header);
                if (i == 0) sector.add(Data(512, 0xf7), true);
                if (i == 1) sector.add(Data(sector.size(), i), false, 0xf8);
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Defenders of the Earth (+3)
        {
            Track track(10);

            for (i = 0; i < 10; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(206, 201, 196, 191));
                sector.add(Data(sector.size(), i), true);
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // World Games (CPC)
        {
            Track track(9);

            for (i = 0; i < 9; ++i)
            {
                Header header(cylhead, (i == 8) ? 121 : 129 + i, (i == 8) ? 0 : 2);
                Sector sector(DataRate::_250K, Encoding::MFM, header);
                if (i == 8) sector.add(Data());
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // SP6
        {
            Track track(9);

            for (i = 0; i < 9; ++i)
            {
                Header header(cylhead, 1 + i, (i == 8) ? 0 : 2);
                Sector sector(DataRate::_250K, Encoding::MFM, header);
                if (i == 7) sector.header = Header(203, 253, 188, 221);
                if (i == 7 || i == 8) sector.add(Data(sector.size(), i), true);
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Tomahawk (track 3)
        {
            Track track(9);

            for (i = 0; i < 9; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(0, 0, 0, 2));
                sector.add(Data(sector.size(), i), false, 0xf8);
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // 8K sector
        {
            Track track(1);

            Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 193, 6));
            sector.add(Data(sector.size(), i), true, 0xf8);
            track.add(std::move(sector));

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Logo Professor (overformatted track, with offsets)
        {
            Track track(10);
            track.tracklen = 6250 * 16;

            for (i = 0; i < 10; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 2 + i, 2));
                sector.offset = (TRACK_OVERHEAD_MFM + (SECTOR_OVERHEAD_MFM + 512 + 25) * (i + 1)) * 16;
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Edd the Duck (track 7)
        {
            static constexpr uint8_t ids[]{ 193,65,70,66,71,67,72,68,73,69,74 };
            Track track(arraysize(ids));

            for (auto id : ids)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, id, (id == 193) ? 6 : 2));
                if (id == 193) sector.add(Data(sector.size(), i), true);
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Alternative for KBI-10 weak sector
        {
            static constexpr uint8_t ids[]{ 193,198,194,199,202,195,200,196,201,197,202 };
            Track track(arraysize(ids));

            for (auto id : ids)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, id, (id == 202) ? 1 : 2));
                if (id == 202) sector.add(Data(sector.size(), i), true);
                track.add(std::move(sector));
            }

            std::string sig{ "KBI" };
            Data data3(512, 0);
            std::copy(sig.begin(), sig.end(), data3.begin());
            track[3].add(std::move(data3));

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // KBI-19: CPC Titan (cyl 14) and Mach 3 (cyl 40)
        {
            static constexpr uint8_t ids[]{ 0,1,4,7,10,13,16,2,5,8,11,14,17,3,6,9,12,15,18 };
            Track track(arraysize(ids));

            for (auto id : ids)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, id, 2));
                track.add(std::move(sector));
            }

            auto trackdata = GenerateKBI19Track(cylhead.next_cyl(), complete(track));
            disk->write(std::move(trackdata));
        }

        // CAL2BOOT.DMK (track 9)
        {
            static constexpr uint8_t ids[]{ 1,8,17,9,18 };
            Track track(arraysize(ids));

            for (auto id : ids)
            {
                Header header(39, cylhead.head, id, 2);
                if (id == 1) header = Header(20, 0, id, 3);
                Sector sector(DataRate::_250K, Encoding::MFM, header);
                if (id == 1 || id == 18) sector.add(Data(sector.size(), i), true);
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Titus the Fox (track 40)
        {
            static constexpr uint8_t ids[]{ 193,198,194,199,195,202,12 };
            Track track(arraysize(ids));

            for (auto id : ids)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(40, cylhead.head, id, 2));
                if (id == 12)
                {
                    sector.header.cyl = 90;
                    sector.header.size = 5;

                    std::string sig{ "Prehistorik Protection !!! Titus Software Nov 1991" };
                    Data data12(128, 0x4e);
                    std::copy(sig.begin(), sig.end(), data12.begin());
                    sector.add(std::move(data12), true);
                }
                track.add(std::move(sector));
            }

            disk->write(GeneratePrehistorikTrack(cylhead.next_cyl(), complete(track)));
        }
#if 0
        // Titus the Fox (track 40), alt format with unused KBI sectors.
        {
            static constexpr uint8_t ids[]{ 193,198,194,199,195,202,12,3,6,9,12,15,18 };
            Track track(arraysize(ids));

            for (auto id : ids)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(40, cylhead.head, id, 2));
                if (id == 12)
                {
                    sector.header.cyl = 90;
                    sector.header.size = 5;

                    std::string sig{ "Prehistorik Protection !!! Titus Software Nov 1991" };
                    Data data12(128, 0x4e);
                    std::copy(sig.begin(), sig.end(), data12.begin());
                    sector.add(std::move(data12), true);
                }
                track.add(std::move(sector));
            }

            // TODO: enhance GeneratePrehistorikTrack to handle this special case.
            disk->write(GeneratePrehistorikTrack(cylhead.next_cyl(), complete(track)));
        }
#endif
        // Prehistoric (track 39)
        {
            static constexpr uint8_t ids[]{ 193,198,194,199,195,200,196,201,197,202,12 };
            Track track(arraysize(ids));

            for (auto id : ids)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(39, cylhead.head, id, 2));
                if (id == 12)
                {
                    sector.header.cyl = 89;
                    sector.header.size = 5;

                    std::string sig{ "Prehistorik Protection !!! Titus Software Nov 1991" };
                    Data data12(128, 0x4e);
                    std::copy(sig.begin(), sig.end(), data12.begin());
                    sector.add(std::move(data12), true);
                }
                track.add(std::move(sector));
            }

            disk->write(GeneratePrehistorikTrack(cylhead.next_cyl(), complete(track)));
        }

        // Prehistoric 2 (track 30)
        {
            static constexpr uint8_t ids[]{ 1,193,2,194,3,195,4,196,5,197,6,198,7,199 };
            Track track(arraysize(ids));

            for (auto id : ids)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, id, 2));
                if (id <= 7) sector.add(Data(sector.size(), i), true);
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Tetris
        {
            Track track(16);

            for (i = 0; i < 16; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(i, i, i, i));
                if (i > 0) sector.add(Data(sector.size(), i), true);
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Terre et Conquerants
        {
            Track track(30);

            for (i = 0; i < 30; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, i, (i == 29) ? 2 : 5));
                if (i != 29) sector.add(Data(sector.size(), i), true);
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // 19 x 256-byte sectors
        {
            Track track(19);

            for (i = 0; i < 19; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 1));
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Opera Soft 32K sector
        {
            Track track(9);

            for (i = 0; i < 9; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, i, (i == 8) ? 8 : 1));
                if (i == 8) sector.add(Data(sector.size(), i), true);
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Sports Hero + Mugsy (+3)
        {
            static constexpr uint8_t ids[]{ 7,14,3,10,17,6,13,2,9,16,5,12,1,8,15,4,11,0 };
            Track track(arraysize(ids));

            for (auto id : ids)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, id, (id == 7) ? 0 : 1));
                if (id == 7 || id == 9) sector.add(Data(sector.size(), i), true);
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Mirage (5*1024 + 1*512)
        {
            Track track(6);

            for (i = 0; i < 6; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, (i < 5) ? 3 : 2));
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Prophet 2000 (5*1024 + 1*256)
        {
            Track track(6);

            for (i = 0; i < 6; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, i, (i < 5) ? 3 : 1));
                if (i == 3 || i == 4) sector.add(Data(sector.size(), i), false, 0xf8);
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Lemmings (SAM)
        {
            Track track(6);

            for (i = 0; i < 6; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, (i == 0) ? 2 : 3));
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // Puffy's Saga (CPC)
        {
            Track track(4);

            for (i = 0; i < 4; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 65 + i, (i == 0) ? 2 : (i == 1) ? 3 : 4));
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // 32 x 128-byte sectors
        {
            Track track(32);

            for (i = 0; i < 32; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 0));
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

#if 1
        // Some HP disks described on cctalk
        {
            Track track(18);

            for (i = 0; i < 18; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, i, (i == 17) ? 0 : 1));
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }
#endif
        break;
    }

    // 1Mbps examples
    case 3:
    {
        disk->metadata["comment"] = "1Mbps examples";

        // Normal ED format
        {
            Track track(36);

            for (i = 0; i < 36; ++i)
            {
                Sector sector(DataRate::_1M, Encoding::MFM, Header(cylhead, 1 + i, 2));
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // 130 x 128-byte sectors
        {
            Track track(130);

            for (i = 0; i < 130; ++i)
            {
                Sector sector(DataRate::_1M, Encoding::MFM, Header(cylhead, 1 + i, 0));
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        // GPT (Siemens) exchange format
        {
            Track track(20);

            for (i = 0; i < 20; ++i)
            {
                Sector sector(DataRate::_1M, Encoding::MFM, Header(cylhead, i, 3));
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        break;
    }

    // 500Kbps MFM sector size test
    case 4 + 0:
    {
        disk->metadata["comment"] = "500Kbps MFM sector size test";

        // MFM sizes 0 to 6
        for (uint8_t size = 0; size < 7; ++size)
        {
            Track track(7 - size);

            for (i = 0; i < 7 - size; ++i)
            {
                Sector sector(DataRate::_500K, Encoding::MFM, Header(cylhead, 1 + i, size));
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        break;
    }

    // 500Kbps FM simple size test
    case 8 + 0:
    {
        disk->metadata["comment"] = "500Kbps FM sector size test";

        // FM sizes 0 to 5
        for (uint8_t size = 0; size < 6; ++size)
        {
            Track track(6 - size);

            for (i = 0; i < 6 - size; ++i)
            {
                Sector sector(DataRate::_500K, Encoding::FM, Header(cylhead, 1 + i, size));
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        break;
    }


    // 250Kbps MFM sector size test
    case 4 + 2:
    {
        disk->metadata["comment"] = "250Kbps MFM sector size test";

        // MFM sizes 0 to 5
        for (uint8_t size = 0; size < 6; ++size)
        {
            Track track(6 - size);

            for (i = 0; i < 6 - size; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, size));
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        break;
    }


    // 250Kbps FM sector size test
    case 8 + 2:
    {
        disk->metadata["comment"] = "250Kbps FM sector size test";

        // FM sizes 0 to 4
        for (uint8_t size = 0; size < 5; ++size)
        {
            Track track(5 - size);

            for (i = 0; i < 5 - size; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::FM, Header(cylhead, 1 + i, size));
                track.add(std::move(sector));
            }

            disk->write(cylhead.next_cyl(), std::move(complete(track)));
        }

        break;
    }

    // 500Kbps MFM bitstream
    case 16 + 0:
    {
        disk->metadata["comment"] = "500Kbps MFM bitstream";

        BitstreamTrackBuilder bitbuf(DataRate::_500K, Encoding::MFM);

        bitbuf.addTrackStart();
        for (i = 0; i < 18; i++)
        {
            const Data data(512, static_cast<uint8_t>(i));
            bitbuf.addSector(Header(cylhead, i + 1, 2), data, 0x54);
        }

        disk->write(cylhead.next_cyl(), std::move(bitbuf.buffer()));
        break;
    }

    // 250Kbps MFM bitstream
    case 16 + 2:
    {
        disk->metadata["comment"] = "250Kbps MFM bitstream";

        // Simple 9-sector format.
        {
            BitstreamTrackBuilder bitbuf(DataRate::_250K, Encoding::MFM);

            bitbuf.addTrackStart();
            for (i = 0; i < 9; i++)
            {
                const Data data(512, static_cast<uint8_t>(i));
                bitbuf.addSector(Header(cylhead, i + 1, 2), data, 0x54);
            }

            disk->write(cylhead.next_cyl(), std::move(bitbuf.buffer()));
        }

        // Empty track.
        {
            disk->write(GenerateEmptyTrack(cylhead.next_cyl(), Track()));
        }

        // KBI-19 format.
        {
            static constexpr uint8_t ids[]{ 0,1,4,7,10,13,16,2,5,8,11,14,17,3,6,9,12,15,18 };
            Track track(arraysize(ids));

            // Create a template that passes the IsKBI19Track check.
            for (auto& id : ids)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, id, 2));
                track.add(std::move(sector));
            }

            // Generate the full track from it.
            disk->write(GenerateKBI19Track(cylhead.next_cyl(), complete(track)));
        }

        // OperaSoft format with 32K sector.
        {
            Track track(9);

            // Create a template that passes the IsOperaSoftTrack check.
            for (i = 0; i < 9; ++i)
            {
                Header header(cylhead, i, (i == 8) ? 8 : 1);
                Sector sector(DataRate::_250K, Encoding::MFM, header);
                track.add(std::move(sector));
            }

            // Generate the full track from it.
            disk->write(GenerateOperaSoftTrack(cylhead.next_cyl(), complete(track)));
        }
        break;
    }

    // 250Kbps FM bitstream
    case 20 + 2:
    {
        disk->metadata["comment"] = "250Kbps FM bitstream";

        BitstreamTrackBuilder bitbuf(DataRate::_250K, Encoding::FM);

        bitbuf.addTrackStart();
        for (i = 0; i < 8; i++)
        {
            const Data data(256, static_cast<uint8_t>(i));
            bitbuf.addSector(Header(cylhead, i + 1, 1), data, 0x4e);
        }

        disk->write(cylhead.next_cyl(), std::move(bitbuf.buffer()));
        break;
    }

    // 500Kbps MFM flux
    case 24 + 0:
    {
        disk->metadata["comment"] = "500Kbps MFM flux";

        const Data data(512, 0x00);
        FluxTrackBuilder fluxbuf(cylhead, DataRate::_500K, Encoding::MFM);

        fluxbuf.addTrackStart();
        for (i = 0; i < 18; i++)
            fluxbuf.addSector(Header(cylhead, i + 1, 2), data, 0x54);

        disk->write(cylhead.next_cyl(), FluxData{ std::move(fluxbuf.buffer()) }, true);
        break;
    }

    // 250Kbps MFM flux
    case 24 + 2:
    {
        disk->metadata["comment"] = "250Kbps MFM flux";

        // Simple 9-sector format.
        {
            const Data data(512, 0x00);
            FluxTrackBuilder fluxbuf(cylhead, DataRate::_250K, Encoding::MFM);

            fluxbuf.addTrackStart();
            for (i = 0; i < 9; i++)
                fluxbuf.addSector(Header(cylhead, i + 1, 2), data, 0x54);

            disk->write(cylhead.next_cyl(), FluxData{ std::move(fluxbuf.buffer()) }, true);
        }

        // Spectrum +3 Speedlock weak sectors (full and part).
        {
            Track track(9);

            for (i = 0; i < 9; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 2));

                if (i >= 2 && i <= 7)
                    sector.add(Data(sector.size(), i), false, 0xf8);

                track.add(std::move(sector));
            }

            // Add Speedlock signature to first sector
            Data data0(512, 0);
            std::string sig{ "SPEEDLOCK" };
            std::copy(sig.begin(), sig.end(), data0.begin() + 304);
            track[0].add(Data(data0));

            complete(track);

            // Full weak sector.
            track[1].remove_data();
            Data weak_data(512);
            iota(weak_data, 0);
            track[1].add(Data(weak_data), true);
            auto trackdata = GenerateSpectrumSpeedlockTrack(cylhead.next_cyl(), track, 0, 512);
            disk->write(trackdata.cylhead, FluxData(trackdata.flux()), true);

            // Part weak sector.
            track[1].remove_data();
            fill(weak_data, 0, 256, 0xe5);
            iota(weak_data, 256, 1);
            fill(weak_data, 256 + 32, 256 + 32 + 48, 2);
            track[1].add(Data(weak_data), true);
            trackdata = GenerateSpectrumSpeedlockTrack(cylhead.next_cyl(), track, 336, 32);
            disk->write(trackdata.cylhead, FluxData(trackdata.flux()), true);
        }

        // Amstrad CPC Speedlock weak sectors (full, half, and part).
        {
            Track track(9);

            for (i = 0; i < 9; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 64 + i, 2));
                track.add(std::move(sector));
            }

            // Add Speedlock signature to first sector
            Data data0(512, 0);
            const std::string sig{ "SPEEDLOCK" };
            std::copy(sig.begin(), sig.end(), data0.begin() + 257);
            track[0].add(std::move(data0));

            complete(track);

            // Full weak sector.
            track[7].remove_data();
            Data weak_data(512);
            iota(weak_data, 0);
            track[7].add(Data(weak_data), true);
            auto trackdata = GenerateCpcSpeedlockTrack(cylhead.next_cyl(), track, 0, 512);
            disk->write(trackdata.cylhead, FluxData(trackdata.flux()), true);

            // Half weak sector.
            track[0].datas()[0][129] = 'S';
            track[7].remove_data();
            fill(weak_data, 0, 256, 0xe5);
            iota(weak_data, 256, 1);
            track[7].add(Data(weak_data), true);
            trackdata = GenerateCpcSpeedlockTrack(cylhead.next_cyl(), track, 256, 256);
            disk->write(trackdata.cylhead, FluxData(trackdata.flux()), true);

            // Part weak sector.
            track[0].datas()[0][129] = 0;
            track[7].remove_data();
            fill(weak_data, 0, 256, 0xe5);
            iota(weak_data, 256, 1);
            fill(weak_data, 256 + 32, 256 + 32 + 48, 2);
            track[7].add(Data(weak_data), true);
            trackdata = GenerateCpcSpeedlockTrack(cylhead.next_cyl(), track, 336, 32);
            disk->write(trackdata.cylhead, FluxData(trackdata.flux()), true);
        }

        // Rainbow Arts weak sector.
        {
            static constexpr uint8_t ids[]{ 193,198,194,109,195,200,196,201,197 };
            Track track(arraysize(ids));

            for (auto id : ids)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, id, 2));
                track.add(std::move(sector));
            }

            // Data error for weak sector.
            Data data1(512, 0);
            track[1].add(std::move(data1), true);

            // Add signature to 4th sector
            Data data3(512, 0);
            const std::string sig{ "\x2a\x6d\xa7\x01\x30\x01\xaf\xed\x42\x4d\x44\x21\x70\x01" };
            std::copy(sig.begin(), sig.end(), data3.begin());
            track[3].add(std::move(data3));

            disk->write(GenerateRainbowArtsTrack(cylhead.next_cyl(), complete(track), 100, 256));
        }

        // KBI-10 weak sector.
        {
            static constexpr uint8_t ids[]{ 193,198,194,199,195,200,196,201,197,202 };
            Track track(arraysize(ids));

            for (auto id : ids)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, id, (id == 202) ? 1 : 2));
                track.add(std::move(sector));
            }

            // Signature and data error for weak sector.
            Data data9(256, 0);
            const std::string sig{ "KBI" };
            std::copy(sig.begin(), sig.end(), data9.begin());
            track[9].add(std::move(data9), true);

            disk->write(GenerateKBIWeakSectorTrack(cylhead.next_cyl(), complete(track), 4, 4));
        }

        // Logo Professor (overformatted track).
        {
            Track track(10);
            track.tracklen = 6250 * 16;

            for (i = 0; i < 10; ++i)
            {
                Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 2 + i, 2));
                sector.offset = (TRACK_OVERHEAD_MFM + (SECTOR_OVERHEAD_MFM + 512 + 25) * (i + 1)) * 16;
                track.add(std::move(sector));
            }

            disk->write(GenerateLogoProfTrack(cylhead.next_cyl(), complete(track)));
        }

        // Sega System 24 (0x2f00 size)
        {
            static constexpr uint8_t sizes[]{ 4,4,4,4,4,3,1 };
            Track track(arraysize(sizes));

            for (i = 0; i < arraysize(sizes); ++i)
            {
                Sector sector(DataRate::_500K, Encoding::MFM, Header(cylhead, 1 + i, sizes[i]));
                track.add(std::move(sector));
            }

            disk->write(GenerateSystem24Track(cylhead.next_cyl(), complete(track)));
        }

        // 8K sector track.
        {
            Track track(1);

            Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 193, 6));
            sector.add(Data(sector.size(), i), true, 0xf8);
            track.add(std::move(sector));

            disk->write(Generate8KSectorTrack(cylhead.next_cyl(), complete(track)));
        }

        break;
    }

    // 250Kbps FM flux
    case 28 + 2:
    {
        disk->metadata["comment"] = "250Kbps FM flux";

        const Data data(256, 0x00);
        FluxTrackBuilder fluxbuf(cylhead, DataRate::_250K, Encoding::FM);

        fluxbuf.addTrackStart();
        for (i = 0; i < 8; i++)
            fluxbuf.addSector(Header(cylhead, i + 1, 1), data, 0x4e);

        disk->write(cylhead.next_cyl(), FluxData{ std::move(fluxbuf.buffer()) }, true);
        break;
    }

    // 500Kbps Amiga bitstream
    case 32 + 0:
    {
        disk->metadata["comment"] = "500Kbps Amiga bitstream";

        const Data data(512, 0x00);
        BitstreamTrackBuilder bitbuf(DataRate::_500K, Encoding::Amiga);

        bitbuf.addTrackStart();
        for (i = 0; i < 22; i++)
            bitbuf.addSector(Header(cylhead, i, 2), data);

        disk->write(cylhead.next_cyl(), std::move(bitbuf.buffer()));
        break;
    }

    // 250Kbps bitstreams
    case 32 + 2:
    {
        disk->metadata["comment"] = "250Kbps bitstreams";

        // AmigaDOS
        {
            BitstreamTrackBuilder bitbuf(DataRate::_250K, Encoding::Amiga);

            bitbuf.addTrackStart();
            for (i = 0; i < 11; i++)
            {
                const Data data(512, static_cast<uint8_t>(i));
                bitbuf.addSector(Header(cylhead, i, 2), data);
            }

            disk->write(cylhead.next_cyl(), std::move(bitbuf.buffer()));
        }

        // RX02
        {
            BitstreamTrackBuilder bitbuf(DataRate::_250K, Encoding::RX02);

            bitbuf.addTrackStart();
            for (i = 0; i < 26; i++)
            {
                const Data data(256, static_cast<uint8_t>(i));
                bitbuf.addSector(Header(cylhead, i + 1, 0), data, 0x38);
            }

            disk->write(cylhead.next_cyl(), std::move(bitbuf.buffer()));
        }

        break;
    }

    default:
        throw util::exception("unknown built-in type");
    }

    // Append a blank track
    disk->write(cylhead, Track());

    disk->strType = "<builtin>";
    return true;
}

bool WriteBuiltIn(const std::string& path, std::shared_ptr<Disk>&/*disk*/)
{
    if (!IsBuiltIn(path))
        return false;

    throw util::exception("built-in disks are read-only");
}
