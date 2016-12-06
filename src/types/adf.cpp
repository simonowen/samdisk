// AmigaDOS format
//
//  http://lclevy.free.fr/adflib/adf_info.html

#include "SAMdisk.h"

#define ADF_SECTOR_SIZE			512
#define ADF_BOOTBLOCK_SIZE		(ADF_SECTOR_SIZE * 2)	// bootblock is 2 sectors

typedef struct
{
	uint32_t disk_type;		// "DOS"+flags
	uint32_t checksum;		// calculated so bootblock checksum is ~0
	uint32_t rootblock;		// 880 for both DD and HD disks
} ADF_BOOTBLOCK;


bool ReadADF (MemFile &file, std::shared_ptr<Disk> &disk)
{
	Format fmtDD { RegularFormat::AmigaDOS };
	Format fmtHD { RegularFormat::AmigaDOSHD };

	if (file.size() != fmtDD.disk_size() && file.size() != fmtHD.disk_size())
		return false;

	uint8_t bootblock[ADF_BOOTBLOCK_SIZE];
	auto pbb = reinterpret_cast<const ADF_BOOTBLOCK*>(bootblock);

	if (!file.seek(0) || !file.read(&bootblock, sizeof(bootblock)))
		return false;

	// Check for AmigaDOS signature
	if ((util::betoh(pbb->disk_type) & 0xffffff00) != 0x444f5300)
		return false;

	uint32_t checksum = 0;
	for (size_t i = 0; i < sizeof(bootblock); i += sizeof(uint32_t))
	{
		auto prev = checksum;

		checksum += util::betoh(*reinterpret_cast<uint32_t*>(bootblock + i));
		if (checksum < prev)
			checksum++;
	}

	if (~checksum)
		Message(msgWarning, "invalid AmigaDOS root block checksum");;

	file.rewind();
	disk->format(file.size() == fmtDD.disk_size() ? fmtDD : fmtHD, file.data(), true);
	disk->strType = "ADF";

	return true;
}

bool WriteADF (FILE* f_, std::shared_ptr<Disk> &disk)
{
	auto &sector0 = disk->get_sector(Header(0, 0, 0, 2));

	Format fmt = (sector0.datarate != DataRate::_500K) ?
		RegularFormat::AmigaDOS : RegularFormat::AmigaDOSHD;

	return WriteRegularDisk(f_, *disk, fmt);
}
