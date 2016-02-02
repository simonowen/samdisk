// Create command

#include "SAMdisk.h"

bool CreateImage (const std::string &path, Range range)
{
	auto disk = std::make_shared<Disk>();

	ValidateRange(range, NORMAL_TRACKS, NORMAL_SIDES);

	Format fmt;
	fmt.cyls = range.cyls();
	fmt.heads = range.heads();

	// Are we allowed to format?
	if (!opt.noformat)
	{
		// Set up the format for ProDos or MGT, with automatic gap3 size
		fmt = opt.cpm ? RegularFormat::ProDos : RegularFormat::MGT;
		fmt.gap3 = 0;

		// Prevent CP/M wrapping during image write
		opt.cpm = 0;

		// Set the disk label, if supplied
//		if (opt.label)
//			disk->strDiskLabel = opt.label;

		// To ensure it fits by default, halve the sector count in FM mode
		if (opt.fm == 1) fmt.sectors >>= 1;

		// Allow everything about the format to be overridden
		OverrideFormat(fmt, true);

		// Check sector count and size
		ValidateGeometry(1, 1, fmt.sectors, fmt.size, 7);

		disk->format(fmt);
	}

	// Write to the output disk image
	WriteImage(path, disk);

	auto cyls = disk->cyls();
	auto heads = disk->heads();

	// Report the new disk parameters
	if (opt.noformat)
		util::cout << util::fmt("Created %2u cyl%s, %u head%s, unformatted.\n", cyls, (cyls == 1) ? "" : "s", heads, (heads == 1) ? "" : "s");
	else
	{
		util::cout << util::fmt("Created %2u cyl%s, %u head%s, %2u sector%s/track, %4u bytes/sector\n",
								cyls, (cyls == 1) ? "" : "s", heads, (heads == 1) ? "" : "s",
								fmt.sectors, (fmt.sectors == 1) ? "" : "s", fmt.sector_size());
	}

	return true;
}

bool CreateHddImage (const std::string &path, int nSizeMB_)
{
	bool f = false;

	// If no sector count is specified, use the size parameter
	auto total_size = (opt.sectors == -1) ?
		static_cast<int64_t>(nSizeMB_) << 20 :
		static_cast<int64_t>(opt.sectors) << 9;

	if (total_size < 4 * 1024 * 1024)
		throw util::exception("needs image size in MB (>=4) or sector count with -s");

	// Create the specified HDD image, ensuring we don't overwrite any existing file
	auto hdd = HDD::CreateDisk(path, total_size, nullptr, false);
	if (!hdd)
		Error("create");
	else
	{
		// Zero-fill up to the required sector count
		f = hdd->Copy(nullptr, hdd->total_sectors, 0, 0, 0, "Creating");

		// If anything went wrong, remove the new file
		if (!f)
			unlink(path.c_str());
	}

	return f;
}
