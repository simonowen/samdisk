// Raw image files matched by file size alone

#include "SAMdisk.h"

bool ReadRAW (MemFile &file, std::shared_ptr<Disk> &disk)
{
	Format fmt;

	switch (file.size())
	{
		case 163840:	fmt = RegularFormat::PC720;  fmt.cyls = 40; fmt.heads = 1;	fmt.sectors = 8; break; // 5.25" SSSD (160K)
		case 184320:	fmt = RegularFormat::PC720;  fmt.cyls = 40; fmt.heads = 1;					  break; // 5.25" SSSD (180K)
		case 327680:	fmt = RegularFormat::PC720;  fmt.cyls = 40;					fmt.sectors = 8; break; // 5.25" DSDD (320K)
		case 368640:	fmt = RegularFormat::PC720;  fmt.cyls = 40;									  break; // 5.25" DSDD (360K)
		case 655360:	fmt = RegularFormat::PC720;									fmt.sectors = 8; break; // 3.5"  DSDD (640K)
		case 737280:	fmt = RegularFormat::PC720;													  break; // 3.5"  DSDD (720K)
		case 1228800:	fmt = RegularFormat::PC1440;								fmt.sectors = 15; break; // 5.25" DSHD (1200K)
		case 1474560:	fmt = RegularFormat::PC1440;												  break; // 3.5"  DSHD (1440K)
		case 1638400:	fmt = RegularFormat::PC1440;								fmt.sectors = 20; break; // 3.5"  DSHD (1600K)
		case 1720320:	fmt = RegularFormat::PC1440;								fmt.sectors = 21; break; // 3.5"  DSHD (1680K)
		case 1763328:	fmt = RegularFormat::PC1440; fmt.cyls = 82;					fmt.sectors = 21; break; // 3.5"  DSHD (1722K)
		case 1784832:	fmt = RegularFormat::PC1440; fmt.cyls = 83;					fmt.sectors = 21; break; // 3.5"  DSHD (1743K)
		case 1802240:	fmt = RegularFormat::PC1440;								fmt.sectors = 22; break; // 3.5"  DSHD (1760K)
		case 1884160:	fmt = RegularFormat::PC1440;								fmt.sectors = 23; break; // 3.5"  DSHD (1840K)
		case 1966080:	fmt = RegularFormat::PC1440;								fmt.sectors = 24; break; // 3.5"  DSHD (1920K)
		case 2949120:	fmt = RegularFormat::PC2880;												  break; // 3.5"  DSED (2880K)

		default:
			return false;
	}

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
