// Sega System 24 arcade system floppy:
//  http://segaretro.org/Sega_System_24

#include "SAMdisk.h"

const int S24_TRACKS = 80;
const int S24_SIDES = 2;

const int S24_TRACKSIZE1 = 0x2d00;
const int S24_TRACKSIZE2 = 0x2f00;

const int S24_DISKSIZE1 = S24_TRACKS * S24_SIDES * S24_TRACKSIZE1;	// 1925120
const int S24_DISKSIZE2 = S24_TRACKS * S24_SIDES * S24_TRACKSIZE2;	// 1843200


bool ReadS24 (MemFile &file, std::shared_ptr<Disk> &disk)
{
	if (!file.rewind() || (file.size() != S24_DISKSIZE1 && file.size() != S24_DISKSIZE2))
		return false;

	auto track_size = file.size() / (S24_TRACKS * S24_SIDES);
	MEMORY mem(track_size);

	// Sector sizes used for both formats
	static const uint8_t ab2d00[] = { 4, 4, 4, 4, 4, 3, 1 };
	static const uint8_t ab2f00[] = { 6, 3, 3, 3, 2, 1 };

	uint8_t sectors = (file.size() == S24_DISKSIZE1) ? arraysize(ab2d00) : arraysize(ab2f00);
	const uint8_t *pbSectors = (file.size() == S24_DISKSIZE1) ? ab2d00 : ab2f00;

	for (uint8_t head = 0; head < S24_SIDES; head++)
	{
		for (uint8_t cyl = 0; cyl < S24_TRACKS; cyl++)
		{
			if (!file.read(mem, track_size))
				throw util::exception("short file reading ", CylHead(cyl, head));

			Track track;
			uint8_t *pb = mem;

			for (uint8_t i = 0; i < sectors; i++)
			{
				Sector sector(DataRate::_500K, Encoding::MFM, Header(cyl, head, i + 1, pbSectors[i]));

				Data data(sector.size());
				memcpy(data.data(), pb, data.size());
				pb += data.size();

				sector.add(std::move(data));
				track.add(std::move(sector));
			}

			disk->write_track(CylHead(cyl, head), std::move(track));
		}
	}

	disk->strType = "S24";
	return true;
}
