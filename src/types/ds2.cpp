// Velesoft's split-side transfer format for the SAM Coupe:
//  http://velesoft.speccy.cz/atom_hdd-cz.htm

#include "SAMdisk.h"

bool ReadDS2 (MemFile &file, std::shared_ptr<Disk> &disk)
{
	std::string path = file.path();
	MemFile file2;
	MemFile *pfileHead0 = &file;
	MemFile *pfileHead1 = &file2;

	Format fmt { RegularFormat::MGT };

	// The input should be half a standard SAM disk in size
	if (!file.rewind() || file.size() != fmt.disk_size() / 2)
		return false;

	// If we've been given the main file, open the second side
	if (IsFileExt(path, "dsk"))
	{
		// DSK->DS2
		size_t offset = path.length() - 1;
		path[offset] = '2';

		if (!file2.open(path))
			throw util::exception("missing .ds2 companion file");
	}
	// Or if given the data file, switch to the header
	else if (IsFileExt(path, "ds2"))
	{
		// DS2->DSK (preserving case from 2nd char)
		size_t offset = path.length() - 1;
		path[offset] = 'K' | (path[offset - 1] & 0x20);

		if (!file2.open(path))
			throw util::exception("missing .dsk companion file");

		std::swap(pfileHead0, pfileHead1);
	}
	// Reject other file extensions
	else
		return false;

	// The companion file should also contain a single side
	if (file2.size() != fmt.disk_size() / 2)
		throw util::exception(path, " file size is incorrect");

	// Join the sides
	Data data(pfileHead0->data().begin(), pfileHead0->data().end());
	data.insert(data.end(), pfileHead1->data().begin(), pfileHead1->data().end());

	disk->format(fmt, data, true);
	disk->strType = "DS2";

	return true;
}
