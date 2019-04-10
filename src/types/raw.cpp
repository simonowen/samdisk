// Raw image files matched by file size alone

#include "SAMdisk.h"

bool ReadRAW (MemFile &file, std::shared_ptr<Disk> &disk)
{
	Format fmt;
	fmt.encoding = Encoding::MFM;

	// An empty format should not match an empty file!
	if (file.size() == 0)
		throw util::exception("image file is zero bytes");

	// Has the user customised any geometry parameters?
	bool customised = opt.range.cyls() || opt.range.heads() ||  opt.sectors > 0 || opt.size >= 0;

	// Attempt to match raw file size against a likely format.
	if (!Format::FromSize(file.size(), fmt) && !customised)
		return false;

	// Allow user overrides of the above format.
	auto orig_fmt = fmt;
	fmt.Override(true);

	// Ensure the intermediate geometry is complete.
	fmt.Validate();

	// If only cyls or heads is given, adjust the other one to match.
	if (fmt.cyls != orig_fmt.cyls && !opt.range.heads())
		fmt.heads = file.size() / (opt.range.cyls() * fmt.track_size());
	else if (fmt.heads != orig_fmt.heads && !opt.range.cyls())
		fmt.cyls = file.size() / (opt.range.heads() * fmt.track_size());

	// If only sector count or size are specified, adjust the other one to match.
	if (fmt.size != orig_fmt.size && opt.sectors < 0)
		fmt.sectors = file.size() / (fmt.cyls * fmt.heads * fmt.sector_size());
	else if (fmt.sectors != orig_fmt.sectors && opt.size < 0)
	{
		auto sector_size = file.size() / (fmt.cyls * fmt.heads * fmt.sectors);
		for(fmt.size = 0; sector_size > 128; sector_size /= 2)
			fmt.size++;
	}

	// Does the format now match the input file?
	if (fmt.disk_size() != file.size())
		throw util::exception("geometry doesn't match file size");

	// Ensure the final geometry is valid.
	fmt.Validate();

	// 720K images with a .cpm extension use the SAM Coupe Pro-Dos parameters
	if (file.size() == 737280 && IsFileExt(file.name(), "cpm"))
	{
		fmt = RegularFormat::ProDos;
		disk->strType = "ProDos";
	}
	// Warn if the size-to-format conversion is being used unmodified.
	// This makes it more obvious when an unsupported format is matched by size.
	else if (!customised)
	{
		Message(msgWarning, "input format guessed from file size -- please check");
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
	fmt.Override(true);

	// Write the image, as read using the supplied format
	WriteRegularDisk(f_, *disk, fmt);

	util::cout << util::fmt("Wrote %u cyl%s, %u head%s, %2u sector%s, %4u bytes/sector = %u bytes\n",
							fmt.cyls, (fmt.cyls == 1) ? "" : "s",
							fmt.heads, (fmt.heads == 1) ? "" : "s",
							fmt.sectors, (fmt.sectors == 1) ? "" : "s",
							fmt.sector_size(), fmt.disk_size());
	return true;
}
