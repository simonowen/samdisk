#include "SAMdisk.h"
#include "TrackData.h"

#include "BitstreamDecoder.h"

TrackData::TrackData (const CylHead &cylhead_)
	: cylhead(cylhead_)
{
}

TrackData::TrackData (const CylHead &cylhead_, Track &&track)
	: cylhead(cylhead_), m_type(TrackDataType::Track)
{
	add(std::move(track));
}

TrackData::TrackData (const CylHead &cylhead_, BitBuffer &&bitstream)
	: cylhead(cylhead_), m_type(TrackDataType::BitStream)
{
	add(std::move(bitstream));
}

TrackData::TrackData (const CylHead &cylhead_, FluxData &&flux)
	: cylhead(cylhead_), m_type(TrackDataType::Flux)
{
	add(std::move(flux));
}


bool TrackData::has_track () const
{
	return (m_flags & TD_TRACK) != 0;
}

bool TrackData::has_bitstream () const
{
	return (m_flags & TD_BITSTREAM) != 0;
}

bool TrackData::has_flux () const
{
	return (m_flags & TD_FLUX) != 0;
}


const Track &TrackData::track ()
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

/*const*/ BitBuffer &TrackData::bitstream ()
{
	if (!has_bitstream())
	{
		if (has_track())
			generate_bitstream(*this);
		else if (has_flux())
			scan_flux(*this);

		if (has_bitstream())
			m_flags |= TD_BITSTREAM;
	}

	return m_bitstream;
}

const FluxData &TrackData::flux ()
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


void TrackData::add (TrackData &&trackdata)
{
	if (!has_track())
		track();

	m_track.add(std::move(trackdata.m_track));
}

void TrackData::add (Track &&track)
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

void TrackData::add (BitBuffer &&bitstream)
{
	m_bitstream = std::move(bitstream);
	m_flags |= TD_BITSTREAM;
}

void TrackData::add (FluxData &&flux)
{
	m_flux = std::move(flux);
	m_flags |= TD_FLUX;
}

TrackData &TrackData::keep_track ()
{
	m_bitstream.clear();
	m_flux.clear();
	m_flags &= TD_TRACK;
	return *this;
}

TrackData &TrackData::keep_bitstream()
{
	m_track.clear();
	m_flux.clear();
	m_flags &= TD_BITSTREAM;
	return *this;
}

TrackData &TrackData::keep_flux()
{
	m_track.clear();
	m_bitstream.clear();
	m_flags &= TD_FLUX;
	return *this;
}
