// Demand-loaded disk tracks, for slow media

#include "SAMdisk.h"
#include "DemandDisk.h"
#include "BitstreamDecoder.h"
#include "ThreadPool.h"

void DemandDisk::extend (const CylHead &cylhead)
{
	// Access the track entry to pre-extend the disk ahead of loading it
	m_tracks[cylhead];
}

const Track &DemandDisk::read_track (const CylHead &cylhead)
{
	if (!m_loaded[cylhead])
	{
		auto track = load(cylhead);
		std::lock_guard<std::mutex> lock(m_mutex);
		m_tracks[cylhead] = std::move(track);
		m_loaded[cylhead] = true;
	}

	return Disk::read_track(cylhead);
}

void DemandDisk::preload (const Range &range_)
{
	// No preloading if multi-threading disabled, or only a single core
	if (!opt.mt || ThreadPool::get_thread_count() <= 1)
		return;

	ThreadPool pool;
	std::vector<std::future<void>> rets;

	range_.each([&] (const CylHead cylhead) {
		if (!g_fAbort)
		{
			rets.push_back(pool.enqueue([this, cylhead] () {
				Track track = load(cylhead);

				std::lock_guard<std::mutex> lock(m_mutex);
				m_tracks[cylhead] = std::move(track);
				m_loaded[cylhead] = true;
			}));
		}
	});

	for (auto &ret : rets)
		ret.get();
}

Track DemandDisk::load (const CylHead &cylhead)
{
	auto cylhead_step = CylHead(cylhead.cyl * opt.step, cylhead.head);
	Track track;

	BitBuffer *bitstream;
	if (get_bitstream_source(cylhead_step, bitstream))
		track.add(scan_bitstream(cylhead, *bitstream));

	const std::vector<std::vector<uint32_t>> *flux_revs;
	if (get_flux_source(cylhead_step, flux_revs))
		track.add(scan_flux(cylhead, *flux_revs));

	return track;
}

void DemandDisk::unload (bool source_only)
{
	Disk::unload(source_only);

	if (source_only)
		m_loaded.set();
	else
		m_loaded.reset();
}
