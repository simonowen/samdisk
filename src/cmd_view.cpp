// View command

#include "SAMdisk.h"

void ViewTrack (const CylHead &cylhead, const Track &track)
{
	bool viewed = false;

	ScanContext context;
	ScanTrack(cylhead, track, context);
	if (!track.empty())
		util::cout << "\n";

	for (const auto &sector : track.sectors())
	{
		if (g_fAbort)
			return;

		// If a specific sector/size is required, skip non-matching ones
		if ((opt.sectors != -1 && (sector.header.sector != opt.sectors)) ||
			(opt.size >= 0 && (sector.header.size != opt.size)))
			continue;

		if (!sector.has_data())
			util::cout << "Sector " << sector.header.sector << " (no data field)\n\n";
		else
		{
			// Determine the data copy and number of bytes to show
			auto copy = std::min(sector.copies(), opt.datacopy);
			const Data &data = sector.data_copy(copy);
			auto data_size = static_cast<int>(data.size());
			auto show = (opt.bytes < 0) ? data_size : std::min(data_size, opt.bytes);

			if (data_size != show)
				util::cout << "Sector " << sector.header.sector << " (" << data_size << " bytes, " << show << " shown):\n";
			else if (data_size != sector.size())
				util::cout << "Sector " << sector.header.sector << " (" << sector.size() << " bytes, " << data_size << " stored):\n";
			else
				util::cout << "Sector " << sector.header.sector << " (" << data_size << " bytes):\n";

			if (show > 0)
			{
				if (sector.copies() == 1)
					util::hex_dump(data.begin(), data.begin() + show);
				else
				{
					std::vector<colour> colours;
					colours.reserve(sector.data_size());

					for (auto &diff : DiffSectorCopies(sector))
					{
						colour c;
						switch (diff.first)
						{
							default:
							case '=': c = colour::none;		break;
							case '-': c = colour::RED;		break;
							case '+': c = colour::YELLOW;	break;
						}

						std::vector<colour> fill(diff.second, c);
						colours.insert(colours.end(), fill.begin(), fill.end());
					}

					assert(static_cast<int>(colours.size()) == sector.data_size());
					util::hex_dump(data.begin(), data.begin() + show, colours.data());
				}
			}
			util::cout << "\n";
		}

		viewed = true;
	}

	// Single sector view but nothing matched?
	if (opt.sectors >= 0 && !viewed)
		util::cout << "Sector " << opt.sectors << " not found\n";

	if (!track.empty())
		util::cout << "\n";
}

bool ViewImage (const std::string &path, Range range)
{
	util::cout << "[" << path << "]\n";

	auto disk = std::make_shared<Disk>();
	if (ReadImage(path, disk))
	{
		ValidateRange(range, MAX_TRACKS, MAX_SIDES, disk->cyls(), disk->heads());

		range.each([&] (const CylHead &cylhead) {
			if (!g_fAbort)
			{
				auto track = disk->read_track(cylhead);
				NormaliseTrack(cylhead, track);
				ViewTrack(cylhead, track);
			}
		}, true);
	}

	return true;
}

bool ViewHdd (const std::string &path, Range range)
{
	auto hdd = HDD::OpenDisk(path);
	if (!hdd)
		Error("open");

	if (!range.empty() && (range.cyls() != 1 || range.heads() != 1))
		throw util::exception("HDD view ranges are not supported");

	MEMORY mem(hdd->sector_size);

	auto cyl = range.cyl_begin;
	auto head = range.head_begin;
	auto sector = (opt.sectors < 0) ? 0 : opt.sectors;
	auto lba_sector = sector;

	if (!range.empty())
	{
		if (cyl >= hdd->cyls || head >= hdd->heads || sector > hdd->sectors || !sector)
		{
			util::cout << util::fmt("Invalid CHS address for drive (Cyl 0-%d, Head 0-%u, Sector 1-%u)\n",
									hdd->cyls - 1, hdd->heads - 1, hdd->sectors);
			return false;
		}

		// Convert CHS address to LBA
		lba_sector = (cyl * hdd->heads + head) * hdd->sectors + (sector - 1);
	}

	// Determine the number of bytes to show
	auto uShow = mem.size;
	if (opt.bytes >= 0 && opt.bytes < uShow)
		uShow = opt.bytes;

	if (lba_sector >= hdd->total_sectors)
		util::cout << util::fmt("LBA value out of drive range (%u sectors).\n", hdd->total_sectors);
	else if (!hdd->Seek(lba_sector) || !hdd->Read(mem, 1))
		Error("read");
	else
	{
		if (!range.empty())
			util::cout << util::fmt("Cyl %s Head %s Sector %u (LBA %s):\n", CylStr(cyl), HeadStr(head), sector, lba_sector);
		else if (mem.size != uShow)
			util::cout << util::fmt("LBA Sector %u (%u bytes, %u shown):\n\n", lba_sector, mem.size, uShow);
		else
			util::cout << util::fmt("LBA Sector %u (%u bytes):\n\n", lba_sector, mem.size);

		util::hex_dump(mem.pb, mem.pb + uShow);
		return true;
	}

	return false;
}

bool ViewBoot (const std::string &path, Range range)
{
	// Strip ":0" from end of string
	std::string device = path.substr(0, path.find_last_of(":"));

	// Force boot sector
	opt.sectors = 0;

	return ViewHdd(device, range);
}
