// Demand-loaded disk tracks, for slow media

#include "SAMdisk.h"
#include "DemandDisk.h"

void DemandDisk::extend (const CylHead &cylhead)
{
	// Access the track entry to pre-extend the disk ahead of loading it
	m_trackdata[cylhead].cylhead = cylhead;
}

const Track &DemandDisk::read_track (const CylHead &cylhead)
{
	auto cylhead_step = CylHead(cylhead.cyl * opt.step, cylhead.head);

	if (!m_loaded[cylhead_step])
	{
		// Quick first read, plus sector-based conversion
		auto trackdata = load(cylhead_step, true);
		auto &track = trackdata.track();

		// Consider error retries
		for (auto attempt = (FIRST_READ_REVS - 1); attempt < opt.retries; )
		{
			// Stop if there's nothing to fix
			if (!track.has_data_error())
				break;

			// Read another track and merge with what we have so far
			trackdata.add(load(cylhead_step));

			// Flux reads include 5 revolutions, others just 1
			attempt += (trackdata.has_flux() ? REMAIN_READ_REVS : FIRST_READ_REVS) - 1;
		}

		std::lock_guard<std::mutex> lock(m_trackdata_mutex);
		m_trackdata[cylhead_step] = std::move(trackdata);
		m_loaded[cylhead_step] = true;
	}

	return Disk::read_track(cylhead);
}

void DemandDisk::unload ()
{
	Disk::unload();
	m_loaded.reset();
}
