// Encode tracks to a bitstream representation

#include "SAMdisk.h"
#include "BitstreamEncoder.h"
#include "BitstreamTrackBuilder.h"
#include "SpecialFormat.h"
#include "IBMPC.h"

bool generate_special(TrackData& trackdata)
{
    auto track{ trackdata.track() };
    int weak_offset{ 0 }, weak_size{ 0 };

    // Special formats have special conversions
    if (IsEmptyTrack(track))
        trackdata.add(GenerateEmptyTrack(trackdata.cylhead, track));
    else if (IsKBI19Track(track))
        trackdata.add(GenerateKBI19Track(trackdata.cylhead, track));
    else if (IsSystem24Track(track))
        trackdata.add(GenerateSystem24Track(trackdata.cylhead, track));
    else if (IsSpectrumSpeedlockTrack(track, weak_offset, weak_size))
        trackdata.add(GenerateSpectrumSpeedlockTrack(trackdata.cylhead, track, weak_offset, weak_size));
    else if (IsCpcSpeedlockTrack(track, weak_offset, weak_size))
        trackdata.add(GenerateCpcSpeedlockTrack(trackdata.cylhead, track, weak_offset, weak_size));
    else if (IsRainbowArtsTrack(track, weak_offset, weak_size))
        trackdata.add(GenerateRainbowArtsTrack(trackdata.cylhead, track, weak_offset, weak_size));
    else if (IsKBIWeakSectorTrack(track, weak_offset, weak_size))
        trackdata.add(GenerateKBIWeakSectorTrack(trackdata.cylhead, track, weak_offset, weak_size));
    else if (IsLogoProfTrack(track))
        trackdata.add(GenerateLogoProfTrack(trackdata.cylhead, track));
    else if (IsOperaSoftTrack(track))
        trackdata.add(GenerateOperaSoftTrack(trackdata.cylhead, track));
    else if (Is8KSectorTrack(track))
        trackdata.add(Generate8KSectorTrack(trackdata.cylhead, track));
    else if (Is11SectorTrack(track))
        trackdata.add(Generate11SectorTrack(trackdata.cylhead, track));
    else
        return false;

    return true;
}

bool generate_simple(TrackData& trackdata)
{
    bool first_sector = true;
    auto& track = trackdata.track();
    BitstreamTrackBuilder bitbuf(track[0].datarate, track[0].encoding);

    FitDetails fit_details{};
    bool fits_ibmpc = FitTrackIBMPC(trackdata.cylhead, track, 200'000, fit_details);

    for (auto& s : track)
    {
        // Prefer option gap, then IBM fit, then sector gap, with 25-byte fallback.
        // For IBM fit use the fit gap3 only if the sectors count matches, implying
        // a simple fitting case. When space is tight on a real PC FDC we need to
        // create large sectors from multiple smaller sectors. We don't have that
        // limitation with bitstream encoding, so just join all sectors together
        // with a smaller gap3 (currently 10 bytes) to make it fit.
        int gap3 = (opt.gap3 > 0) ? opt.gap3 :
            fits_ibmpc ?
            ((fit_details.total_units == track.size()) ? fit_details.gap3 : 10) :
            s.gap3 ? s.gap3 :
            25;

        bitbuf.setEncoding(s.encoding);

        switch (s.encoding)
        {
        case Encoding::MFM:
        case Encoding::FM:
        case Encoding::Amiga:
        case Encoding::RX02:
            if (first_sector)
                bitbuf.addTrackStart();

            if (s.has_badidcrc())
            {
                bitbuf.addSectorHeader(s.header, true);
            }
            else if (s.has_baddatacrc() && fits_ibmpc && !fit_details.real_errors)
            {
                bitbuf.addSectorUpToData(s.header, s.dam);

                auto fake_size = Sector::SizeCodeToLength(fit_details.size_code);
                auto data = s.data_copy();
                if (data.size() > fake_size)
                    data.resize(fake_size);
                bitbuf.addBlock(data);
            }
            else
            {
                bitbuf.addSector(s, gap3);
            }
            break;
        default:
            throw util::exception("bitstream conversion not yet available for ", s.encoding, " sectors");
        }

        first_sector = false;
    }

    auto track_time_ns = bitbuf.size() * bitcell_ns(bitbuf.datarate());
    auto track_time_ms = track_time_ns / 1'000'000;

    // ToDo: caller should supply size limit
    if (track_time_ms > 205 && !opt.force)
        throw util::exception("generated bitstream is too big for ", trackdata.cylhead);

    trackdata.add(std::move(bitbuf.buffer()));
    return true;
}

void generate_bitstream(TrackData& trackdata)
{
    assert(trackdata.has_track());

    // Special formats have special conversions (unless disabled)
    if (!opt.nospecial && generate_special(trackdata))
    {
        // Fail if we've encountered a flux-only special format, as converting
        // it to bitstream is unlikely to give a working track.
        if (!trackdata.has_bitstream())
            throw util::exception("no suitable bitstream representation for ", trackdata.cylhead);
    }
    else if (opt.nottb)
        throw util::exception("track to bitstream conversion not permitted for ", trackdata.cylhead);
    else if (!generate_simple(trackdata))
        throw util::exception("bitstream conversion not yet implemented for ", trackdata.cylhead);
}

void generate_flux(TrackData& trackdata)
{
    uint8_t last_bit{ 0 }, curr_bit{ 0 };
    auto& bitbuf = trackdata.bitstream();
    auto ns_per_bitcell = bitcell_ns(bitbuf.datarate);
    bitbuf.seek(0);

    uint32_t flux_time{ 0 };
    FluxData flux_data{};
    std::vector<uint32_t> flux_times{};
    flux_times.reserve(bitbuf.size());

    while (!bitbuf.wrapped())
    {
        auto next_bit{ bitbuf.read1() };

        flux_time += ns_per_bitcell;
        if (curr_bit)
        {
            if (trackdata.cylhead.cyl < 40)
            {
                flux_times.push_back(flux_time);
                flux_time = 0;
            }
            else
            {
                auto pre_comp_ns{ (last_bit == next_bit) ? 0 : (last_bit ? +240 : -240) };
                flux_times.push_back(flux_time + pre_comp_ns);
                flux_time = 0 - pre_comp_ns;
            }
        }

        last_bit = curr_bit;
        curr_bit = next_bit;

        if (bitbuf.index())
        {
            flux_data.push_back(std::move(flux_times));
            flux_times.clear();
        }
    }

    if (flux_data.empty() || !flux_times.empty())
        flux_data.push_back(std::move(flux_times));

    trackdata.add(std::move(flux_data), true);
}
