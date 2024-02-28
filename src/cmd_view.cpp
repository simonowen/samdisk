// View command

#include "SAMdisk.h"

void ViewTrack(const CylHead& cylhead, const Track& track)
{
    bool viewed = false;

    ScanContext context;
    ScanTrack(cylhead, track, context);
    if (!track.empty())
        util::cout << "\n";

    if (opt.verbose)
        return;

    for (const auto& sector : track.sectors())
    {
        // If a specific sector/size is required, skip non-matching ones
        if ((opt.sectors != -1 && (sector.header.sector != opt.sectors)) ||
            (opt.size >= 0 && (sector.header.size != opt.size)))
            continue;

        if (!sector.has_data())
            util::cout << "Sector " << sector.header.sector << " (no data field)\n\n";
        else
        {
            // Determine the data copy and number of bytes to show
            auto copy = std::min(sector.copies(), opt.datacopy);
            const Data& data = sector.data_copy(copy);
            auto data_size = data.size();

            auto show_begin = std::max(opt.bytes_begin, 0);
            auto show_end = (opt.bytes_end < 0) ? data_size :
                std::min(opt.bytes_end, data_size);

            if (data_size != sector.size())
                util::cout << "Sector " << sector.header.sector << " (" << sector.size() << " bytes, " << data_size << " stored):\n";
            else
                util::cout << "Sector " << sector.header.sector << " (" << data_size << " bytes):\n";

            if (show_end > show_begin)
            {
                if (sector.copies() == 1)
                    util::hex_dump(data.begin(), data.begin() + show_end, show_begin);
                else
                {
                    std::vector<colour> colours;
                    colours.reserve(sector.data_size());

                    for (auto& diff : DiffSectorCopies(sector))
                    {
                        colour c;
                        switch (diff.first)
                        {
                        default:
                        case '=': c = colour::none;     break;
                        case '-': c = colour::RED;      break;
                        case '+': c = colour::YELLOW;   break;
                        }

                        std::vector<colour> fill(diff.second, c);
                        colours.insert(colours.end(), fill.begin(), fill.end());
                    }

                    assert(static_cast<int>(colours.size()) == sector.data_size());
                    util::hex_dump(data.begin(), data.begin() + show_end,
                        show_begin, colours.data());
                }
            }
            util::cout << "\n";
        }

        viewed = true;
    }

    // Single sector view but nothing matched?
    if (opt.sectors >= 0 && !viewed)
        util::cout << "Sector " << opt.sectors << " not found\n";

    if (!track.empty())
        util::cout << "\n";
}

void ViewTrack_MFM_FM(Encoding encoding, BitBuffer& bitbuf)
{
    auto max_size = bitbuf.track_bitsize() * 110 / 100;

    Data track_data;
    std::vector<colour> colours;
    track_data.reserve(max_size);
    colours.reserve(max_size);

    uint32_t dword = 0;
    int bits = 0, a1 = 0, am_dist = 0xffff, data_size = 0;
    uint8_t am = 0;
    uint16_t sync_mask = opt.a1sync ? 0xffdf : 0xffff;

    bitbuf.seek(0);
    while (!bitbuf.wrapped())
    {
        dword = (dword << 1) | bitbuf.read1();
        ++bits;

        bool found_am = false;
        if (encoding == Encoding::MFM && (dword & sync_mask) == 0x4489)
        {
            found_am = true;
        }
        else if (encoding == Encoding::FM)
        {
            switch (dword)
            {
            case 0xaa222888:    // F8/C7 DDAM
            case 0xaa22288a:    // F9/C7 Alt-DDAM
            case 0xaa2228a8:    // FA/C7 Alt-DAM
            case 0xaa2228aa:    // FB/C7 DAM
            case 0xaa2a2a88:    // FC/D7 IAM
            case 0xaa222a8a:    // FD/C7 RX02 DAM
            case 0xaa222aa8:    // FE/C7 IDAM
                found_am = true;
                break;
            }
        }

        if (found_am || (bits == (encoding == Encoding::MFM ? 16 : 32)))
        {
            // Decode data byte.
            uint8_t b = 0;
            if (encoding == Encoding::MFM)
            {
                for (int i = 7; i >= 0; --i)
                    b |= static_cast<uint8_t>(((dword >> (i * 2)) & 1) << i);
            }
            else
            {
                for (int i = 7; i >= 0; --i)
                    b |= static_cast<uint8_t>(((dword >> (i * 4 + 1)) & 1) << i);
            }
            track_data.push_back(b);
            ++am_dist;

            if (encoding == Encoding::MFM && found_am)
            {
                // A1 sync byte (bright yellow if aligned to bitstream, dark yellow if not).
                colours.push_back((bits == 16) ? colour::YELLOW : colour::yellow);
                ++a1;
            }
            else
            {
                if (am == 0xfe && am_dist == 4)
                    data_size = Sector::SizeCodeToLength(b);

                if (a1 == 3)
                {
                    colours.push_back(colour::RED);
                    am = b;
                    am_dist = 0;
                }
                else if (encoding == Encoding::FM && found_am)
                {
                    colours.push_back((bits == 32) ? colour::RED : colour::red);
                    am = b;
                    am_dist = 0;
                }
                else if (am == 0xfe && am_dist >= 1 && am_dist <= 4)
                {
                    colours.push_back((am_dist == 3) ? colour::GREEN : colour::green);
                }
                else if (am == 0xfb && am_dist >= 1 && am_dist <= data_size)
                {
                    colours.push_back(colour::white);
                }
                else if ((am == 0xfe && am_dist > 4 && am_dist <= 6) ||
                    (am == 0xfb && am_dist > data_size&& am_dist <= (data_size + 2)))
                {
                    colours.push_back(colour::MAGENTA);
                }
                else
                {
                    colours.push_back(colour::grey);
                }

                a1 = 0;
            }

            bits = 0;
        }
    }

    auto show_begin = std::max(opt.bytes_begin, 0);
    auto show_end = (opt.bytes_end < 0) ? track_data.size() :
        std::min(opt.bytes_end, track_data.size());
    if (show_end > show_begin)
    {
        util::cout << encoding << " Decode (" << bitbuf.track_bitsize() << " bits):\n";
        util::hex_dump(track_data.begin(), track_data.begin() + show_end,
            show_begin, colours.data());
    }
}

