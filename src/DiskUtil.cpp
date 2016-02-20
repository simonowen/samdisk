// Disk class utilities

#include "SAMdisk.h"
#include "DiskUtil.h"
#include "SpecialFormat.h"

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


// Normalise track contents, optionally applying load-time change filters
void NormaliseTrack (const CylHead &cylhead, Track &track)
{
	int i;
	auto sectors = track.size();

	// Clear the track length if offsets are disabled
	if (opt.offsets == 0)
		track.tracklen = 0;

	// Pass 1
	for (i = 0; i < sectors; ++i)
	{
		auto &sector = track[i];

		// Clear all data, for privacy during diagnostics?
		if (opt.nodata)
		{
			sector.remove_data();
			sector.add(Data());		// empty data rather than no data
		}

		// Track offsets disabled?
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
		// Remove gap data if disabled, or the gap mask doesn't allow it
		if (sector.has_gapdata() && (opt.gaps == GAPS_NONE || !(opt.gapmask & (1 << i))))
			sector.remove_gapdata();
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
	for (i = 0; i < sectors; ++i)
	{
		Sector &sector = track[i];
#if 0
		// Clean and remove gap data unless we're to keep it all
		if (fLoadFilter_ && opt.gaps != GAPS_ALL)
			CleanGap(i);
#endif

		// Remove only the final gap if --no-gap4b was used
		if (i == sectors - 1 && opt.gap4b == 0 && sector.has_gapdata())
			sector.remove_gapdata();

		// Allow overrides for track gap3 and sector size
		if (opt.gap3 != -1) sector.gap3 = static_cast<uint8_t>(opt.gap3);
		// if (opt.size != -1) size = opt.size;	// ToDo: remove?

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

	auto weak_offset = 0;

	// Check for Speedlock weak sector (either +3 or CPC)
	if (opt.fix != 0 && cylhead.cyl == 0 && track.size() == 9)
	{
		auto &sector1 = track[1];
		if (sector1.copies() == 1 && IsSpectrumSpeedlockTrack(track, weak_offset))
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
			}
			else
				Message(msgWarning, "image is missing multiple copies of +3 Speedlock weak sector");
		}

		auto &sector7 = track[7];
		if (sector7.copies() == 1 && IsCpcSpeedlockTrack(track, weak_offset))
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
			}
			else
				Message(msgWarning, "image is missing multiple copies of CPC Speedlock weak sector");
		}
	}

	// Check for Rainbow Arts weak sector missing copies
	if (opt.fix != 0 && cylhead.cyl == 40 && track.size() == 9)
	{
		auto &sector1 = track[1];
		if (sector1.copies() == 1 && IsRainbowArtsTrack(track, weak_offset))
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
			}
			else
				Message(msgWarning, "image is missing multiple copies of Rainbow Arts weak sector");
		}
	}

	// Single copy of an 8K sector?
	if (opt.check8k != 0 && track.is_8k_sector() && track[0].copies() == 1 && track[0].data_size() >= 0x1801)
	{
		static auto chk8k_disk = CHK8K_UNKNOWN;

		Sector &sector = track[0];
		Data &data = sector.data_copy(0);

		// Attempt to determine the 8K checksum method, if any
		auto chk8k = Get8KChecksumMethod(data.data(), data.size(), chk8k_disk);

		// If we found a positive checksum match, and the disk doesn't already have one, set it
		if (chk8k >= CHK8K_FOUND && chk8k_disk == CHK8K_UNKNOWN)
			chk8k_disk = chk8k;

		// Determine the checksum method name and the checksum length
		int checksum_len = 0;
		const char *pcszMethod = Get8KChecksumMethodName(chk8k_disk, &checksum_len);

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
}


