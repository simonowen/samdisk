// HP9114 LIF disk image:
//  http://www.hpmuseum.org/cgi-sys/cgiwrap/hpmuseum/articles.cgi?read=260

#include "SAMdisk.h"

bool ReadLIF (MemFile &file, std::shared_ptr<Disk> &disk)
{
	Format fmt { RegularFormat::LIF };

	// For now, rely on the file size and extension
	if (file.size() != fmt.disk_size() || !IsFileExt(file.name(), "lif"))
		return false;

	file.rewind();
	disk->format(fmt, file.data());
	disk->strType = "LIF";

	return true;
}

bool WriteLIF (FILE* f_, std::shared_ptr<Disk> &disk)
{
	return WriteRegularDisk(f_, *disk, RegularFormat::LIF);
}
