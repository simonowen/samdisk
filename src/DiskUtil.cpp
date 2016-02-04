// Disk class utilities

#include "SAMdisk.h"
#include "DiskUtil.h"

static const int MIN_DIFF_BLOCK = 16;


static void item_separator (int items)
{
	if (!items)
		util::cout << colour::grey << '[' << colour::none;
	else
		util::cout << ',';
}

void DumpTrack (const CylHead &cylhead, const Track &track, const ScanContext &context, int flags)
{
	if (opt.hex != 1)
		util::cout << util::fmt(" %2u.%u  ", cylhead.cyl, cylhead.head);
	else
		util::cout << util::fmt(" %s.%u  ", CylStr(cylhead.cyl), cylhead.head);

	if (track.empty())
		util::cout << colour::grey << "<blank>" << colour::none;
	else
	{
		for (auto &sector : track.sectors())
		{
			util::cout << RecordStr(sector.header.sector);

			// Use 'd' suffix for deleted sectors, or 'a' for alt-DAM
			if (sector.is_deleted())
				util::cout << colour::green << "d" << colour::none;
			else if (sector.is_altdam())
				util::cout << colour::YELLOW << "a" << colour::none;
			else if (sector.is_rx02dam())
				util::cout << colour::YELLOW << "x" << colour::none;

			auto items = 0;

			if (sector.header.cyl != context.sector.header.cyl)
			{
				item_separator(items++);
				util::cout << colour::yellow << "c" << std::string(CylStr(sector.header.cyl)) << colour::none;
			}

			if (sector.header.head != context.sector.header.head)
			{
				item_separator(items++);
				util::cout << colour::yellow << "h" << std::string(HeadStr(sector.header.head)) << colour::none;
			}

			if (sector.header.size != context.sector.header.size)
			{
				item_separator(items++);
				util::cout << colour::yellow << "n" << std::string(SizeStr(sector.header.size)) << colour::none;
			}

			if (sector.copies() > 1)
			{
				item_separator(items++);
				util::cout << "m" << sector.copies();
			}

			if (sector.has_badidcrc())
			{
				item_separator(items++);
				util::cout << colour::RED << "ic" << colour::none;	// ID header CRC error
			}

			if (!sector.has_badidcrc() && !sector.has_data())		// No data field
			{
				item_separator(items++);
				util::cout << colour::YELLOW << "nd" << colour::none;
			}

			if (sector.has_baddatacrc())
			{
				item_separator(items++);
				util::cout << colour::RED << "dc" << colour::none;	// Data field CRC error
			}

			if (track.is_repeated(sector))
			{
				item_separator(items++);
				util::cout << colour::YELLOW << "r" << colour::none;	// Repeated sector
			}

			if (sector.has_data() && sector.data_size() == 0)
			{

				item_separator(items++);
				util::cout << colour::YELLOW << "z" << colour::none;	// Zero data stored
			}
			else if (sector.has_data() && sector.has_shortdata() && !sector.has_baddatacrc())
			{
				item_separator(items++);
				util::cout << colour::RED << "-" << (sector.size() - sector.data_size()) << colour::none;	// Less data than sector size
			}

			if (sector.encoding == Encoding::FM && context.sector.encoding == Encoding::MFM)
			{
				item_separator(items++);
				util::cout << colour::CYAN << "fm" << colour::none;	// FM sector on MFM track
			}

			if (sector.has_gapdata())
			{
				item_separator(items++);
				util::cout << colour::CYAN << "+" << (sector.data_size() - sector.size()) << colour::none;	// More data than sector size (crc, gaps, ...)
			}

			if (items)
				util::cout << colour::grey << ']' << colour::none;

			util::cout << ' ';
		}
		//		p--;

		// If we've got a track length and sector data, show the sector offsets too
		if ((flags & DUMP_OFFSETS) && !track.empty() && track.tracklen)
		{
			auto prevoffset = 0;
			auto shift = (context.sector.encoding == Encoding::FM) ? 5 : 4;

			util::cout << util::fmt("\n         %s: ", WordStr(track.tracklen >> shift));

			for (const auto &sector : track.sectors())
			{
				auto offset = sector.offset;

				// Invalid or missing offset?
				if (offset < prevoffset)
				{
					// Show a red question mark instead of the offset
					util::cout << colour::RED << '?' << colour::none;
				}
				else
				{
					// Show offset from previous sector
					if (offset > prevoffset)
						util::cout << util::fmt("%s ", WordStr((offset - prevoffset) >> shift));
					else
						util::cout << util::fmt("-%s ", WordStr((prevoffset - offset) >> shift));

					if (!opt.absoffsets)
						prevoffset = offset;
				}
			}

			// Show the gap until end of track
			auto last_offset = track.sectors()[track.size() - 1].offset;
			if (track.tracklen > last_offset)
				util::cout << util::fmt("[%s]", WordStr((track.tracklen - last_offset) >> shift));
			else
				util::cout << util::fmt("[-%s]", WordStr((last_offset - track.tracklen) >> shift));
		}
	}

	util::cout << "\n";

	if (flags & DUMP_DIFF)
	{
		for (const auto &sector : track.sectors())
		{
			// If we have more than 1 copy, compare them
			if (sector.copies() > 1)
			{
				util::cout << "        diff (" << RecordStr(sector.header.sector) << "): ";

				auto i = 0;
				for (auto &diff : DiffSectorCopies(sector))
				{
					util::cout << diff.first << diff.second << ' ';
					if (++i > 12)
					{
						util::cout << "...";
						break;
					}
				}
				util::cout << '\n';
			}
		}
	}
}


