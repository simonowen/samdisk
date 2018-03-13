// Demand-loaded disk tracks, for slow media

#include "SAMdisk.h"
#include "DemandDisk.h"

// Storage for class statics.
constexpr int DemandDisk::FIRST_READ_REVS;
constexpr int DemandDisk::REMAIN_READ_REVS;


void DemandDisk::extend (const CylHead &cylhead)
{
	// Access the track entry to pre-extend the disk ahead of loading it
	m_trackdata[cylhead].cylhead = cylhead;
}

const TrackData &DemandDisk::read (const CylHead &cylhead)
{
	if (!m_loaded[cylhead])
	{
		// Quick first read, plus sector-based conversion
		auto trackdata = load(cylhead, true);
		auto &track = trackdata.track();

		// Consider error retries
		for (auto attempt = 1; attempt < opt.rescans; )
		{
			// Stop if there's nothing to fix
			if (!track.has_data_error())
				break;

			// Read another track and merge with what we have so far
			trackdata.add(load(cylhead));

			// Flux reads include 5 revolutions, others just 1
			attempt += (trackdata.has_flux() ? REMAIN_READ_REVS : FIRST_READ_REVS) - 1;
		}

		std::lock_guard<std::mutex> lock(m_trackdata_mutex);
		m_trackdata[cylhead] = std::move(trackdata);
		m_loaded[cylhead] = true;
	}

	return Disk::read(cylhead);
}

const TrackData &DemandDisk::write (TrackData &&/*trackdata*/)
{
#if 1
	throw util::exception("writing not currently supported");
#else
	//save(trackdata);
	m_loaded[trackdata.cylhead] = true;
	return Disk::write(std::move(trackdata));
#endif
}

void DemandDisk::unload ()
{
	Disk::unload();
	m_loaded.reset();
}
