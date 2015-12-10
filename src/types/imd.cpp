// Dave Dunfield's ImageDisk format:
//  http://www.classiccmp.org/dunfield/img/index.htm

#include "SAMdisk.h"

typedef struct
{
	uint8_t mode;
	uint8_t cyl, head, sectors, size;
} IMD_TRACK;

typedef struct
{
	uint8_t idstatus, datastatus;
	uint8_t cyl, head, sector, size;
	uint8_t crc1, crc2;
} IMD_SECTOR;


bool ReadIMD (MemFile &file, std::shared_ptr<Disk> &disk)
{
	char sz[128];
	if (!file.rewind() || !file.read(sz, 4) || memcmp(sz, "IMD ", 4))
		return false;

	// Search for the end of the file comment
	std::stringstream ss;
	char ch;

	file.rewind();
	while (file.read(&ch, sizeof(ch)) && ch != 0x1a)
		ss << ch;

	// Reject the file if we didn't find the end of comment
	if (ch != 0x1a)
		return false;

	// Copy any comment
	disk->metadata["comment"] = ss.str();

	uint8_t rmap[MAX_SECTORS], cmap[MAX_SECTORS], hmap[MAX_SECTORS], nmap[MAX_SECTORS * 2];
	for (;;)
	{
		IMD_TRACK it;
		if (!file.read(&it, sizeof(it)))
			break;

		// Check for sensible geometry
		if (it.cyl > MAX_TRACKS || it.sectors > MAX_SECTORS || ((it.size & 0xf8) && it.size != 0xff))
			throw util::exception("bad geometry");

		CylHead cylhead(it.cyl, it.head & 1);
		Track track;

		// Determine the encoding and data rate
		static const DataRate datarates[] = { DataRate::_500K, DataRate::_300K, DataRate::_250K, DataRate::_500K, DataRate::_300K, DataRate::_250K, DataRate::_1M, DataRate::_1M };
		static const Encoding encodings[] = { Encoding::FM, Encoding::FM, Encoding::FM, Encoding::MFM, Encoding::MFM, Encoding::MFM, Encoding::FM, Encoding::MFM };

		if (it.mode >= arraysize(datarates))
			throw util::exception("invalid track mode (", it.mode, ") on ", cylhead);

		// Read the sector map
		if (!file.read(rmap, it.sectors))
			throw util::exception("short file reading rmap for ", cylhead);

		// Read the cylinder map, if supplied
		if (!(it.head & 0x80))
			memset(cmap, it.cyl, sizeof(cmap));
		else if (!file.read(cmap, it.sectors))
			throw util::exception("short file reading cmap for ", cylhead);

		// Read the head map, if supplied
		if (!(it.head & 0x40))
			memset(hmap, it.head & 0x01, sizeof(hmap));
		else if (!file.read(hmap, it.sectors))
			throw util::exception("short file reading hmap for ", cylhead);

		// Read the size map, if supplied
		if (it.size == 0xff && !file.read(nmap, 2, it.sectors))
			throw util::exception("short file reading nmap for ", cylhead);

		for (int i = 0; i < it.sectors; ++i)
		{
			Sector sector(datarates[it.mode], encodings[it.mode], Header(cmap[i], hmap[i], rmap[i], it.size));

			// Check for size map override
			if (it.size == 0xff)
			{
				// Read the sector size from the map, and convert to a size code
				uint16_t wSize = (nmap[i * 2 + 1] << 8) | nmap[i * 2];
				sector.header.size = SizeToCode(wSize);

				// Fail if the size was invalid
				if (sector.header.size == 0xff)
					throw util::exception("invalid sector size (", wSize, ") on ", cylhead, " sector ", sector.header.sector);
			}

			uint8_t b;
			if (!file.read(&b, sizeof(b)))
				throw util::exception("short file reading ", cylhead, " sector ", sector.header.sector);

			// Unknown sector type?
			if (b > 8)
				throw util::exception("unknown sector type (", b, ") on ", cylhead, " sector ", sector.header.sector);

			// Data field present?
			if (b != 0)
			{
				// No-data is a special case entry, convert type to more bitfields
				b--;

				bool deleted_data = (b & 2) != 0;
				bool bad_data = (b & 4) != 0;
				uint8_t dam = deleted_data ? 0xf8 : 0xfb;

				// Compressed?
				if (b & 1)
				{
					// Read the fill byte
					uint8_t fill;
					if (!(file.read(&fill, sizeof(fill), 1)))
						throw util::exception("short file reading fill byte for ", cylhead, " sector ", sector.header.sector);

					sector.add(Data(sector.size(), fill), bad_data, dam);
				}
				else
				{
					Data data(sector.size());

					// Read the normal sector data
					if (!file.read(data))
						throw util::exception("short file reading ", cylhead, " sector ", sector.header.sector);

					sector.add(std::move(data), bad_data, dam);
				}
			}

			track.add(std::move(sector));
		}

		disk->write_track(cylhead, std::move(track));
	}

	disk->strType = "IMD";
	return true;
}


