// Richard Wilson's Amstrad CPC disk format:
//  https://web.archive.org/web/20060215102422/http://andercheran.aiind.upv.es/~amstrad/docs/dsc.html

#include "SAMdisk.h"

typedef struct
{
	uint8_t head : 1;	// b0 = head
	uint8_t cyl : 7;	// b7-1 = cyl
	uint8_t sectors;
} DSC_TRACK;

typedef struct
{
	uint8_t cyl;
	uint8_t head;
	uint8_t sector;
	uint8_t size;
	uint8_t flags;		// b1 set to use fill value instead of reading sector data
	uint8_t fill;       // sector filler byte, if flag set above
} DSC_SECTOR;

typedef struct
{
	uint8_t bOff256, bOff64K, bOff16M;
} DSC_DATA;


bool ReadDSC (MemFile &file, std::shared_ptr<Disk> &disk)
{
	file.rewind();

	// ToDo: std::string for file extension
	std::string strPath = file.path();
	MemFile file2;
	MemFile *pfileHeader = &file, *pfileData = &file2;

	// If we've been given the header, open the data file
	if (IsFileExt(strPath, "hdr"))
	{
		// HDR->DSC (preserving case)
		size_t offset = strPath.length() - 3;
		strPath[offset + 0] = 'D' | (strPath[offset + 0] & 0x20);
		strPath[offset + 1] = 'S' | (strPath[offset + 1] & 0x20);
		strPath[offset + 2] = 'C' | (strPath[offset + 2] & 0x20);

		if (!file2.open(strPath))
			throw util::exception("missing .dsc companion file");
	}
	// Or if given the data file, switch to the header
	else if (IsFileExt(strPath, "dsc"))
	{
		// DSC->HDR
		size_t offset = strPath.length() - 3;
		strPath[offset + 0] = 'H' | (strPath[offset + 0] & 0x20);
		strPath[offset + 1] = 'D' | (strPath[offset + 1] & 0x20);
		strPath[offset + 2] = 'R' | (strPath[offset + 2] & 0x20);

		if (!file2.open(strPath))
			throw util::exception("missing .hdr companion file");

		std::swap(pfileHeader, pfileData);
	}
	// Reject other file extensions
	else
		return false;

	for (;;)
	{
		DSC_TRACK dh;
		if (!pfileHeader->read(&dh, sizeof(dh)))
			break;

		Track track;
		CylHead cylhead(dh.cyl, dh.head);

		for (int i = 0; i < dh.sectors; ++i)
		{
			DSC_SECTOR ds;
			if (!pfileHeader->read(&ds, sizeof(ds), 1))
				throw util::exception("short file reading %s", CHS(cylhead.cyl, cylhead.head, i));

			Sector sector(DataRate::_250K, Encoding::MFM, Header(ds.cyl, ds.head, ds.sector, ds.size));

			if (ds.flags & ~0x02)
				Message(msgWarning, "unknown flags [%02X] for %s", ds.flags & ~0x02, CHSR(cylhead.cyl, cylhead.head, i, ds.sector));

			// b1 of flags indicates fill byte
			if (ds.flags & 0x02)
				sector.add(Data(sector.size(), ds.fill));
			else
			{
				DSC_DATA dd;
				if (!pfileHeader->read(&dd, sizeof(dd)))
					throw util::exception("short file reading ", CHSR(cylhead.cyl, cylhead.head, i, sector.header.sector));

				auto offset = (dd.bOff16M << 24) | (dd.bOff64K << 16) | (dd.bOff256 << 8);
				if (offset != pfileData->tell())
					Message(msgWarning, "header offset (%lu) != data pointer (%lu)", offset, pfileData->tell());

				Data data(sector.size());
				if (!pfileData->read(data))
					throw util::exception("short file reading %s data", CHSR(cylhead.cyl, cylhead.head, i, sector.header.sector));
				sector.add(std::move(data));
			}

			track.add(std::move(sector));
		}

		disk->write_track(cylhead, std::move(track));
	}

	disk->strType = "DSC";
	return true;
}