Format GetFormat (RegularFormat reg_fmt)
{
	Format fmt;

	switch (reg_fmt)
	{
		case RegularFormat::MGT:	// 800K
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 10;
			fmt.skew = 1;
			fmt.gap3 = 24;
			break;

		case RegularFormat::ProDos:	// 720K
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 9;
			fmt.interleave = 2;
			fmt.skew = 2;
			fmt.gap3 = 0x50;
			fmt.fill = 0xe5;
			break;

		case RegularFormat::PC320:	 // 320K
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.cyls = 40;
			fmt.sectors = 8;
			fmt.interleave = 1;
			fmt.skew = 1;
			fmt.gap3 = 0x50;
			fmt.fill = 0xf6;
			break;

		case RegularFormat::PC360:	 // 360K
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.cyls = 40;
			fmt.sectors = 9;
			fmt.interleave = 1;
			fmt.skew = 1;
			fmt.gap3 = 0x50;
			fmt.fill = 0xf6;
			break;

		case RegularFormat::PC640:	 // 640K
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 8;
			fmt.interleave = 1;
			fmt.skew = 1;
			fmt.gap3 = 0x50;
			fmt.fill = 0xe5;
			break;

		case RegularFormat::PC720:	 // 720K
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 9;
			fmt.interleave = 1;
			fmt.skew = 1;
			fmt.gap3 = 0x50;
			fmt.fill = 0xf6;
			break;

		case RegularFormat::PC1200:	// 1.2M
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_500K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 15;
			fmt.interleave = 1;
			fmt.skew = 1;
			fmt.gap3 = 0x54;
			fmt.fill = 0xf6;
			break;

		case RegularFormat::PC1232:	// 1232K
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_500K;
			fmt.encoding = Encoding::MFM;
			fmt.cyls = 77;
			fmt.sectors = 8;
			fmt.size = 3;
			fmt.interleave = 1;
			fmt.skew = 1;
			fmt.gap3 = 0x54;
			fmt.fill = 0xf6;
			break;

		case RegularFormat::PC1440:	// 1.44M
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_500K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 18;
			fmt.interleave = 1;
			fmt.skew = 1;
			fmt.gap3 = 0x65;
			fmt.fill = 0xf6;
			break;

		case RegularFormat::PC2880:	// 2.88M
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_1M;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 36;
			fmt.interleave = 1;
			fmt.skew = 1;
			fmt.gap3 = 0x53;
			fmt.fill = 0xf6;
			break;

		case RegularFormat::D80:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 9;
			fmt.skew = 5;
			fmt.fill = 0xe5;
			break;

		case RegularFormat::OPD:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 18;
			fmt.size = 1;
			fmt.fill = 0xe5;
			fmt.base = 0;
			fmt.offset = 17;
			fmt.interleave = 13;
			fmt.skew = 13;
			break;

		case RegularFormat::MBD820:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.cyls = 82;
			fmt.sectors = 5;
			fmt.size = 3;
			fmt.skew = 1;
			fmt.gap3 = 44;
			break;

		case RegularFormat::MBD1804:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_500K;
			fmt.encoding = Encoding::MFM;
			fmt.cyls = 82;
			fmt.sectors = 11;
			fmt.size = 3;
			fmt.skew = 1;
			break;

		case RegularFormat::TRDOS:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.cyls = 80;
			fmt.heads = 2;
			fmt.sectors = 16;
			fmt.size = 1;
			fmt.interleave = 2;
			fmt.head1 = 0;
			break;

		case RegularFormat::D2M:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_500K;
			fmt.encoding = Encoding::MFM;
			fmt.cyls = 81;
			fmt.sectors = 10;
			fmt.size = 3;
			fmt.fill = 0xe5;
			fmt.gap3 = 0x64;
			fmt.head0 = 1;
			fmt.head1 = 0;
			break;

		case RegularFormat::D4M:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_1M;
			fmt.encoding = Encoding::MFM;
			fmt.cyls = 81;
			fmt.sectors = 20;
			fmt.size = 3;
			fmt.fill = 0xe5;
			fmt.gap3 = 0x64;
			fmt.head0 = 1;
			fmt.head1 = 0;
			break;

		case RegularFormat::D81:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 10;
			fmt.gap3 = 0x26;
			fmt.head0 = 1;
			fmt.head1 = 0;
			break;

		case RegularFormat::_2D:
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.cyls = 40;
			fmt.sectors = 16;
			fmt.size = 1;
			break;

		case RegularFormat::AmigaDOS:
			fmt.fdc = FdcType::Amiga;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::Amiga;
			fmt.cyls = 80;
			fmt.sectors = 11;
			fmt.size = 2;
			fmt.base = 0;
			break;

		case RegularFormat::AmigaDOSHD:
			fmt.fdc = FdcType::Amiga;
			fmt.datarate = DataRate::_500K;
			fmt.encoding = Encoding::Amiga;
			fmt.sectors = 22;
			fmt.size = 2;
			fmt.base = 0;
			break;

		case RegularFormat::LIF:
			fmt.cyls = 77;
			fmt.heads = 2;
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 16;
			fmt.size = 1;
			break;

		case RegularFormat::AtariST:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 9;
			fmt.gap3 = 40;
			fmt.fill = 0x00;
			break;

		default:
			assert(false);
			break;
	}

	return fmt;
}


