// Magic Shadow Archiver for Atari ST
//  http://www.atari-wiki.com/?title=Magic_Shadow_Archiver

#include "SAMdisk.h"

#define MSA_SIGNATURE	"\x0e\x0f"
#define MSA_RLESTART	0xe5

typedef struct
{
	uint8_t abSig[2];			// should be 0x0e,0x0f
	uint8_t abSectors[2];		// MSB/LSB of sectors/track
	uint8_t abSides[2];			// MSB/LSB of sides-1
	uint8_t abStartTrack[2];	// MSB/LSB of start track (0-based)
	uint8_t abEndTrack[2];		// MSB/LSB of start track (0-based)
} MSA_HEADER;

typedef struct
{
	uint8_t abLength[2];		// LSB/MSB track length
} MSA_TRACK;

typedef struct
{
	uint8_t bMarker;			// RLE start marker (0xE5)
	uint8_t bFill;				// Repeated byte
	uint8_t abLength[2];		// Length of repeated block
} MSA_RLE;


bool ReadMSA (MemFile &file, std::shared_ptr<Disk> &disk)
{
	MSA_HEADER dh;
	if (!file.rewind() || !file.read(&dh, sizeof(dh)))
		return false;
	else if (memcmp(dh.abSig, MSA_SIGNATURE, sizeof(dh.abSig)))
		return false;
	else if (dh.abSectors[0] || dh.abSides[0] || dh.abStartTrack[0] || dh.abEndTrack[0])
		return false;
	else if (dh.abStartTrack[1] > dh.abEndTrack[1] || dh.abSides[1] >= MAX_SIDES || dh.abSectors[1] > MAX_SECTORS)
		return false;

	auto bSectors = dh.abSectors[1];
	auto bSides = dh.abSides[1] + 1;
	auto bStartTrack = dh.abStartTrack[1];
	auto bEndTrack = dh.abEndTrack[1];

	Format fmt { RegularFormat::AtariST };
	fmt.cyls = bEndTrack + 1;
	fmt.heads = bSides;
	fmt.sectors = bSectors;
	fmt.datarate = (fmt.track_size() < 6000) ? DataRate::_250K : DataRate::_500K;

	// ToDo: do we still need this? or is it applied by ReadImage()?
	fmt.Override();

	auto track_size = fmt.track_size();
	Data mem(track_size);
	Data mem2(track_size);

	for (uint8_t cyl = bStartTrack; cyl <= bEndTrack; ++cyl)
	{
		for (uint8_t head = 0; head < bSides; ++head)
		{
			CylHead cylhead(cyl, head);

			MSA_TRACK dt;
			if (!file.read(&dt, sizeof(dt)))
				throw util::exception("short file reading ", cylhead, " header");

			uint16_t wLength = (dt.abLength[0] << 8) | dt.abLength[1];
			if (!wLength || wLength > track_size)
				throw util::exception("invalid track length (", wLength, ") on ", cylhead);
			else if (wLength == track_size)
			{
				// Read uncompressed track data
				if (!file.read(mem))
					throw util::exception("short file reading raw data for ", cylhead);
			}
			else // Compressed track
			{
				mem.clear();

				if (!file.read(mem2.data(), wLength))
					throw util::exception("short file reading compressed data for ", cylhead);

				uint8_t *pb = mem2.data();

				while (wLength)
				{
					// Not run-byte?
					if (*pb != MSA_RLESTART)
					{
						mem.push_back(*pb++);
						wLength--;
					}
					else if (wLength < 4)
						throw util::exception("invalid RLE block on ", cylhead);
					else
					{
						// RLE block to expand
						auto prle = reinterpret_cast<MSA_RLE*>(pb);
						uint8_t fill = prle->bFill;
						auto wLen = (prle->abLength[0] << 8) | prle->abLength[1];

						// Ensure the length is non-zero and fits
						if (!wLen || mem.size() + wLen > track_size)
							throw util::exception("invalid RLE data on ", cylhead);

						// Write the uncompressed block
						while (wLen--)
							mem.push_back(fill);

						pb += sizeof(MSA_RLE);
						wLength -= sizeof(MSA_RLE);
					}
				}

				// Ensure we've expanded the exact track size
				if (mem.size() != track_size)
					throw util::exception("expanded data doesn't match track size on ", cylhead);
			}

			Track track;
			track.format(cylhead, fmt);
			track.populate(mem.begin(), mem.end());
			disk->write(cylhead, std::move(track));
		}
	}

	disk->fmt = fmt;
	disk->strType = "MSA";

	return true;
}
