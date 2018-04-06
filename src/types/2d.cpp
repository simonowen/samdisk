// 2D is a raw format used by PC-88 systems

#include "SAMdisk.h"

bool Read2D (MemFile &file, std::shared_ptr<Disk> &disk)
{
	Format fmt { RegularFormat::_2D };

	if (!IsFileExt(file.name(), "2d") || file.size() != fmt.disk_size())
		return false;

	file.rewind();
	disk->format(fmt, file.data());
	disk->strType = "2D";

	return true;
}

bool Write2D (FILE* /*f_*/, std::shared_ptr<Disk> &/*disk*/)
{
	throw std::logic_error("2D writing not implemented");
}
