// Demand-loaded disk tracks, for slow media

#include "SAMdisk.h"
#include "DemandDisk.h"

// Storage for class statics.
constexpr int DemandDisk::FIRST_READ_REVS;
constexpr int DemandDisk::REMAIN_READ_REVS;


void DemandDisk::extend(const CylHead& cylhead)
{
    // Access the track entry to pre-extend the disk ahead of loading it
    m_trackdata[cylhead].cylhead = cylhead;
}

bool DemandDisk::supports_retries() const
{
    // We only support full track rescans rather than individual sector retries.
    return false;
}

const TrackData& DemandDisk::read(const CylHead& cylhead, bool uncached)
{
    if (uncached || !m_loaded[cylhead])
    {
        // Quick first read, plus sector-based conversion
        auto trackdata = load(cylhead, true);
        auto& track = trackdata.track();

        // If the disk supports sector-level retries we won't duplicate them.
        auto retries = supports_retries() ? 0 : opt.retries;
        auto rescans = opt.rescans;

        // Consider rescans and error retries.
        while (rescans > 0 || retries > 0)
        {
            // If no more rescans are required, stop when there's nothing to fix.
            if (rescans <= 0 && track.has_good_data())
                break;

            auto rescan_trackdata = load(cylhead);
            auto& rescan_track = rescan_trackdata.track();

            // If the rescan found more sectors, use the new track data.
            if (rescan_track.size() > track.size())
                std::swap(trackdata, rescan_trackdata);

            // Flux reads include 5 revolutions, others just 1
            auto revs = trackdata.has_flux() ? REMAIN_READ_REVS : 1;
            rescans -= revs;
            retries -= revs;
        }

        std::lock_guard<std::mutex> lock(m_trackdata_mutex);
        m_trackdata[cylhead] = std::move(trackdata);
        m_loaded[cylhead] = true;
    }

    return Disk::read(cylhead);
}

void DemandDisk::save(TrackData&/*trackdata*/)
{
    throw util::exception("writing to this device is not currently supported");
}

const TrackData& DemandDisk::write(TrackData&& trackdata)
{
    save(trackdata);
    m_loaded[trackdata.cylhead] = true;
    return Disk::write(std::move(trackdata));
}

void DemandDisk::clear()
{
    Disk::clear();
    m_loaded.reset();
}
