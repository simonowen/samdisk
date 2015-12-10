// Miles Gordon Technology format for MGT +D and SAM Coupe disks:
//  http://www.worldofspectrum.org/faq/reference/formats.htm#MGT
//
// Also accepts IMG variant with different track order.

#include "SAMdisk.h"

bool ReadMGT (MemFile &file, std::shared_ptr<Disk> &disk)
{
	if (file.size() != MGT_DISK_SIZE)
		return false;

	// Check the first directory chain to test for an IMG image
	std::array<uint8_t, 2> buf;
	auto offset = MGT_DIR_TRACKS * MGT_TRACK_SIZE + SECTOR_SIZE - 2;
	bool img = file.seek(offset) && file.read(buf) && buf[0] == 0x04 && buf[1] == 0x02;

	file.rewind();
	disk->format(Format(RegularFormat::MGT), file.data(), img);
	disk->strType = img ? "IMG" : "MGT";

	return true;
}

bool WriteMGT (FILE* f_, std::shared_ptr<Disk> &disk)
{
	return WriteRegularDisk(f_, *disk, RegularFormat::MGT);
}
