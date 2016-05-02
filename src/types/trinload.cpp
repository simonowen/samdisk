// Requires Quazar Trinity ethernet interface
//  http://www.samcoupe.com/hardtrin.htm
//
// Also TrinLoad network loader for the SAM Coupe:
//  http://simonowen.com/blog/2015/03/05/trinload-10/

#include "SAMdisk.h"
#include "Trinity.h"
#include "DemandDisk.h"

class TrinLoadDisk final : public DemandDisk
{
public:
	explicit TrinLoadDisk (std::unique_ptr<Trinity> trinity)
		: m_trinity(std::move(trinity))
	{
	}

protected:
	TrackData load (const CylHead &cylhead, bool /*first_read*/) override
	{
		auto data = m_trinity->read_track(cylhead.cyl, cylhead.head);

		Track track;
		track.format(cylhead, RegularFormat::MGT);
		track.populate(data.begin(), data.end());
		return TrackData(cylhead, std::move(track));
	}

	bool preload (const Range &/*range*/) override
	{
		return false;
	}

private:
	std::unique_ptr<Trinity> m_trinity;
};


bool ReadTrinLoad (const std::string &path, std::shared_ptr<Disk> &disk)
{
	if (util::lowercase(path).substr(0, 4) != "sam:")
		return false;

	auto trinity = Trinity::Open();

	auto record = strtoul(path.c_str() + 4, nullptr, 10);
	if (record != 0)
		trinity->select_record(record);

	auto ip_addr_str = trinity->devices()[0];
	auto trinload_disk = std::make_shared<TrinLoadDisk>(std::move(trinity));

	trinload_disk->fmt = Format(RegularFormat::MGT);
	trinload_disk->extend(CylHead(trinload_disk->fmt.cyls - 1, trinload_disk->fmt.heads - 1));
	trinload_disk->metadata["address"] = ip_addr_str;
	trinload_disk->strType = "TrinLoad";
	disk = trinload_disk;

	return true;
}

#if 0
bool WriteTrinLoad (const std::string &path, std::shared_ptr<Disk> &disk)
{
	throw util::exception("not implemented");
}
#endif
