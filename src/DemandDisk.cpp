// Demand-loaded disk tracks, for slow media

#include "SAMdisk.h"
#include "DemandDisk.h"
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
		Track track = load(cylhead);
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

void DemandDisk::unload (const CylHead &cylhead)
{
	auto it = m_tracks.find(cylhead);
	if (it != m_tracks.end())
		m_tracks.erase(it);

	m_loaded[cylhead] = false;
}
