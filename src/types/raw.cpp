// Raw image files matched by file size alone

#include "SAMdisk.h"

bool ReadRAW (MemFile &file, std::shared_ptr<Disk> &disk)
{
	Format fmt;

	if (!SizeToFormat(file.size(), fmt))
		return false;

	assert(fmt.disk_size() == file.size());

	// 720K images with a .cpm extension use the SAM Coupe Pro-Dos parameters
	if (file.size() == 737280 && IsFileExt(file.name(), "cpm"))
	{
		fmt = RegularFormat::ProDos;
		disk->strType = "ProDos";
	}
	else
	{
		// To prevent unexpected behaviour, warn that this is a raw image file
		Message(msgWarning, "input file format guessed using only file size");
	}

	file.rewind();
	disk->format(fmt, file.data());
	disk->strType = "RAW";

	return true;
}

bool WriteRAW (FILE* f_, std::shared_ptr<Disk> &disk)
{
	int max_id = -1;

	auto range = opt.range;
	ValidateRange(range, disk->cyls(), disk->heads());

	Format fmt;
	fmt.cyls = 0;
	fmt.heads = 0;
	fmt.base = 0xff;

	disk->each([&] (const CylHead &cylhead, const Track &track) {
		// Skip empty tracks
		if (track.empty())
			return;

		// Track the used disk extent
		fmt.cyls = std::max(fmt.cyls, cylhead.cyl + 1);
		fmt.heads = std::max(fmt.heads, cylhead.head + 1);

		// Keep track of the largest sector count
		if (track.size() > fmt.sectors)
			fmt.sectors = static_cast<uint8_t>(track.size());

		// First track?
		if (fmt.datarate == DataRate::Unknown)
		{
			// Find a typical sector to use as a template
			ScanContext context;
			Sector typical = GetTypicalSector(cylhead, track, context.sector);

			fmt.datarate = typical.datarate;
			fmt.encoding = typical.encoding;
			fmt.size = typical.header.size;
		}

		for (auto &s : track.sectors())
		{
			// Track the lowest sector number
			if (s.header.sector < fmt.base)
				fmt.base = s.header.sector;

			// Track the highest sector number
			if (s.header.sector > max_id)
				max_id = s.header.sector;

			if (s.datarate != fmt.datarate)
				throw util::exception("mixed data rates are unsuitable for raw output");
			else if (s.encoding != fmt.encoding)
				throw util::exception("mixed data encodings are unsuitable for raw output");
			else if (s.header.size != fmt.size)
				throw util::exception("mixed sector sizes are unsuitable for raw output");
		}
	});

	if (fmt.datarate == DataRate::Unknown)
		throw util::exception("source disk is blank");
	else if (max_id < fmt.base || max_id >= fmt.base + fmt.sectors)
		throw util::exception("non-sequential sector numbers are unsuitable for raw output");

	// Allow user overrides for flexibility
	OverrideFormat(fmt, true);

	// Write the image, as read using the supplied format
	WriteRegularDisk(f_, *disk, fmt);

	util::cout << util::fmt("Wrote %u cyl%s, %u head%s, %2u sector%s, %4u bytes/sector = %u bytes\n",
							fmt.cyls, (fmt.cyls == 1) ? "" : "s",
							fmt.heads, (fmt.heads == 1) ? "" : "s",
							fmt.sectors, (fmt.sectors == 1) ? "" : "s",
							fmt.sector_size(), fmt.disk_size());
	return true;
}