bool SizeToFormat (int64_t size, Format &fmt)
{
	switch (size)
	{
		case 163840:	// 5.25" SSSD (160K)
			fmt = RegularFormat::PC320;
			fmt.heads = 1;
			break;

		case 184320:	// 5.25" SSSD (180K)
			fmt = RegularFormat::PC360;
			fmt.heads = 1;
			break;

		case 327680:	// 5.25" DSDD (320K)
			fmt = RegularFormat::PC320;
			break;

		case 368640:	// 5.25" DSDD (360K)
			fmt = RegularFormat::PC360;
			break;

		case 655360:	// 3.5"  DSDD (640K)
			fmt = RegularFormat::PC640;
			break;

		case 737280:	// 3.5"  DSDD (720K)
			fmt = RegularFormat::PC720;
			break;

		case 1228800:	// 5.25" DSHD (1200K)
			fmt = RegularFormat::PC1200;
			break;

		case 1261568:	// 5.25" DSHD (1232K)
			fmt = RegularFormat::PC1232;
			break;

		case 1474560:	// 3.5"  DSHD (1440K)
			fmt = RegularFormat::PC1440;
			break;

		case 1638400:	// 3.5"  DSHD (1600K)
			fmt = RegularFormat::PC1440;
			fmt.sectors = 20;
			fmt.gap3 = 0;
			break;

		case 1720320:	// 3.5"  DSHD (1680K)
			fmt = RegularFormat::PC1440;
			fmt.sectors = 21;
			fmt.gap3 = 0;
			break;

		case 1763328:	// 3.5"  DSHD (1722K)
			fmt = RegularFormat::PC1440;
			fmt.cyls = 82;
			fmt.sectors = 21;
			fmt.gap3 = 0;
			break;

		case 1784832:	// 3.5"  DSHD (1743K)
			fmt = RegularFormat::PC1440;
			fmt.cyls = 83;
			fmt.sectors = 21;
			fmt.gap3 = 0;
			break;

		case 1802240:	// 3.5"  DSHD (1760K)
			fmt = RegularFormat::PC1440;
			fmt.sectors = 22;
			fmt.gap3 = 0;
			break;

		case 1884160:	// 3.5"  DSHD (1840K)
			fmt = RegularFormat::PC1440;
			fmt.sectors = 23;
			fmt.gap3 = 0;
			break;

		case 1966080:	// 3.5"  DSHD (1920K)
			fmt = RegularFormat::PC1440;
			fmt.sectors = 24;
			fmt.gap3 = 0;
			break;

		case 2949120:	// 3.5"  DSED (2880K)
			fmt = RegularFormat::PC2880;
			break;

		default:
			return false;
	}

	return true;
}


void OverrideFormat (Format &fmt, bool full_control/*=false*/)
{
	if (full_control)
	{
		if (opt.range.cyls()) fmt.cyls = opt.range.cyls();
		if (opt.range.heads()) fmt.heads = opt.range.heads();
		if (opt.sectors != -1) fmt.sectors = opt.sectors;
		if (opt.size >= 0 && opt.size <= 7) fmt.size = opt.size;

		fmt.encoding = Encoding::MFM;

		if (fmt.track_size() < 6000)
			fmt.datarate = DataRate::_250K;
		else if (fmt.track_size() < 12000)
			fmt.datarate = DataRate::_500K;
		else
			fmt.datarate = DataRate::_1M;
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
	if (opt.cylsfirst != -1) fmt.cyls_first = (opt.cylsfirst != 0);
	if (opt.rate != -1) fmt.datarate = static_cast<DataRate>(opt.rate * 1000);
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