void ViewTrack_Bitstream(BitBuffer& bitbuf)
{
    Data track_data;
    auto max_size = (bitbuf.track_bitsize() + 15) / 16;
    track_data.reserve(max_size);

    bitbuf.seek(opt.bitskip);
    while (!bitbuf.wrapped())
    {
        uint8_t b = 0, c = 0;
        for (int i = 0; i < 8; ++i)
        {
            c = (c << 1) | bitbuf.read1();
            b = (b << 1) | bitbuf.read1();
        }

        track_data.push_back(b);
    }

    auto show_begin = std::max(opt.bytes_begin, 0);
    auto show_end = (opt.bytes_end < 0) ? track_data.size() :
        std::min(opt.bytes_end, track_data.size());
    if (show_end > show_begin)
    {
        util::cout << "Bitstream Decode(" << (bitbuf.track_bitsize() - opt.bitskip) << " bits) :\n";
        util::hex_dump(track_data.begin(), track_data.begin() + show_end, show_begin);
    }
}

bool ViewImage(const std::string& path, Range range)
{
    util::cout << "[" << path << "]\n";

    auto disk = std::make_shared<Disk>();
    if (ReadImage(path, disk))
    {
        ValidateRange(range, MAX_TRACKS, MAX_SIDES, opt.step, disk->cyls(), disk->heads());

        range.each([&](const CylHead& cylhead) {
            auto track = disk->read_track(cylhead * opt.step);
            NormaliseTrack(cylhead, track);
            ViewTrack(cylhead, track);

            if (opt.verbose)
            {
                auto trackdata = disk->read(cylhead * opt.step);
                auto bitbuf = trackdata.preferred().bitstream();
                NormaliseBitstream(bitbuf);
                auto encoding = (opt.encoding == Encoding::Unknown) ?
                    bitbuf.encoding : opt.encoding;

                if (opt.bitskip >= 0)
                {
                    ViewTrack_Bitstream(bitbuf);
                }
                else
                {
                    switch (encoding)
                    {
                    case Encoding::MFM:
                    case Encoding::Amiga:
                    case Encoding::Agat:
                    case Encoding::MX:
                        ViewTrack_MFM_FM(Encoding::MFM, bitbuf);
                        break;
                    case Encoding::FM:
                    case Encoding::RX02:
                        ViewTrack_MFM_FM(Encoding::FM, bitbuf);
                        break;
                    default:
                        throw util::exception("unsupported track view encoding");
                    }
                }
            }
            }, true);
    }

    return true;
}

bool ViewHdd(const std::string& path, Range range)
{
    auto hdd = HDD::OpenDisk(path);
    if (!hdd)
        Error("open");

    if (!range.empty() && (range.cyls() != 1 || range.heads() != 1))
        throw util::exception("HDD view ranges are not supported");

    MEMORY mem(hdd->sector_size);

    auto cyl = range.cyl_begin;
    auto head = range.head_begin;
    auto sector = (opt.sectors < 0) ? 0 : opt.sectors;
    auto lba_sector = sector;

    if (!range.empty())
    {
        if (cyl >= hdd->cyls || head >= hdd->heads || sector > hdd->sectors || !sector)
        {
            util::cout << util::fmt("Invalid CHS address for drive (Cyl 0-%d, Head 0-%u, Sector 1-%u)\n",
                hdd->cyls - 1, hdd->heads - 1, hdd->sectors);
            return false;
        }

        // Convert CHS address to LBA
        lba_sector = (cyl * hdd->heads + head) * hdd->sectors + (sector - 1);
    }

    if (lba_sector >= hdd->total_sectors)
        util::cout << util::fmt("LBA value out of drive range (%u sectors).\n", hdd->total_sectors);
    else if (!hdd->Seek(lba_sector) || !hdd->Read(mem, 1))
        Error("read");
    else
    {
        if (!range.empty())
            util::cout << util::fmt("Cyl %s Head %s Sector %u (LBA %s):\n", CylStr(cyl), HeadStr(head), sector, lba_sector);
        else
            util::cout << util::fmt("LBA Sector %u (%u bytes):\n\n", lba_sector, mem.size);

        auto show_begin = std::max(opt.bytes_begin, 0);
        auto show_end = (opt.bytes_end < 0) ? mem.size :
            std::min(opt.bytes_end, mem.size);
        util::hex_dump(mem.pb, mem.pb + show_end, show_begin);
        return true;
    }

    return false;
}

bool ViewBoot(const std::string& path, Range range)
{
    // Strip ":0" from end of string
    std::string device = path.substr(0, path.find_last_of(":"));

    // Force boot sector
    opt.sectors = 0;

    return ViewHdd(device, range);
}
