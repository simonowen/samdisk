// Temporary SAM Disk File format, replaced by EDSK for custom format SAM disks

#include "SAMdisk.h"

const uint8_t SDF_CRC_ERROR = 0x08;
const uint8_t SDF_RECORD_NOT_FOUND = 0x10;
const uint8_t SDF_DELETED_DATA = 0x20;

const int SDF_TRACK_SIZE = 512 * 12;
const int SDF_SIDES = 2;
const int SDF_NORMAL_SIZE = SDF_SIDES * SDF_TRACK_SIZE * 80;
const int SDF_SIZE_81_TRACKS = SDF_SIDES * SDF_TRACK_SIZE * 81;
const int SDF_SIZE_82_TRACKS = SDF_SIDES * SDF_TRACK_SIZE * 82;
const int SDF_SIZE_83_TRACKS = SDF_SIDES * SDF_TRACK_SIZE * 83;

typedef struct
{
	uint8_t sectors;
} SDF_TRACK;

typedef struct
{
	uint8_t idstatus, datastatus;
	uint8_t cyl, head, sector, size;
	uint8_t crc1, crc2;
} SDF_SECTOR;


bool ReadSDF (MemFile &file, std::shared_ptr<Disk> &disk)
{
	file.rewind();

	if (file.size() != SDF_NORMAL_SIZE && file.size() != SDF_SIZE_81_TRACKS && file.size() != SDF_SIZE_82_TRACKS && file.size() != SDF_SIZE_83_TRACKS)
		return false;

	MEMORY mem(SDF_TRACK_SIZE);
	uint8_t cyls = static_cast<uint8_t>(file.size() / (SDF_TRACK_SIZE * SDF_SIDES));

	Range(cyls, SDF_SIDES).each([&] (const CylHead &cylhead) {
		if (!file.read(mem, SDF_TRACK_SIZE))
			throw util::exception("short file reading ", cylhead);

		auto pt = reinterpret_cast<const SDF_TRACK *>(mem.pb);
		auto ps = reinterpret_cast<const SDF_SECTOR *>(pt + 1);

		Track track(pt->sectors);

		for (auto i = 0; i < pt->sectors; ++i)
		{
			Sector sector(DataRate::_250K, Encoding::MFM, Header(ps->cyl, ps->head, ps->sector, ps->size));
			auto pd = reinterpret_cast<const uint8_t *>(ps + 1);

			bool id_crc_error = (ps->idstatus & SDF_CRC_ERROR) != 0;
			bool data_not_found = (ps->datastatus & SDF_RECORD_NOT_FOUND) != 0;
			bool deleted_dam = (ps->datastatus & SDF_DELETED_DATA) != 0;
			bool data_crc_error = (ps->datastatus & SDF_CRC_ERROR) != 0;

			if (id_crc_error)
				sector.set_badidcrc();
			else if (!data_not_found)
			{
				Data data(pd, pd + sector.size());
				sector.add(std::move(data), data_crc_error, deleted_dam ? 0xf8 : 0xfb);
			}

			ps = reinterpret_cast<const SDF_SECTOR *>(pd + sector.size());
			track.add(std::move(sector));
		}

		disk->write_track(cylhead, std::move(track));
	}, true);

	disk->strType = "SDF";

	return true;
}


#if 0
static bool UnpackSDF (const uint8_t *ps_, uint8_t *pd_, int /*len*/)	// ToDo: use len!
{
	const uint8_t *s;
	uint8_t *t, *p;
	int nTracks = 0;

	s = ps_;
	t = p = pd_;

	for (;;)
	{
		// No escape coming up so must be a data byte
		if (s[1] != 0xed)
			*t++ = *s++;

		// Repeated block escape?  <ED><ED><len><byte>
		else if (s[0] == 0xed)
		{
			// Expand repeated byte block
			memset(t, s[3], s[2]);
			t += s[2];
			s += 4;
		}

		// End of track marker?  <00><ED><ED><00>
		else if (s[2] == 0xed && !s[0] && !s[3])
		{
			// Chop off the extra null we included
			size_t len = t - p;
			s += 4;

			// Last block?
			if (!len)
				break;

			// Invalid track length?
			if (len > SDF_TRACK_SIZE)
				throw util::exception("invalid packed SDF data");

			// Pad up to the end of the track with zeros
			len = SDF_TRACK_SIZE - len;
			memset(t, 0x00, SDF_TRACK_SIZE - len);
			t = p += SDF_TRACK_SIZE;

			// Increment track count
			nTracks++;
		}

		// Regular data byte
		else
			*t++ = *s++;
	}

	return nTracks;
}
#endif

bool UnwrapSDF (std::shared_ptr<Disk> &/*src_disk*/, std::shared_ptr<Disk> &/*disk*/)
{
	throw std::logic_error("not implemented");
#if 0
	// Unpacking nested files is effectively a fix
	if (opt.fix == 0)
		return false;

	// Check for SPECIAL file in the first directory slot, with filename ending in _DATA
	PCSECTOR ps = disk->GetSector(0, 0, 1, &fmtMGT);
	if (!ps)
		return retOK;

	MGT_DIR *pdir = reinterpret_cast<MGT_DIR *>(ps->apbData[0]);
	if (pdir->bType != 8 || memcmp(&pdir->abName[3], "_DATA", 5) ||
		pdir->bSectorsHigh != 0x06 || pdir->bSectorsLow != 0x17)
		return retOK;

	MEMORY mem(MGT_DISK_SIZE - (MGT_DIR_TRACKS * MGT_TRACK_SIZE));
	auto pb = mem.pb;

	for (uint8_t head = 0; head < NORMAL_SIDES; head++)
	{
		for (uint8_t cyl = 0; cyl < NORMAL_TRACKS; cyl++)
		{
			if (!head && cyl < MGT_DIR_TRACKS)
				continue;

			if (disk->ReadRegularTrack(cyl, head, &fmtMGT, pb))
				return 0;

			pb += MGT_TRACK_SIZE;
		}
	}

	MEMORY mem2(SDF_SIZE_83_TRACKS * 2);
	int nTracks = UnpackSDF(mem, mem2, SDF_SIZE_83_TRACKS);


	if (nTracks != 80 * 2 && nTracks != 81 * 2 && nTracks != 82 * 2 && nTracks != 83 * 2)
		return retOK;

	MemFile file;
	file.open(mem2, nTracks * SDF_TRACK_SIZE, "packed.sdf");

	disk.reset();

	if (!ReadSDF(file, disk))
		throw util::exception("invalid packed SDF data");

	disk->strType = "SDF (packed)";
	return true;
#endif
}
