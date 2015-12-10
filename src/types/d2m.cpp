// CMD FD-2000 (HD):
//  http://www.unusedino.de/ec64/technical/formats/d2m-dnp.html

#include "SAMdisk.h"

const int D2M_TRACKS = 81;
const int D2M_SIDES = 2;
const int D2M_SECTORS = 10;

const int D2M_SECTOR_SIZE = 1024;
const int D2M_TRACK_SIZE = D2M_SECTORS * D2M_SECTOR_SIZE;
const int D2M_DISK_SIZE = D2M_TRACKS * D2M_SIDES * D2M_TRACK_SIZE;

const int D2M_PARTITION_OFFSET = D2M_DISK_SIZE - (2 * D2M_TRACK_SIZE) + (2 * D2M_SECTOR_SIZE);	// Partition table start


bool ReadD2M (MemFile &file, std::shared_ptr<Disk> &disk)
{
	// D2M is a fixed-size image
	uint8_t ab[256];
	if (file.size() != D2M_DISK_SIZE || !file.seek(D2M_PARTITION_OFFSET) || !file.read(&ab, sizeof(ab)))
		return false;

	// Check a partition starts at track 1 sector 1, with "SYSTEM" name
	if (memcmp(ab, "\x01\x01", 2) || memcmp(ab + 5, "SYSTEM\xa0\xa0\xa0\xa0\xa0\xa0\xa0\xa0\xa0\xa0", 16))
		return false;

	Format fmt { RegularFormat::D2M };

	// D2M stores the sides in the reverse order, so fiddle things to read it easily
	file.rewind();
	std::swap(fmt.head0, fmt.head1);
	disk->format(fmt, file.data());
	std::swap(disk->fmt.head0, disk->fmt.head1);
	disk->flip_sides();
	disk->strType = "D2M";

	return true;
}

bool WriteD2M (FILE* /*f_*/, std::shared_ptr<Disk> &/*disk*/)
{
	throw std::logic_error("not implemented");
#if 0
	auto missing = 0;
	bool f = true;

	PCFORMAT pf_ = &fmtD2M;
	auto cyls = D2M_TRACKS;
	auto heads = D2M_SIDES;

	MEMORY mem(D2M_TRACK_SIZE);

	for (auto cyl = 0; f && cyl < cyls; cyl++)
	{
		for (auto head = heads - 1; f && head >= 0; head--)
		{
			missing += pd_->ReadRegularTrack(cyl, head, pf_, mem);
			f = (fwrite(mem, D2M_SECTOR_SIZE, pf_->sectors, f_) == pf_->sectors);
		}
	}

	if (!f)
		throw util::exception("write error");

	if (missing)
		Message(msgWarning, "source missing %d/%d sectors", missing, cyls * heads * pf_->sectors);

	return true;
#endif
}
