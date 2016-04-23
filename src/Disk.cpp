// Core disk class

#include "SAMdisk.h"
#include "Disk.h"
#include "IBMPC.h"

// ToDo: split classes into separate files

std::string to_string (const DataRate &datarate)
{
	switch (datarate)
	{
		case DataRate::_250K:	return "250Kbps";		break;
		case DataRate::_300K:	return "300Kbps";		break;
		case DataRate::_500K:	return "500Kbps";		break;
		case DataRate::_1M:		return "1Mbps";			break;
		case DataRate::Unknown:	break;
	}
	return "Unknown";
}

std::string to_string (const Encoding &encoding)
{
	switch (encoding)
	{
		case Encoding::MFM:		return "MFM";			break;
		case Encoding::FM:		return "FM";			break;
		case Encoding::GCR:		return "GCR";			break;
		case Encoding::Amiga:	return "Amiga";			break;
		case Encoding::Ace:		return "Ace";			break;
		case Encoding::Unknown:	break;
	}
	return "Unknown";
}

//////////////////////////////////////////////////////////////////////////////

Disk::Disk (Format &fmt_)
	: fmt(fmt_)
{
	format(fmt);
}

Range Disk::range () const
{
	return Range(cyls(), heads());
}

int Disk::cyls () const
{
	return m_tracks.empty() ? 0 : (m_tracks.rbegin()->first.cyl + 1);
}

int Disk::heads () const
{
	if (m_tracks.empty())
		return 0;

	auto it = std::find_if(m_tracks.begin(), m_tracks.end(), [] (const std::pair<const CylHead, const Track> &p) {
		return p.first.head != 0;
	});

	return (it != m_tracks.end()) ? 2 : 1;
}


void Disk::unload (bool source_only)
{
	if (!source_only)
		m_tracks.clear();

	m_bitstreamdata.clear();
	m_fluxdata.clear();
}


bool Disk::get_bitstream_source (const CylHead &cylhead, BitBuffer* &p)
{
	auto it = m_bitstreamdata.find(cylhead);
	if (it != m_bitstreamdata.end())
	{
		p = &it->second;
		return true;
	}

	return false;
}

bool Disk::get_flux_source (const CylHead &cylhead, const std::vector<std::vector<uint32_t>>* &p)
{
	auto it = m_fluxdata.find(cylhead);
	if (it != m_fluxdata.end())
	{
		p = &it->second;
		return true;
	}

	return false;
}

void Disk::set_source (const CylHead &cylhead, BitBuffer &&bitbuf)
{
	m_bitstreamdata.emplace(std::make_pair(cylhead, std::move(bitbuf)));
	m_tracks[cylhead]; // extend
}

void Disk::set_source (const CylHead &cylhead, std::vector<std::vector<uint32_t>> &&data)
{
	m_fluxdata[cylhead] = std::move(data);
	m_tracks[cylhead]; // extend
}


const Track &Disk::read_track (const CylHead &cylhead)
{
	return m_tracks[cylhead];
}

Track &Disk::write_track (const CylHead &cylhead, const Track &track)
{
	// Move a temporary copy of the const source track
	return write_track(cylhead, Track(track));
}

Track &Disk::write_track (const CylHead &cylhead, Track &&track)
{
	// Invalidate stored format, since we can no longer guarantee a match
	fmt.sectors = 0;

	// Move supplied track into disk
	return m_tracks[cylhead] = std::move(track);
}

void Disk::each (const std::function<void (const CylHead &cylhead, const Track &track)> &func, bool cyls_first)
{
	if (!m_tracks.empty())
	{
		range().each([&] (const CylHead &cylhead) {
			func(cylhead, read_track(cylhead));
		}, cyls_first);
	}
}

void Disk::format (const RegularFormat &reg_fmt, const Data &data, bool cyls_first)
{
	format(Format(reg_fmt), data, cyls_first);
}

