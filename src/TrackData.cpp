#include "SAMdisk.h"
#include "TrackData.h"

#include "BitstreamDecoder.h"
#include "BitstreamEncoder.h"

TrackData::TrackData(const CylHead& cylhead_)
    : cylhead(cylhead_)
{
}

TrackData::TrackData(const CylHead& cylhead_, Track&& track)
    : cylhead(cylhead_), m_type(TrackDataType::Track)
{
    add(std::move(track));
}

TrackData::TrackData(const CylHead& cylhead_, BitBuffer&& bitstream)
    : cylhead(cylhead_), m_type(TrackDataType::BitStream)
{
    add(std::move(bitstream));
}

TrackData::TrackData(const CylHead& cylhead_, FluxData&& flux, bool normalised)
    : cylhead(cylhead_), m_type(TrackDataType::Flux)
{
    add(std::move(flux), normalised);
}


bool TrackData::has_track() const
{
    return (m_flags & TD_TRACK) != 0;
}

bool TrackData::has_bitstream() const
{
    return (m_flags & TD_BITSTREAM) != 0;
}

bool TrackData::has_flux() const
{
    return (m_flags & TD_FLUX) != 0;
}

bool TrackData::has_normalised_flux() const
{
    return has_flux() && m_normalised_flux;
}


const Track& TrackData::track()
{
    if (!has_track())
    {
        if (!has_bitstream())
            bitstream();

        if (has_bitstream())
        {
            scan_bitstream(*this);
            m_flags |= TD_TRACK;
        }
    }

    return m_track;
}

/*const*/ BitBuffer& TrackData::bitstream()
{
    if (!has_bitstream())
    {
        if (has_track())
            generate_bitstream(*this);
        else if (has_flux())
            scan_flux(*this);
        else
        {
            add(Track());
            generate_bitstream(*this);
        }

        m_flags |= TD_BITSTREAM;
    }

    return m_bitstream;
}

const FluxData& TrackData::flux()
{
    if (!has_flux())
    {
        if (!has_bitstream())
            bitstream();

        if (has_bitstream())
        {
            generate_flux(*this);
            m_flags |= TD_FLUX;
        }
    }

    return m_flux;
}

TrackData TrackData::preferred()
{
    switch (opt.prefer)
    {
    case PreferredData::Track:
        return { cylhead, Track(track()) };
    case PreferredData::Bitstream:
        return { cylhead, BitBuffer(bitstream()) };
    case PreferredData::Flux:
        return { cylhead, FluxData(flux()) };
    case PreferredData::Unknown:
        break;
    }

    auto trackdata = *this;
    if (trackdata.has_flux() && !trackdata.has_normalised_flux())
    {
        // Ensure there are track and bitstream representations, then clear
        // the unnormalised flux, as its use must be explicitly requested.
        trackdata.track();
        trackdata.m_flux.clear();
        trackdata.m_flags &= ~TD_FLUX;
    }
    return trackdata;
}


void TrackData::add(TrackData&& trackdata)
{
    if (trackdata.has_flux())
        add(FluxData(trackdata.flux()), trackdata.has_normalised_flux());

    if (trackdata.has_bitstream())
        add(BitBuffer(trackdata.bitstream()));

    if (trackdata.has_track())
        add(Track(trackdata.track()));
}

void TrackData::add(Track&& track)
{
    if (!has_track())
    {
        m_track = std::move(track);
        m_flags |= TD_TRACK;
    }
    else
    {
        // Add new data to existing
        m_track.add(std::move(track));
    }
}

void TrackData::add(BitBuffer&& bitstream)
{
    m_bitstream = std::move(bitstream);
    m_flags |= TD_BITSTREAM;
}

void TrackData::add(FluxData&& flux, bool normalised)
{
    m_normalised_flux = normalised;
    m_flux = std::move(flux);
    m_flags |= TD_FLUX;
}
