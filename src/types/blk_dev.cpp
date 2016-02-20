// Simple block floppy device (USB and SCSI)

#include "SAMdisk.h"
#include "DemandDisk.h"
#include "BlockDevice.h"

class BlockFloppyDisk final : public DemandDisk
{
public:
	explicit BlockFloppyDisk (std::unique_ptr<BlockDevice> blockdev)
		: m_blockdev(std::move(blockdev))
	{
	}

protected:
	Track load (const CylHead &cylhead) override
	{
		auto lba = (cylhead.cyl * fmt.heads + cylhead.head) * fmt.sectors;
		std::vector<int> error_ids;

		if (!m_blockdev->Seek(lba))
			throw posix_error(errno, "seek");

		MEMORY mem(fmt.track_size());
		if (m_blockdev->Read(mem.pb, fmt.sectors) < fmt.sectors)
		{
			// Retry sectors individually
			for (auto i = 0; i < fmt.sectors; ++i)
			{
				auto p = mem.pb + i * fmt.sector_size();
				if (!m_blockdev->Seek(lba + i) || !m_blockdev->Read(p, 1))
				{
					Message(msgWarning, "error reading %s",
						CHR(cylhead.cyl, cylhead.head, fmt.base + i));
					error_ids.push_back(fmt.base + i);
				}
			}
		}

		Track track;
		track.format(cylhead, fmt);

		Data data(mem.pb, mem.pb + mem.size);
		track.populate(data.begin(), data.end());

		// Flag any errors
		for (auto id : error_ids)
		{
			for (auto &s : track)
				if (s.header.sector == id)
					s.set_baddatacrc();
		}

		return track;
	}

	void preload (const Range &/*range*/) override
	{
		// Pre-loading is not supported on real devices
	}

private:
	std::unique_ptr<HDD> m_blockdev;
};


bool ReadBlkDev (const std::string &path, std::shared_ptr<Disk> &disk)
{
	auto blockdev = std::make_unique<BlockDevice>();
	if (!blockdev->Open(path, true))
		return false;

	Format fmt;
	if (!SizeToFormat(blockdev->total_bytes, fmt))
		throw util::exception("not a floppy device");

	// Allow subsets of the track format
	if (opt.sectors > fmt.sectors)
		throw util::exception("sector count must be <= ", fmt.sectors);
	else if (opt.sectors > 0)
		fmt.sectors = opt.sectors;

	auto blk_dev_disk = std::make_shared<BlockFloppyDisk>(std::move(blockdev));
	blk_dev_disk->extend(CylHead(fmt.cyls - 1, fmt.heads - 1));
	blk_dev_disk->fmt = fmt;

	blk_dev_disk->strType = "Block Floppy Device";
	disk = blk_dev_disk;

	return true;
}
