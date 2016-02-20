#ifndef DISKUTIL_H
#define DISKUTIL_H

const int MAX_SIDES = 2;
const int MAX_TRACKS = 128;			// Internal format maximum, needed for 1MB TRD
const int MAX_SECTORS = 144;

const int MIN_SECTOR_SIZE = 128;


struct ScanContext
{
	CylHead last_cylhead {};
	Sector sector { DataRate::Unknown, Encoding::Unknown, Header(0, 0, 1, 2) };
	int sectors = 0;
	int gap3 = 0;
	bool custom_cyl = false;
	bool custom_head = false;
	bool warned = false;
};


const int DUMP_OFFSETS = 1;
const int DUMP_DIFF = 2;

void DumpTrack (const CylHead &cylhead, const Track &track, const ScanContext &context, int flags = 0);
void NormaliseTrack (const CylHead &cylhead, Track &track);

Format GetFormat (RegularFormat reg_fmt);
bool SizeToFormat (int64_t size, Format &fmt);
void OverrideFormat (Format &format, bool full_control = false);

std::vector<std::pair<char, size_t>> DiffSectorCopies (const Sector &sector);

Sector GetTypicalSector (const CylHead &cylhead, const Track &track, Sector last_sector);

bool WriteRegularDisk (FILE *f_, Disk &disk, const Format &format);

#endif // DISKUTIL_H