void Disk::format (const Format &new_fmt, const Data &data, bool cyls_first)
{
	auto it = data.begin(), itEnd = data.end();

	new_fmt.range().each([&] (const CylHead &cylhead) {
		Track track;
		track.format(cylhead, new_fmt);
		it = track.populate(it, itEnd);
		write_track(cylhead, std::move(track));
	}, cyls_first);

	// Assign format after formatting as it's cleared by formatting
	fmt = new_fmt;
}

void Disk::flip_sides ()
{
	decltype(m_tracks) tracks;

	for (auto pair : m_tracks)
	{
		CylHead cylhead = pair.first;
		cylhead.head ^= 1;

		// Move tracks to the new head position
		tracks[cylhead] = std::move(pair.second);
	}

	// Finally, swap the gutted container with the new one
	std::swap(tracks, m_tracks);
}

void Disk::resize (int new_cyls, int new_heads)
{
	if (!new_cyls && !new_heads)
	{
		m_tracks.clear();
		return;
	}

	// Remove tracks beyond the new extent
	for (auto it = m_tracks.begin(); it != m_tracks.end(); )
	{
		if (it->first.cyl >= new_cyls || it->first.head >= new_heads)
			it = m_tracks.erase(it);
		else
			++it;
	}

	// If the disk is too small, insert a blank track to extend it
	if (cyls() < new_cyls || heads() < new_heads)
		m_tracks[CylHead(new_cyls - 1, new_heads - 1)];
}

const Sector &Disk::get_sector (const Header &header)
{
	return read_track(header).get_sector(header);
}

bool Disk::find (const Header &header, const Sector *&found_sector)
{
	auto &track = read_track(header);
	auto it = track.find(header);
	if (it != track.end())
	{
		found_sector = &*it;
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////

std::string to_string (const Range &range)
{
	std::ostringstream ss;
	auto separator = ", ";

	if (range.empty())
		return "All Tracks";

	if (range.cyls() == 1)
		ss << "Cyl " << CylStr(range.cyl_begin);
	else if (range.cyl_begin == 0)
	{
		ss << std::setw(2) << range.cyl_end << " Cyls";
		separator = " ";
	}
	else
		ss << "Cyls " << CylStr(range.cyl_begin) << '-' << CylStr(range.cyl_end - 1);

	if (range.heads() == 1)
		ss << " Head " << range.head_begin;
	else if (range.head_begin == 0)
		ss << separator << range.head_end << " Heads";
	else
		ss << " Heads " << range.head_begin << '-' << (range.head_end - 1);

	return ss.str();
}


Range::Range (int num_cyls, int num_heads)
	: Range(0, num_cyls, 0, num_heads)
{
}

Range::Range (int cyl_begin_, int cyl_end_, int head_begin_, int head_end_)
	: cyl_begin(cyl_begin_), cyl_end(cyl_end_), head_begin(head_begin_), head_end(head_end_)
{
	assert(cyl_begin >= 0 && cyl_begin <= cyl_end);
	assert(head_begin >= 0 && head_begin <= head_end);
}

bool Range::empty () const
{
	return cyls() <= 0 || heads() <= 0;
}

int Range::cyls () const
{
	return cyl_end - cyl_begin;
}

int Range::heads () const
{
	return head_end - head_begin;
}

bool Range::contains (const CylHead &cylhead)
{
	return cylhead.cyl >= cyl_begin  && cylhead.cyl < cyl_end &&
		cylhead.head >= head_begin && cylhead.head < head_end;
}

void Range::each (const std::function<void (const CylHead &cylhead)> &func, bool cyls_first/*=false*/) const
{
	if (cyls_first && heads() > 1)
	{
		for (auto head = head_begin; head < head_end; ++head)
			for (auto cyl = cyl_begin; cyl < cyl_end; ++cyl)
				func(CylHead(cyl, head));
	}
	else
	{
		for (auto cyl = cyl_begin; cyl < cyl_end; ++cyl)
			for (auto head = head_begin; head < head_end; ++head)
				func(CylHead(cyl, head));
	}
}
