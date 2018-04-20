// Disk class utilities

#include "SAMdisk.h"
#include "DiskUtil.h"
#include "SpecialFormat.h"
#include "TrackDataParser.h"

static const int MIN_DIFF_BLOCK = 16;
static const int DEFAULT_MAX_SPLICE = 72;	// limit of bits treated as splice noise between recognised gap patterns


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
			else if (sector.is_rx02dam() && sector.encoding != Encoding::RX02)
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
			else if (sector.has_data() && !sector.has_baddatacrc() && track.data_overlap(sector))
			{
				item_separator(items++);
				util::cout << colour::YELLOW << "o" << colour::none;	// Overlaps next sector
			}
			else if (sector.has_data() && sector.has_shortdata() && !sector.has_baddatacrc())
			{
				item_separator(items++);
				util::cout << colour::RED << "-" << (sector.size() - sector.data_size()) << colour::none;	// Less data than sector size
			}

			if (sector.encoding != context.sector.encoding)
			{
				item_separator(items++);
				util::cout << colour::CYAN << short_name(sector.encoding) << colour::none;	// FM sector on MFM track
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
					if (offset >= prevoffset)
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


// Normalise track contents, performing overrides and applying fixes as requested.
bool NormaliseTrack (const CylHead &cylhead, Track &track)
{
	bool changed = false;
	int i;

	// Clear the track length if offsets are disabled (cosmetic: no changed flag).
	if (opt.offsets == 0)
		track.tracklen = 0;

	// Pass 1
	for (i = 0; i < track.size(); ++i)
	{
		auto &sector = track[i];

		// Remove sectors with duplicate CHRN?
		if (opt.nodups)
		{
			// Remove duplicates found later on the track.
			for (int j = i + 1; j < track.size(); ++j)
			{
				auto &s = track[j];
				if (s.header.compare_chrn(sector.header) && s.encoding == sector.encoding)
				{
					track.remove(j--);
					changed = true;
				}
			}
		}

		// Clear all data, for privacy during diagnostics?
		if (opt.nodata && sector.has_data())
		{
			sector.remove_data();
			sector.add(Data());		// empty data rather than no data
			changed = true;
		}

		// Track offsets disabled? (cosmetic: no changed flag)
		if (opt.offsets == 0)
			sector.offset = 0;
#if 0
		// ToDo: move this to fdrawcmd disk type
		// Build ASRock corruption check block
		if (i < (int)arraysize(ah))
		{
			ah[i].cyl = sector.header.cyl;
			ah[i].head = sector.header.head;
			ah[i].sector = sector.header.sector;
			ah[i].size = sector.header.size;
		}
#endif

		if (sector.has_gapdata())
		{
			// Remove gap data if disabled, or the gap mask doesn't allow it
			if (opt.gaps == GAPS_NONE || !(opt.gapmask & (1 << i)))
			{
				sector.remove_gapdata();
				changed = true;
			}
			// Remove normal gaps unless we're asked to keep them.
			else if (opt.gaps == GAPS_CLEAN && sector.encoding == Encoding::MFM)
			{
				int gap3 = 0;
				if (test_remove_gap3(sector.data_copy(), sector.size(), gap3))
				{
					sector.remove_gapdata(true);
					changed = true;

					if (!sector.gap3)
						sector.gap3 = gap3;
				}
			}
		}
#if 0
		// ToDo: move this to affected disk types
		// Check for short sector wrapping the track end (includes logoprof.dmk and Les Justiciers 2 [1B].scp [CPC])
		// Also check it doesn't trigger in cyl 18 sector 5 of Super Sprint (KryoFlux).
		if (ps->uData > 0 && tracklen > 0 && ps->offset > 0 &&
			ps->uData < uSize &&
			GetDataExtent(i) > uSize &&
			(ps->offset + GetSectorOverhead(encrate) + uSize) > tracklen)
		{
			Message(msgWarning, "%s truncated at end of track data", CHSR(cyl, head, i, ps->sector));
		}
#endif
	}

	// Pass 2
	for (i = 0; i < track.size(); ++i)
	{
		Sector &sector = track[i];

		// Remove only the final gap if --no-gap4b was used
		if (i == (track.size() - 1) && opt.gap4b == 0 && sector.has_gapdata())
		{
			sector.remove_gapdata(true);
			changed = true;
		}

		// Allow override for sector datarate.
		if (opt.datarate != DataRate::Unknown)
		{
			sector.datarate = opt.datarate;
			changed = true;
		}

		// Allow override for sector encoding.
		if (opt.encoding != Encoding::Unknown)
		{
			sector.encoding = opt.encoding;
			changed = true;
		}

		// Allow overrides for track gap3 (cosmetic: no changed flag)
		if (opt.gap3 != -1)
			sector.gap3 = static_cast<uint8_t>(opt.gap3);
#if 0
		// ToDo: move this to fdrawcmd disk type
		// Check for the ASRock FDC problem that corrupts sector data with format headers (unless comparison block is all one byte)
		int nn = sizeof(ah) - sizeof(ah[0]) - 1;
		if (sectors > 1 && ps->apbData[0] && memcmp(ps->apbData[0], ps->apbData[0] + 1, sizeof(ah) - 1))
		{
			// Ignore the first sector and size in case of a placeholder sector
			if (!memcmp(&ah[1], ps->apbData[0] + sizeof(ah[0]), sizeof(ah) - sizeof(ah[0])))
				Message(msgWarning, "possible FDC data corruption on %s", CHSR(cyl, head, i, ps->sector));
		}
#endif
	}

	int weak_offset, weak_size;

	// Check for Speedlock weak sector (either +3 or CPC)
	if (opt.fix != 0 && cylhead.cyl == 0 && track.size() == 9)
	{
		auto &sector1 = track[1];
		if (sector1.copies() == 1 && IsSpectrumSpeedlockTrack(track, weak_offset, weak_size))
		{
			// Are we to add the missing weak sector?
			if (opt.fix == 1)
			{
				// Add a copy with differences matching the typical weak sector
				auto data = sector1.data_copy();
				for (i = weak_offset; i < data.size(); ++i)
					data[i] = ~data[i];
				sector1.add(std::move(data), true);

				Message(msgFix, "added suitable second copy of +3 Speedlock weak sector");
				changed = true;
			}
			else
				Message(msgWarning, "missing multiple copies of +3 Speedlock weak sector");
		}

		auto &sector7 = track[7];
		if (sector7.copies() == 1 && IsCpcSpeedlockTrack(track, weak_offset, weak_size))
		{
			// Are we to add the missing weak sector?
			if (opt.fix == 1)
			{
				// Add a second data copy with differences matching the typical weak sector
				auto data = sector7.data_copy();
				for (i = weak_offset; i < data.size(); ++i)
					data[i] = ~data[i];
				sector7.add(std::move(data), true);

				Message(msgFix, "added suitable second copy of CPC Speedlock weak sector");
				changed = true;
			}
			else
				Message(msgWarning, "missing multiple copies of CPC Speedlock weak sector");
		}
	}

	// Check for Rainbow Arts weak sector missing copies
	if (opt.fix != 0 && cylhead.cyl == 40 && track.size() == 9)
	{
		auto &sector1 = track[1];
		if (sector1.copies() == 1 && IsRainbowArtsTrack(track, weak_offset, weak_size))
		{
			// Are we to add the missing weak sector?
			if (opt.fix == 1)
			{
				// Ensure the weak sector has a data CRC error, to fix broken CPCDiskXP images
				if (!sector1.has_baddatacrc())
				{
					auto data = sector1.data_copy();
					sector1.remove_data();
					sector1.add(std::move(data), true);
					if (opt.debug) util::cout << "added missing data CRC error to Rainbow Arts track\n";
				}

				// Add a second data copy with differences matching the typical weak sector
				auto data = sector1.data_copy();
				for (i = weak_offset; i < data.size(); ++i)
					data[i] = ~data[i];
				sector1.add(std::move(data), true);

				Message(msgFix, "added suitable second copy of Rainbow Arts weak sector");
				changed = true;
			}
			else
				Message(msgWarning, "missing multiple copies of Rainbow Arts weak sector");
		}
	}

	// Check for missing OperaSoft 32K sector (CPDRead dumps).
	if (opt.fix != 0 && cylhead.cyl == 40 && track.size() == 9)
	{
		auto &sector7 = track[7];
		auto &sector8 = track[8];
		if (sector7.has_data() && sector8.data_size() == 0 && IsOperaSoftTrack(track))
		{
			if (opt.fix == 1)
			{
				const auto &data7 = sector7.data_copy();

				// Add 0x55 data and correct CRC for 256 bytes
				auto data8{ Data(256, 0x55) };
				data8.push_back(0xe8);
				data8.push_back(0x9f);

				// Fill up to the protection check with gap filler
				auto fill8{ Data(0x512 - data8.size(), 0x4e) };
				data8.insert(data8.end(), fill8.begin(), fill8.end());

				// Append sector 7 data to offset 0x512 to pass the protection check.
				data8.insert(data8.end(), data7.begin(), data7.end());

				// Replace any existing sector 8 data with out hand-crafted version.
				sector8.remove_data();
				sector8.add(std::move(data8), true);

				Message(msgFix, "added missing data to OperaSoft 32K sector");
				changed = true;
			}
			else
				Message(msgWarning, "missing data in OperaSoft 32K sector");
		}
	}

	// Single copy of an 8K sector?
	if (opt.check8k != 0 && track.is_8k_sector() && track[0].copies() == 1 && track[0].data_size() >= 0x1801)
	{
		static auto chk8k_disk = CHK8K_UNKNOWN;
		static int chk8k_id = -1;

		const auto &sector = track[0];
		const auto &data = sector.data_copy();

		// If the sector ID has changed, so might the checksum method, so start over.
		// This is used by Fun Radio [2B] (CPC).
		if (sector.header.sector != chk8k_id)
		{
			chk8k_disk = CHK8K_UNKNOWN;
			chk8k_id = sector.header.sector;
		}

		// Attempt to determine the 8K checksum method, if any
		auto chk8k = Get8KChecksumMethod(data.data(), data.size(), chk8k_disk);

		// If we found a positive checksum match, and the disk doesn't already have one, set it
		if (chk8k >= CHK8K_FOUND && chk8k_disk == CHK8K_UNKNOWN)
			chk8k_disk = chk8k;

		// Determine the checksum method name and the checksum length
		int checksum_len = 0;
		auto pcszMethod = Get8KChecksumMethodName(chk8k_disk, checksum_len);

		// If what we've found doesn't match the disk checksum method, report it
		if (chk8k_disk >= CHK8K_FOUND && chk8k != chk8k_disk && chk8k != CHK8K_VALID)
		{
			if (checksum_len == 2)
				Message(msgWarning, "invalid %s checksum [%02X %02X] on %s", pcszMethod, data[0x1800], data[0x1801], CH(cylhead.cyl, cylhead.head));
			else
				Message(msgWarning, "invalid %s checksum [%02X] on %s", pcszMethod, data[0x1800], CH(cylhead.cyl, cylhead.head));
		}
		// If we've yet to find a valid disk method, but didn't recognise this track, report that too
		else if (chk8k == CHK8K_UNKNOWN)
		{
			if (data.size() >= 0x1802)
				Message(msgWarning, "unrecognised or invalid 6K checksum [%02X %02X] on %s", data[0x1800], data[0x1801], CH(cylhead.cyl, cylhead.head));
			else
				Message(msgWarning, "unrecognised or invalid 6K checksum [%02X] on %s", data[0x1800], CH(cylhead.cyl, cylhead.head));
		}
	}

	// Return whether the supplied track was modified.
	return changed;
}

bool NormaliseBitstream (BitBuffer &bitbuf)
{
	bool modified = false;

	// Align sync marks to byte boundaries?
	if (opt.align)
		modified |= bitbuf.align();

	return modified;
}

// Attempt to repair a track, given another copy of the same track.
bool RepairTrack (const CylHead &cylhead, Track &track, const Track &src_track)
{
	bool changed = false;

	// Loop over all source sectors available.
	for (auto src_sector : src_track)
	{
		// Skip repeated source sectors, as the data source is ambiguous.
		if (src_track.is_repeated(src_sector))
			continue;

		// In real-world use 250Kbps/300Kbps are interchangable due to 300rpm/360rpm.
		if (!track.empty() &&
			(track[0].datarate == DataRate::_250K || track[0].datarate == DataRate::_300K) &&
			(src_sector.datarate == DataRate::_250K || src_sector.datarate == DataRate::_300K))
		{
			// Convert source to target data rate.
			src_sector.datarate = track[0].datarate;
		}

		// Find a target sector with the same CHRN, datarate, and encoding.
		auto it = track.find(src_sector.header, src_sector.datarate, src_sector.encoding);
		if (it != track.end())
		{
			// Skip repeated target sectors, as the repair target is ambiguous.
			if (track.is_repeated(*it))
				continue;

			// Merge the two sectors to give the best version.
			if (it->merge(std::move(src_sector)) == Sector::Merge::Improved)
			{
				if (it->has_good_data())
					Message(msgFix, "repaired %s", CHR(cylhead.cyl, cylhead.head, it->header.sector));
				else
					Message(msgFix, "improved %s", CHR(cylhead.cyl, cylhead.head, it->header.sector));
				changed = true;
			}
		}
		else
		{
			// Default to adding to the end of the track.
			auto insert_idx = track.size();

			// Loop over sectors appearing after the current sector on the source track.
			auto idx_src = src_track.index_of(src_sector);
			for (int i = idx_src + 1; i < src_track.size(); ++i)
			{
				auto &s = src_track[i];

				// Attempt to find the same sector on the target track.
				it = track.find(s.header, s.datarate, s.encoding);
				if (it != track.end())
				{
					// The missing sector must appear before the match we just found.
					insert_idx = track.index_of(*it);
					break;
				}
			}

			Message(msgFix, "added missing %s", CHR(cylhead.cyl, cylhead.head, src_sector.header.sector));
			track.insert(insert_idx, std::move(src_sector));
			changed = true;
		}
	}

	return changed;
}


std::vector<std::pair<char, size_t>> DiffSectorCopies (const Sector &sector)
{
	assert(sector.copies() > 0);
	std::vector<std::pair<char, size_t>> diffs;

	auto &smallest = *std::min_element(
		sector.datas().begin(), sector.datas().end(),
		[](const Data &d1, const Data &d2) {
			return d1.size() < d2.size();
		}
	);
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
Sector GetTypicalSector (const CylHead &cylhead, const Track &track, const Sector &last)
{
	std::map<DataRate, int> datarates;
	std::map<Encoding, int> encodings;
	std::map<int, int> cyls, heads, sizes, gap3s;

	Sector typical = last;

	// Find the most common values
	for (const auto &sector : track)
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

	// Use the previous values if the typical values are no better,
	// except for cyl and head where we favour the physical position
	// and prevent a single different sector changing the values.

	if (datarates[typical.datarate] == datarates[last.datarate])
		typical.datarate = last.datarate;

	if (encodings[typical.encoding] == encodings[last.encoding])
		typical.encoding = last.encoding;

	if (cyls[typical.header.cyl] == cyls[cylhead.cyl])
		typical.header.cyl = cylhead.cyl;
	else if (cyls[typical.header.cyl] == cyls[last.header.cyl])
		typical.header.cyl = last.header.cyl;
	else if (cyls[typical.header.cyl] == 1)
		typical.header.cyl = cylhead.cyl;

	if (heads[typical.header.head] == heads[cylhead.head])
		typical.header.head = cylhead.head;
	else if (heads[typical.header.head] == heads[last.header.head])
		typical.header.head = last.header.head;
	else if (heads[typical.header.head] == 1)
		typical.header.head = cylhead.head;

	if (sizes[typical.header.size] == sizes[last.header.size])
		typical.header.size = last.header.size;

	// Use previous gap3 if still present, or if no new gap3 was found.
	if (last.gap3 && (gap3s[last.gap3] || !typical.gap3))
		typical.gap3 = last.gap3;

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


bool test_remove_gap2(const Data &data, int offset)
{
	if (data.size() < offset)
		return false;

	TrackDataParser parser(data.data() + offset, data.size() - offset);
	auto max_splice = (opt.maxsplice == -1) ? DEFAULT_MAX_SPLICE : opt.maxsplice;
	auto splice = 0, len = 0;
	uint8_t fill;

	if (opt.debug) util::cout << "----gap2----:\n";

	parser.GetGapRun(len, fill);

	if (!len)
	{
		splice = 1;
		for (; parser.GetGapRun(len, fill) && !len; ++splice);

		if (opt.debug) util::cout << "found " << splice << " splice bits\n";
		if (splice > max_splice)
		{
			if (opt.debug) util::cout << "stopping due to too many splice bits\n";
			return false;
		}
	}

	if (len > 0 && fill == 0x4e)
	{
		if (opt.debug) util::cout << "found " << len << " bytes of " << fill << " filler\n";
		parser.GetGapRun(len, fill);
	}

	if (!len)
	{
		splice = 1;
		for (; parser.GetGapRun(len, fill) && !len; ++splice);

		if (opt.debug) util::cout << "found " << splice << " splice bits\n";
		if (splice > max_splice)
		{
			if (opt.debug) util::cout << "stopping due to too many splice bits\n";
			return false;
		}
	}

	if (len > 0 && fill == 0x00)
	{
		if (opt.debug) util::cout << "found " << len << " bytes of " << fill << " filler\n";
		parser.GetGapRun(len, fill);
	}

	if (!len)
	{
		splice = 1;
		for (; parser.GetGapRun(len, fill) && !len; ++splice);
		if (opt.debug) util::cout << "found " << splice << " splice bits\n";
		if (splice > max_splice)
			return false;
	}

	if (len > 0)
	{
		if (fill != 0x00)
		{
			if (opt.debug) util::cout << "stopping due to " << len << " bytes of " << fill << " filler\n";
			return false;
		}

		if (opt.debug) util::cout << "found " << len << " bytes of " << fill << " filler\n";
	}

	if (opt.debug) util::cout << "gap2 can be removed\n";
	return true;
}

bool test_remove_gap3(const Data &data, int offset, int &gap3)
{
	if (data.size() < offset)
		return false;

	TrackDataParser parser(data.data() + offset, data.size() - offset);
	auto max_splice = (opt.maxsplice == -1) ? DEFAULT_MAX_SPLICE : opt.maxsplice;
	auto splice = 0, len = 0;
	uint8_t fill = 0x00;
	bool unshifted = true;

	if (opt.debug) util::cout << "----gap3----:\n";
	while (!parser.IsWrapped())
	{
		parser.GetGapRun(len, fill, &unshifted);

		if (!len)
		{
			splice = 1;
			for (; parser.GetGapRun(len, fill) && !len; ++splice);
			if (opt.debug) util::cout << "found " << splice << " splice bits\n";
			if (splice > max_splice)
			{
				if (opt.debug) util::cout << "stopping due to too many splice bits\n";
				return false;
			}
		}

		if (len == 3 && fill == 0xa1)
		{
			auto am = parser.ReadByte();
			if (opt.debug) util::cout << "found AM (" << am << ")\n";
			break;
		}

		if (len > 0 && fill != 0x00 && fill != 0x4e)
		{
			if (opt.debug) util::cout << "stopping due to " << len << " bytes of " << fill << " filler\n";
			return false;
		}

		if (len > 0 && fill == 0x4e && !gap3)
		{
			gap3 = static_cast<uint8_t>(len);
			if (opt.debug) util::cout << "gap3 size is " << len << " bytes\n";
		}
		if (len > 0)
		{
			if (opt.debug) util::cout << "found " << len << " bytes of " << fill << " filler\n";
		}
	}

	// Ignore the detected gap3 if it's not naturally aligned in the bitstream.
	if (gap3 && !unshifted)
		gap3 = 0;

	if (opt.debug) util::cout << "gap3 can be removed\n";
	return true;
}

bool test_remove_gap4b(const Data &data, int offset)
{
	if (data.size() < offset)
		return false;

	TrackDataParser parser(data.data() + offset, data.size() - offset);
	auto splice = 0, len = 0;
	uint8_t fill;

	if (opt.debug) util::cout << "----gap4b----:\n";

	parser.GetGapRun(len, fill);

	if (!len)
	{
		splice = 1;
		for (; parser.GetGapRun(len, fill) && !len; ++splice);
		if (opt.debug) util::cout << "found " << splice << " splice bits\n";
		/*
		auto max_splice = (opt.maxsplice == -1) ? DEFAULT_MAX_SPLICE : opt.maxsplice;
		if (splice > max_splice)
		{
		if (opt.debug) util::cout << "stopping due to too many splice bits\n";
		return false;
		}
		*/
	}

	if (len > 0 && (fill == 0x4e || fill == 0x00))
	{
		if (opt.debug) util::cout << "found " << len << " bytes of " << fill << " filler\n";
	}
	else if (len > 0)
	{
		if (opt.debug) util::cout << "stopping due to " << len << " bytes of " << fill << " filler\n";
		return false;
	}

	if (opt.debug) util::cout << "gap4b can be removed\n";
	return true;
}