void OverrideFormat (Format &fmt, bool full_control/*=false*/)
{
	static const DataRate rates[] = { DataRate::_500K, DataRate::_300K, DataRate::_250K, DataRate::_1M };

	if (full_control)
	{
		if (opt.sectors != -1) fmt.sectors = opt.sectors;
		if (opt.size >= 0 && opt.size <= 7) fmt.size = opt.size;
	}

	// Merge any overrides from the command-line
	if (opt.fm == 1) fmt.encoding = Encoding::FM;
	if (opt.fill >= 0) fmt.fill = static_cast<uint8_t>(opt.fill);
	if (opt.gap3 >= 0) fmt.gap3 = opt.gap3;
	if (opt.base != -1) fmt.base = opt.base;
	if (opt.interleave >= 0) fmt.interleave = opt.interleave;
	if (opt.skew >= 0) fmt.skew = opt.skew;
	if (opt.head0 != -1) fmt.head0 = opt.head0;
	if (opt.head1 != -1) fmt.head1 = opt.head1;
	if (opt.rate != -1) fmt.datarate = rates[opt.rate & 3];	// ToDo: remove this?

/*
	// ToDo: auto-adjust density to match sector content?
	// Check the gap size required for the current settings
	uint8_t gap3 = GetFormatGap(RPM_TIME_300, fmt.datarate, fmt.encoding, fmt.sectors, fmt.size);

	// Does it fit?
	if (!gap3)
	{
	// If not, try again with high density
	pf_->encrate = (pf_->encrate & ~FD_RATE_MASK) | FD_RATE_500K;
	gap3 = GetFormatGap(RPM_TIME_300, pf_->encrate, pf_->sectors, pf_->size);

	// If that still doesn't fit, assume extra density (we'll check properly later)
	if (!gap3)
	pf_->encrate = (pf_->encrate & ~FD_RATE_MASK) | FD_RATE_1M;
	}
*/
}


std::vector<std::pair<char, size_t>> DiffSectorCopies (const Sector &sector)
{
	assert(sector.copies() > 0);
	std::vector<std::pair<char, size_t>> diffs;

	auto &smallest = sector.data_copy(sector.copies() - 1);
	auto it = smallest.begin();
	auto itEnd = smallest.end();

	//	int extent = pt_->GetDataExtent(nSector_);	// ToDo
	auto change_count = 0;

	auto diff = 0;
	while (it < itEnd)
	{
		auto offset = std::distance(smallest.begin(), it);
		auto same = std::distance(it, itEnd);

		for (const auto &data : sector.datas())
		{
			if (same)
			{
				// Find the prefix length where all data copies match
				auto pair = std::mismatch(it, it + same, data.begin() + offset);
				auto offset2 = std::distance(it, pair.first);
				same = std::min(same, offset2);
			}
		}
		it = it + same;

		// Show the matching block if big enough or if found
		// at the start of the data field. Other fragments will
		// be added to the diff block.
		if (same >= MIN_DIFF_BLOCK || (offset == 0 && same > 0))
		{
			if (diff)
			{
				++change_count;
				diffs.push_back(std::make_pair('-', diff));
				diff = 0;
			}

			++change_count;
			diffs.push_back(std::make_pair('=', same));
			same = 0;
		}

		offset = std::distance(smallest.begin(), it);
		auto match = std::distance(it, itEnd);

		for (const auto &data : sector.datas())
		{
			if (match)
			{
				// Find the prefix length where each copy has matching filler
				auto pair = std::mismatch(data.begin() + offset, data.begin() + offset + match - 1, data.begin() + offset + 1);
				auto offset2 = 1 + std::distance(data.begin() + offset, pair.first);
				match = std::min(match, offset2);
			}
		}
		it = it + match;

		// Show the filler block if big enough. Filler has matching
		// bytes in each copy, but a different value across copies.
		if (match >= MIN_DIFF_BLOCK)
		{
			if (diff)
			{
				++change_count;
				diffs.push_back(std::make_pair('-', diff));
				diff = 0;
			}

			++change_count;
			diffs.push_back(std::make_pair('+', match));
			match = 0;
		}

		diff += static_cast<int>(same + match);
	}

	if (diff)
	{
		++change_count;
		diffs.push_back(std::make_pair('-', diff));
	}

	return diffs;
}

