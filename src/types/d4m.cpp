// CMD FD-2000 (ED):
//  http://www.unusedino.de/ec64/technical/formats/d2m-dnp.html

#include "SAMdisk.h"

const int D4M_TRACKS = 81;
const int D4M_SIDES = 2;
const int D4M_SECTORS = 20;

const int D4M_SECTOR_SIZE = 1024;
const int D4M_TRACK_SIZE = D4M_SECTORS * D4M_SECTOR_SIZE;
const int D4M_DISK_SIZE = D4M_TRACKS * D4M_SIDES * D4M_TRACK_SIZE;

const int D4M_PARTITION_OFFSET = D4M_DISK_SIZE - (2 * D4M_TRACK_SIZE) + (2 * D4M_SECTOR_SIZE);	// Partition table start


bool ReadD4M (MemFile &file, std::shared_ptr<Disk> &disk)
{
	// D4M is a fixed-size image
	uint8_t ab[256];
	if (file.size() != D4M_DISK_SIZE || !file.seek(D4M_PARTITION_OFFSET) || !file.read(&ab, sizeof(ab)))
		return false;

	// Check a partition starts at track 1 sector 1, with "SYSTEM" name
	if (memcmp(ab, "\x01\x01", 2) || memcmp(ab + 5, "SYSTEM\xa0\xa0\xa0\xa0\xa0\xa0\xa0\xa0\xa0\xa0", 16))
		return false;

	Format fmt { RegularFormat::D4M };

	// D2M stores the sides in the reverse order, so fiddle things to read it easily
	file.rewind();
	std::swap(fmt.head0, fmt.head1);
	disk->format(fmt, file.data());
	std::swap(disk->fmt.head0, disk->fmt.head1);
	disk->flip_sides();
	disk->strType = "D4M";

	return true;
}

bool WriteD4M (FILE* /*f_*/, std::shared_ptr<Disk> &/*disk*/)
{
	throw std::logic_error("D4M writing not implemented");
#if 0
	auto missing = 0;
	bool f = true;

	PCFORMAT pf_ = &fmtD4M;
	auto cyls = D4M_TRACKS;
	auto heads = D4M_SIDES;

	MEMORY mem(D4M_TRACK_SIZE);

	for (auto cyl = 0; f && cyl < cyls; cyl++)
	{
		for (auto head = heads - 1; f && head >= 0; head--)
		{
			missing += pd_->ReadRegularTrack(cyl, head, pf_, mem);
			f = (fwrite(mem, D4M_SECTOR_SIZE, pf_->sectors, f_) == pf_->sectors);
		}
	}

	if (!f)
		throw util::exception("write error");

	if (missing)
		Message(msgWarning, "source missing %d/%d sectors", missing, cyls * heads * pf_->sectors);

	return true;
#endif
}
