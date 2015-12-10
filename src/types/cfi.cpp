// Amstrad FDCOPY "compressed floppy image":
//  https://web.archive.org/web/20100706002713/http://www.fdos.org/ripcord/rawrite/cfi.html

#include "SAMdisk.h"
#include "types.h"

bool ReadCFI (MemFile &file, std::shared_ptr<Disk> &disk)
{
	uint8_t abLen[2];

	if (!IsFileExt(file.name(), "cfi") || !file.rewind())
		return false;

	// Check the image has a complete number of track blocks
	for (auto track = 0; file.read(abLen, sizeof(abLen), 1); ++track)
	{
		auto track_len = (abLen[1] << 8) | abLen[0];

		if (!file.seek(file.tell() + track_len))
			throw util::exception("short file in CFI track block ", track);
	}

	MEMORY mem(NORMAL_TRACKS * NORMAL_SIDES * NORMAL_SIDES * 36 * SECTOR_SIZE);	// 2.88M
	uint8_t *pb = mem;

	file.rewind();

	// Read the next track length until we run out of data
	while (file.read(abLen, sizeof(abLen), 1))
	{
		auto track_len = (abLen[1] << 8) | abLen[0];
		auto track_end = file.tell() + track_len;

		while (file.tell() < track_end)
		{
			if (!file.read(abLen, sizeof(abLen)))
				throw util::exception("short file reading CFI track block");

			uint16_t wLen = ((abLen[1] & 0x7f) << 8) | abLen[0];

			if (pb + wLen >= mem + mem.size)
				throw util::exception("expanded CFI image is too big");

			// RLE block?
			if (abLen[1] & 0x80)
			{
				uint8_t bFill;
				if (!file.read(&bFill, sizeof(bFill)))
					throw util::exception("short file expanding CFI RLE block");

				memset(pb, bFill, wLen);
			}
			else
			{
				if (!file.read(pb, wLen))
					throw util::exception("short file reading uncompressed CFI block");
			}

			pb += wLen;
		}

		if (file.tell() != track_end)
			throw util::exception("track data overflows CFI track block");
	}

	// Open the uncompressed data
	file.open(mem, static_cast<int>(pb - mem.pb), "packed.cfi");

	if (!ReadBPB(file, disk))
		throw util::exception("invalid packed CFI content");

	disk->strType = "CFI";
	return true;
}
