// Scan command

#include "SAMdisk.h"
#include "IBMPC.h"
#include "DiskUtil.h"

// Normalise track contents, optionally applying load-time change filters
void Normalise (const CylHead &cylhead, Track &track)
{
#if 0
	FD_ID_HEADER ah[8] = {};
#endif
	int i;
	auto sectors = track.size();

	// Clear the track length if offsets are disabled
	if (opt.offsets == 0)
		track.tracklen = 0;

	// Pass 1
	for (i = 0; i < sectors; ++i)
	{
		Sector &sector = track[i];
#if 0
		// Calculate the approximate track size, with abnormal sectors counted as minimum size
		if (uTrackSize) uTrackSize += GetSectorOverhead(encrate);
		uTrackSize += (ps->apbData[0] && !ps->IsDataCRC()) ? uSize : MIN_SECTOR_SIZE;
#endif
		// Should we clear all data, for privacy during diagnostics?
		if (opt.nodata)
		{
			sector.remove_data();
			sector.add(Data());		// empty data
		}

		// Track offsets disabled?
		if (opt.offsets == 0)
			sector.offset = 0;
#if 0
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

#if 0
	// If the good data total exceeds the natural track size, we should attempt to remove duplicates
	bool fRemoveDups = uTrackSize > GetTrackCapacity(RPM_TIME_300, encrate);
#endif
	auto removed = 0;
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
//		if (opt.size != -1) size = opt.size;	// ToDo: remove?

#if 0
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

	// Any sectors removed?
	if (removed)
	{
		Message(msgFix, "removed %u duplicate %s from oversized %s",
				removed, (removed == 1) ? "sector" : "sectors", CH(cylhead.cyl, cylhead.head));
	}

#if 0
	// Check for Speedlock weak sector (either +3 or CPC)
	if (opt.fix != 0 && cylhead.cyl == 0 && sectors == 9 && track[1].has_data() && track[7].has_data())
	{
		auto weak_offset = 0;

		Sector &sector = track[1];
		if (ps->IsDataCRC() && ps->uData && !ps->apbData[1] && IsSpectrumSpeedlockTrack(this, &weak_offset))
		{
			// Are we to add the missing weak sector?
			if (opt.fix == 1)
			{
				// Add a copy with differences matching the typical weak sector
				memcpy(ps->apbData[1] = new BYTE[ps->uData], ps->apbData[0], ps->uData);
				for (i = weak_offset; i < ps->uData; ++i) ps->apbData[1][i] = ~ps->apbData[0][i];

				Message(msgFix, "added suitable second copy of +3 Speedlock weak sector");
			}
			else
				Message(msgWarning, "image is missing multiple copies of +3 Speedlock weak sector");
		}

		ps = &sector[7];
		if (ps->IsDataCRC() && ps->uData && !ps->apbData[1] && IsCpcSpeedlockTrack(this, &weak_offset))
		{
			// Are we to add the missing weak sector?
			if (opt.fix == 1)
			{
				// Add a second data copy with differences matching the typical weak sector
				memcpy(ps->apbData[1] = new BYTE[ps->uData], ps->apbData[0], ps->uData);
				for (i = weak_offset; i < ps->uData; ++i) ps->apbData[1][i] = ~ps->apbData[0][i];

				Message(msgFix, "added suitable second copy of CPC Speedlock weak sector");
			}
			else
				Message(msgWarning, "image is missing multiple copies of CPC Speedlock weak sector");
		}
	}

	// Check for Rainbow Arts weak sector missing copies
	if (opt.fix != 0 && sectors == 9 && sector[1].sector == 198 && sector[1].uData &&
		sector[1].apbData[0] && !sector[1].apbData[1] && sector[3].apbData[0] &&
		!memcmp(sector[3].apbData[0], "\x2a\x6d\xa7\x01\x30\x01\xaf\xed\x42\x4d\x44\x21\x70\x01", 14))
	{
		// Are we to add the missing weak sector?
		if (opt.fix == 1)
		{
			PSECTOR ps = &sector[1];

			// Ensure the weak sector has a data CRC error, to fix broken CPCDiskXP images
			ps->flags |= SF_DATACRC;

			// Add a second data copy with differences matching the typical weak sector
			memcpy(ps->apbData[1] = new BYTE[ps->uData], ps->apbData[0], ps->uData);
			for (i = 100; i < ps->uData; ++i) ps->apbData[1][i] = ~ps->apbData[0][i];

			Message(msgFix, "added suitable second copy of Rainbow Arts weak sector");
		}
		else
			Message(msgWarning, "image is missing multiple copies of Rainbow Arts weak sector");
	}
#endif
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


void ScanTrack (const CylHead &cylhead, const Track &track, ScanContext &context)
{
	// Reset the context if the cylinder is before the last (such as at a head change)
	if (cylhead.cyl < context.last_cylhead.cyl)
		context = ScanContext();

	context.last_cylhead = cylhead;

	// Process only non-blank tracks
	if (!track.empty())
	{
		// Have we warned about possible incompatible disks?
		// Example disk: Anaconda (TR-DOS)
		if (!context.warned && track.tracklen && track[0].encoding == Encoding::MFM)
		{
			auto &first_sector = *(track.sectors().begin());
			auto &last_sector = *(track.sectors().rbegin());

			// Calculate the offset needed to hide a 256-byte sector using the current encoding and
			// data rate.  If the first sector offset exceeds that then there could be a problem.
			auto min_offset_bits = (Sector::SizeCodeToLength(1) + GetSectorOverhead(first_sector.encoding)) * 16;

			// Calculate the gap between the end of final sector and the start of the first sector
			auto data_end_bits = last_sector.offset + (GetSectorOverhead(last_sector.encoding) + last_sector.size() * 16);
			auto wrap_start_bits = track.tracklen + first_sector.offset;

			// If the gap before the first visible sector is suspiciously large, and the wrapping
			// gap is large enough to hide a 256-byte sector, warn the user
			if (first_sector.offset > min_offset_bits && (data_end_bits + min_offset_bits) < wrap_start_bits)
			{
				Message(msgWarning, "late track start (@%lu) may indicate missing first sector", first_sector.offset / 16);
				context.warned = true;
			}
		}

		Sector typical = GetTypicalSector(cylhead, track, context.sector);
		bool custom_cyl = cylhead.cyl != typical.header.cyl;
		bool custom_head = cylhead.head != typical.header.head;

		// If the encoding, rate or size have changed, show the track settings
		if (typical.datarate != context.sector.datarate ||
			typical.encoding != context.sector.encoding ||
			(typical.header.cyl != context.sector.header.cyl && custom_cyl) ||
			(typical.header.head != context.sector.header.head && custom_head) ||
			typical.header.size != context.sector.header.size ||
			typical.gap3 != context.gap3 ||
			track.size() != context.sectors ||
			custom_cyl != context.custom_cyl ||
			custom_head != context.custom_head)
		{
			util::cout << util::fmt("%s %s, %2u sector%s, %4u bytes%s",
									to_string(typical.datarate).c_str(), to_string(typical.encoding).c_str(),
									track.size(), (track.size() == 1) ? "" : "s",
									typical.size(), (track.size() == 1) ? "" : "/sector");

			if (custom_cyl && typical.header.cyl != cylhead.cyl) util::cout << ", c=" << CylStr(typical.header.cyl);
			if (custom_head && typical.header.head != cylhead.head) util::cout << ", h=" << HeadStr(typical.header.head);
			if (typical.header.size != Sector::SizeCodeToRealSizeCode(typical.header.size)) util::cout << ", n=" << SizeStr(typical.header.size);
			if (typical.gap3 != 0) util::cout << ", gap3=" << ByteStr(typical.gap3);

			util::cout << ":\n";
		}

		context.sector = typical;
		context.sectors = track.size();
		context.gap3 = typical.gap3;
		context.custom_cyl = custom_cyl;
		context.custom_head = custom_head;
	}

	auto flags = 0;
	if (opt.offsets == 1) flags |= DUMP_OFFSETS;
	if (!opt.nodiff) flags |= DUMP_DIFF;
	DumpTrack(cylhead, track, context, flags);
}

bool ScanImage (const std::string &path, Range range)
{
	util::cout << '[' << path << "]\n";
	util::cout.screen->flush();

	auto disk = std::make_shared<Disk>();
	if (ReadImage(path, disk))
	{
		Format &fmt = disk->fmt;

		// Regular format and no range specified?
		if (!opt.verbose && range.empty() && fmt.sectors > 0)
		{
			util::cout << util::fmt("%s %s, %2u cyls, %u heads, %2u sectors, %4u bytes/sector\n",
									to_string(fmt.datarate).c_str(), to_string(fmt.encoding).c_str(),
									disk->cyls(), disk->heads(), fmt.sectors, fmt.sector_size());

			std::stringstream ss;
			if (fmt.base != 1) { ss << util::fmt(" Base=%u", fmt.base); }
			if (fmt.offset) { ss << util::fmt(" Offset=%u", fmt.offset); }
			if (fmt.skew) { ss << util::fmt(" Skew=%u", fmt.skew); }
			if (fmt.interleave > 1) { ss << util::fmt(" Interleave=%u:1", fmt.interleave); }
			if (fmt.head0 != 0) { ss << util::fmt(" Head0=%u", fmt.head0); }
			if (fmt.head1 != 1) { ss << util::fmt(" Head1=%u", fmt.head1); }
			if (fmt.gap3) { ss << util::fmt(" Gap3=%u", fmt.gap3); }

			auto str = ss.str();
			if (!str.empty())
				util::cout << str << "\n";
		}
		else
		{
			ValidateRange(range, MAX_TRACKS, MAX_SIDES, disk->cyls(), disk->heads());
			ReportRange(range);

			disk->preload(range);

			ScanContext context;
			range.each([&] (const CylHead cylhead) {
				if (!g_fAbort)
				{
					if (cylhead.cyl == range.cyl_begin)
						context = ScanContext();

//					track.Normalise(); // ToDo: ?
					auto track = disk->read_track(cylhead);
					Normalise(cylhead, track);
					ScanTrack(cylhead, track, context);
				}
			}, true);
		}
	}

	return true;
}
