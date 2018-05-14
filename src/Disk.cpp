// Core disk class

#include "SAMdisk.h"
#include "Disk.h"
#include "IBMPC.h"
#include "ThreadPool.h"

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


bool Disk::preload (const Range &range_, int cyl_step)
{
	// No pre-loading if multi-threading disabled, or only a single core
	if (!opt.mt || ThreadPool::get_thread_count() <= 1)
		return false;

	ThreadPool pool;
	std::vector<std::future<void>> rets;

	range_.each([&] (const CylHead cylhead) {
		rets.push_back(pool.enqueue([this, cylhead, cyl_step] () {
			read_track(cylhead * cyl_step);
		}));
	});

	for (auto &ret : rets)
		ret.get();

	return true;
}

void Disk::clear ()
{
	m_trackdata.clear();
}


const TrackData &Disk::read (const CylHead &cylhead, bool /*uncached*/)
{
	// Safe look-up requires mutex ownership, in case of call from preload()
	std::lock_guard<std::mutex> lock(m_trackdata_mutex);
	return m_trackdata[cylhead];
}

const Track &Disk::read_track (const CylHead &cylhead, bool uncached)
{
	read(cylhead, uncached);
	std::lock_guard<std::mutex> lock(m_trackdata_mutex);
	return m_trackdata[cylhead].track();
}

const BitBuffer &Disk::read_bitstream (const CylHead &cylhead, bool uncached)
{
	read(cylhead, uncached);
	std::lock_guard<std::mutex> lock(m_trackdata_mutex);
	return m_trackdata[cylhead].bitstream();
}

const FluxData &Disk::read_flux (const CylHead &cylhead, bool uncached)
{
	read(cylhead, uncached);
	std::lock_guard<std::mutex> lock(m_trackdata_mutex);
	return m_trackdata[cylhead].flux();
}


const TrackData &Disk::write (TrackData &&trackdata)
{
	// Invalidate stored format, since we can no longer guarantee a match
	fmt.sectors = 0;

	std::lock_guard<std::mutex> lock(m_trackdata_mutex);
	auto cylhead = trackdata.cylhead;
	m_trackdata[cylhead] = std::move(trackdata);
	return m_trackdata[cylhead];
}

const Track &Disk::write (const CylHead &cylhead, Track &&track)
{
	write(TrackData(cylhead, std::move(track)));
	return read_track(cylhead);
}

const BitBuffer &Disk::write(const CylHead &cylhead, BitBuffer &&bitbuf)
{
	write(TrackData(cylhead, std::move(bitbuf)));
	return read_bitstream(cylhead);
}

const FluxData &Disk::write(const CylHead &cylhead, FluxData &&flux_revs, bool normalised)
{
	write(TrackData(cylhead, std::move(flux_revs), normalised));
	return read_flux(cylhead);
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
		write(cylhead, std::move(track));
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
