// Core disk class

#include "SAMdisk.h"
#include "Disk.h"
#include "IBMPC.h"
#include "ThreadPool.h"

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
	return m_trackdata.empty() ? 0 : (m_trackdata.rbegin()->first.cyl + 1);
}

int Disk::heads () const
{
	if (m_trackdata.empty())
		return 0;

	auto it = std::find_if(m_trackdata.begin(), m_trackdata.end(), [] (const std::pair<const CylHead, const TrackData> &p) {
		return p.first.head != 0;
	});

	return (it != m_trackdata.end()) ? 2 : 1;
}


bool Disk::preload (const Range &range_)
{
	// No pre-loading if multi-threading disabled, or only a single core
	if (!opt.mt || ThreadPool::get_thread_count() <= 1)
		return false;

	ThreadPool pool;
	std::vector<std::future<void>> rets;

	range_.each([&] (const CylHead cylhead) {
		if (!g_fAbort)
		{
			rets.push_back(pool.enqueue([this, cylhead] () {
				read_track(cylhead);
			}));
		}
	});

	for (auto &ret : rets)
		ret.get();

	return true;
}

void Disk::unload ()
{
	m_trackdata.clear();
}


void Disk::add (TrackData &&trackdata)
{
	m_trackdata[trackdata.cylhead] = std::move(trackdata);
}

const Track &Disk::read_track (const CylHead &cylhead)
{
	auto cylhead_step = CylHead(cylhead.cyl * opt.step, cylhead.head);

	// Safe look-up requires mutex ownership, in case of call from preload()
	m_trackdata_mutex.lock();
	auto &trackdata = m_trackdata[cylhead_step];
	m_trackdata_mutex.unlock();

	// Fetch the track outside the lock in case conversion is needed
	return trackdata.track();
}

const Track &Disk::write_track (const CylHead &cylhead, const Track &track)
{
	// Move a temporary copy of the const source track
	return write_track(cylhead, Track(track));
}

const Track &Disk::write_track (const CylHead &cylhead, Track &&track)
{
	// Invalidate stored format, since we can no longer guarantee a match
	fmt.sectors = 0;

	// Move supplied track into disk
	m_trackdata[cylhead] = TrackData(cylhead, std::move(track));
	return m_trackdata[cylhead].track();
}

void Disk::each (const std::function<void (const CylHead &cylhead, const Track &track)> &func, bool cyls_first)
{
	if (!m_trackdata.empty())
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
	decltype(m_trackdata) trackdata;

	for (auto pair : m_trackdata)
	{
		CylHead cylhead = pair.first;
		cylhead.head ^= 1;

		// Move tracks to the new head position
		trackdata[cylhead] = std::move(pair.second);
	}

	// Finally, swap the gutted container with the new one
	std::swap(trackdata, m_trackdata);
}

void Disk::resize (int new_cyls, int new_heads)
{
	if (!new_cyls && !new_heads)
	{
		m_trackdata.clear();
		return;
	}

	// Remove tracks beyond the new extent
	for (auto it = m_trackdata.begin(); it != m_trackdata.end(); )
	{
		if (it->first.cyl >= new_cyls || it->first.head >= new_heads)
			it = m_trackdata.erase(it);
		else
			++it;
	}

	// If the disk is too small, insert a blank track to extend it
	if (cyls() < new_cyls || heads() < new_heads)
		m_trackdata[CylHead(new_cyls - 1, new_heads - 1)];
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
