// Opus Discovery for Spectrum:
//  http://www.worldofspectrum.org/opus.html

#include "SAMdisk.h"
#include "opd.h"

const uint8_t OP_JR = 0x18;	// Z80 opcode for unconditional relative jump


bool ReadOPD (MemFile &file, std::shared_ptr<Disk> &disk)
{
	OPD_BOOT ob;
	if (!file.rewind() || !file.read(&ob, sizeof(ob)))
		return false;

	// Pick up the geometry details from the boot sector
	Format fmt { RegularFormat::OPD };
	fmt.cyls = ob.cyls;
	fmt.heads = (ob.flags & 0x10) ? 2 : 1;
	fmt.sectors = ob.sectors;
	fmt.size = ob.flags >> 6;

	// Require a correct file extension, or the JR opcode and exact file size
	if (!IsFileExt(file.name(), "opd") && !IsFileExt(file.name(), "opu") &&
		(ob.jr_boot[0] != OP_JR || file.size() != fmt.disk_size()))
		return false;

	fmt.Validate();

	file.rewind();
	disk->format(fmt, file.data(), true);
	disk->strType = "OPD";

	return true;
}

bool WriteOPD (FILE* f_, std::shared_ptr<Disk> &disk)
{
	Format fmt { RegularFormat::OPD };

	const Sector *ps = nullptr;
	if (!disk->find(Header(0, 0, fmt.base, fmt.size), ps) || ps->data_size() < static_cast<int>(sizeof(OPD_BOOT)))
		return false;

	auto pob = reinterpret_cast<const OPD_BOOT*>(ps->data_copy().data());

	fmt.cyls = pob->cyls;
	fmt.heads = (pob->flags & 0x10) ? 2 : 1;
	fmt.sectors = pob->sectors;
	fmt.size = pob->flags >> 6;

	fmt.Override(true);
	fmt.Validate();

	return WriteRegularDisk(f_, *disk, fmt);
}
