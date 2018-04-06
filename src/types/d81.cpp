// CBM 1581:
//  http://unusedino.de/ec64/technical/formats/d81.html

#include "SAMdisk.h"

const int D81_TRACKS = 80;
const int D81_SIDES = 2;
const int D81_SECTORS = 10;

const int D81_SECTOR_SIZE = 512;
const int D81_TRACK_SIZE = D81_SECTORS * D81_SECTOR_SIZE;
const int D81_DISK_SIZE = D81_TRACKS * D81_SIDES * D81_TRACK_SIZE;

const int D81_HEADER_OFFSET = 0x61800;		// Sector 40/1 containing disk header


bool ReadD81 (MemFile &file, std::shared_ptr<Disk> &disk)
{
	// D81 is a fixed-size image
	uint8_t ab[256];
	if (file.size() != D81_DISK_SIZE || !file.seek(D81_HEADER_OFFSET) || !file.read(&ab, sizeof(ab)))
		return false;

	// Check for 1581 signature and various check bytes
	if (ab[2] != 'D' || ab[3] || ab[0x14] != 0xa0 || ab[0x15] != 0xa0 || ab[0x1b] != 0xa0 || ab[0x1c] != 0xa0)
		return false;

	Format fmt { RegularFormat::D81 };

	// D2M stores the sides in the reverse order, so fiddle things to read it easily
	file.rewind();
	std::swap(fmt.head0, fmt.head1);
	disk->format(fmt, file.data());
	std::swap(disk->fmt.head0, disk->fmt.head1);
	disk->flip_sides();
	disk->strType = "D81";

	return true;
}

bool WriteD81 (FILE* /*f_*/, std::shared_ptr<Disk> &/*disk*/)
{
	throw std::logic_error("D81 writing not implemented");
#if 0
	auto missing = 0;
	bool f = true;

	PCFORMAT pf_ = &fmtD81;
	auto cyls = D81_TRACKS;
	auto heads = D81_SIDES;

	MEMORY mem(D81_TRACK_SIZE);

	for (auto cyl = 0; f && cyl < cyls; cyl++)
	{
		for (auto head = heads - 1; f && head >= 0; head--)
		{
			missing += pd_->ReadRegularTrack(cyl, head, pf_, mem);
			f = (fwrite(mem, D81_SECTOR_SIZE, pf_->sectors, f_) == pf_->sectors);
		}
	}

	if (!f)
		throw util::exception("write error");

	if (missing)
		Message(msgWarning, "source missing %d/%d sectors", missing, cyls * heads * pf_->sectors);

	return true;
#endif
}