// Determine the common properties of sectors on a track.
// This is mostly used by scan track headers, to reduce the detail shown for each sector.
Sector GetTypicalSector (const CylHead &cylhead, const Track &track, Sector last_sector)
{
	std::map<DataRate, int> datarates;
	std::map<Encoding, int> encodings;
	std::map<int, int> cyls, heads, sizes, gap3s;

	Sector typical = last_sector;

	// Find the most common values
	for (const auto &sector : track.sectors())
	{
		if (++datarates[sector.datarate] > datarates[typical.datarate])
			typical.datarate = sector.datarate;

		if (++encodings[sector.encoding] > encodings[typical.encoding])
			typical.encoding = sector.encoding;

		if (++cyls[sector.header.cyl] > cyls[typical.header.cyl])
			typical.header.cyl = sector.header.cyl;

		if (++heads[sector.header.head] > heads[typical.header.head])
			typical.header.head = sector.header.head;

		if (++sizes[sector.header.size] > sizes[typical.header.size])
			typical.header.size = sector.header.size;

		if (++gap3s[sector.gap3] > gap3s[typical.gap3])
			typical.gap3 = sector.gap3;
	}

	// If they are no better than the last typical sector, use with the previous values.

	if (datarates[typical.datarate] == datarates[last_sector.datarate])
		typical.datarate = last_sector.datarate;

	if (encodings[typical.encoding] == encodings[last_sector.encoding])
		typical.encoding = last_sector.encoding;

	if (cyls[typical.header.cyl] == cyls[last_sector.header.cyl])
		typical.header.cyl = last_sector.header.cyl;
	else if (cyls[typical.header.cyl] == 1)		// changed cyl from previous, but only 1 sector?
		typical.header.cyl = cylhead.cyl;		// use physical cyl instead

	if (heads[typical.header.head] == heads[last_sector.header.head])
		typical.header.head = last_sector.header.head;
	else if (heads[typical.header.head] == 1)	// changed head from previous, but only 1 sector?
		typical.header.head = cylhead.head;		// use physical head instead

	if (sizes[typical.header.size] == sizes[last_sector.header.size])
		typical.header.size = last_sector.header.size;

	// Clear the gap3 value unless all gaps are the same size or undefined
	if ((gap3s[typical.gap3] + gap3s[0]) != track.size())
		typical.gap3 = 0;

	return typical;
}


bool WriteRegularDisk (FILE *f_, Disk &disk, const Format &fmt)
{
	auto missing = 0;

	fmt.range().each([&] (const CylHead &cylhead) {
		const auto &track = disk.read_track(cylhead);
		Header header(cylhead, 0, fmt.size);

		for (header.sector = fmt.base; header.sector < fmt.base + fmt.sectors; ++header.sector)
		{
			Data buf(fmt.sector_size(), fmt.fill);

			auto it = track.find(header);
			if (it != track.end() && (*it).has_data())
			{
				const auto &data = (*it).data_copy();
				std::copy(data.begin(), data.begin() + std::min(data.size(), buf.size()), buf.begin());
			}
			else
			{
				missing++;
			}

			if (!fwrite(buf.data(), buf.size(), 1, f_))
				throw util::exception("write error, disk full?");
		}
	}, fmt.cyls_first);

	if (missing && !opt.minimal)
		Message(msgWarning, "source missing %u sectors from %u/%u/%u/%u regular format", missing, fmt.cyls, fmt.heads, fmt.sectors, fmt.sector_size());

	return true;
}