bool WriteIMD (FILE* /*f_*/, std::shared_ptr<Disk> &/*disk*/)
{
	throw std::logic_error("not implemented");
#if 0
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);

	// Format a suitable ASCII header
	auto strHeader = util::fmt("IMD SAMdisk%02u%02u%02u, %02u/%02u/%04u %02u:%02u:%02u",
							   YEAR % 100, MONTH + 1, DAY, tm->tm_mday, tm->tm_mon + 1,
							   1900 + tm->tm_year, tm->tm_hour, tm->tm_min, tm->tm_sec);
	fwrite(strHeader.c_str(), strHeader.length(), 1, f_);

	// Write the comment field, plus a terminating EOF
	fwrite(pd_->strComment.c_str(), pd_->strComment.length(), 1, f_);
	fwrite("\x1a", sizeof(char), 1, f_);


	for (BYTE cyl = 0; cyl < pd_->bUsedCyls; cyl++)
	{
		for (BYTE head = 0; head < pd_->bUsedHeads; head++)
		{
			PCTRACK pt = pd_->PeekTrack(cyl, head);
			int i;

			// Empty tracks aren't included in the image
			if (!pt->sectors)
				continue;

			// Map encrate to IMD track mode
			static BYTE modes[] = { 0, 1, 2, 6, 3, 4, 5, 7 };

			IMD_TRACK it;
			it.mode = modes[(pt->IsMFM() ? 4 : 0) | (pt->encrate & FD_RATE_MASK)];
			it.cyl = cyl;
			it.head = head;
			it.sectors = pt->sectors;
			it.size = pt->sector[0].size;	// default to first sector's size

			BYTE cmap[MAX_SECTORS], hmap[MAX_SECTORS], rmap[MAX_SECTORS], nmap[MAX_SECTORS * 2], hflags = 0;

			// First pass over the sectors to check for oddities
			for (i = 0; i < pt->sectors; i++)
			{
				PCSECTOR ps = &pt->sector[i];

				// Ensure the sector size is legal
				if (ps->size > 7)
				{
					throw util::exception(CHSR(pt->cyl, pt->head, i, ps->sector), " uses an extended size code, which IMD doesn't support");
// ToDo!			ScanTrack(pt, true);
					return retUnsuitableTarget;
				}
				else if (pt->IsMFM() && ps->IsFM())
				{
					throw util::exception(CH(pt->cyl, pt->head), " is mixed-density, which IMD doesn't support");
// ToDo!			ScanTrack(pt, true);
					return retUnsuitableTarget;
				}

				// Update the sector, cylinder and head maps
				cmap[i] = ps->cyl;
				hmap[i] = ps->head;
				rmap[i] = ps->sector;
				nmap[i * 2] = SectorSize(ps->size) & 0xff;
				nmap[i * 2 + 1] = static_cast<uint8_t>(SectorSize(ps->size) >> 8);

				// Note deviations from the physical cyl/head values, and fixed sector size
				if (ps->cyl != cyl)   hflags |= 0x80;
				if (ps->head != head) hflags |= 0x40;
				if (ps->size != it.size) it.size = 0xff;
			}

			// Add the flag bits to the track head value
			it.head |= hflags;

			// Write the track header
			if (!fwrite(&it, sizeof(it), 1, f_))
				return retWriteError;

			// Write the sector map
			if (!fwrite(rmap, i, 1, f_))
				return retWriteError;

			// Write the cylinder map if non-standard values were used
			if ((hflags & 0x80) && !fwrite(cmap, i, 1, f_))
				return retWriteError;

			// Write the head map if non-standard values were used
			if ((hflags & 0x40) && !fwrite(hmap, i, 1, f_))
				return retWriteError;

			// Write the size map if mixed sector sizes were used
			if (it.size == 0xff && !fwrite(nmap, 2, i, f_))
				return retWriteError;

			// Second pass over sectors to write the data out
			for (i = 0; i < pt->sectors; i++)
			{
				PCSECTOR ps = &pt->sector[i];
				unsigned uDataSize = SectorSize(ps->size);

				// Check for missing data field
				if (!ps->apbData[0])
				{
					BYTE bType = 0;

					// Write the sector data as 'not present'
					if (!fwrite(&bType, 1, 1, f_))
						return retWriteError;
				}

				// Sector filled with the same value?
				else if (!memcmp(ps->apbData[0], ps->apbData[0] + 1, uDataSize - 1))
				{
					BYTE bType = ps->IsDeleted() ? 4 : 2;
					if (ps->IsDataCRC()) bType += 4;

					// Write the type and the fill byte to repeat
					BYTE bFill = ps->apbData[0][0];
					if (!fwrite(&bType, sizeof(bType), 1, f_) || !fwrite(&bFill, sizeof(bFill), 1, f_))
						return retWriteError;
				}
				else
				{
					BYTE bType = ps->IsDeleted() ? 3 : 1;
					if (ps->IsDataCRC()) bType += 4;

					// Write the type and the full sector data
					if (!fwrite(&bType, 1, 1, f_) || !fwrite(ps->apbData[0], uDataSize, 1, f_))
						return retWriteError;
				}
			}
		}
	}

	return retOK;
#endif
}
