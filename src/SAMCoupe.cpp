// SAM Coupe helper functions

#include "SAMdisk.h"
#include "SAMCoupe.h"

MGT_DISK_INFO *GetDiskInfo (const uint8_t *p, MGT_DISK_INFO &di)
{
	auto dir = reinterpret_cast<const MGT_DIR *>(p);

	// Assume 4 directory tracks until we discover otherwise
	di.dir_tracks = MGT_DIR_TRACKS;

	// Determine the disk/DOS type, as each has different capabilities
	if (!memcmp(dir->abBDOS, "BDOS", 4))
		di.dos_type = SamDosType::BDOS;
	else if (dir->abLabel[0] != 0x00 && dir->abLabel[0] != 0xff)
		di.dos_type = SamDosType::MasterDOS;
	else
		di.dos_type = SamDosType::SAMDOS;

	// Extract the remaining disk information from type-specific locations
	switch (di.dos_type)
	{
		case SamDosType::SAMDOS:
			// Nothing more to do for SAMDOS
			break;

		case SamDosType::MasterDOS:
			// Add on any extra directory tracks
			di.dir_tracks += dir->extra.bExtraDirTracks;

			// Ensure the track count is legal for MasterDOS
			if (di.dir_tracks < MGT_DIR_TRACKS)
				di.dir_tracks = MGT_DIR_TRACKS;
			else if (di.dir_tracks > 39)
				di.dir_tracks = 39;

			// 16-bit random value used as a disk serial number
			di.serial_number = (dir->extra.abSerial[0] << 8) | dir->extra.abSerial[1];

			// MasterDOS uses '*' for 'no label'
			if (dir->abLabel[0] != '*')
			{
				di.disk_label = std::string(reinterpret_cast<const char *>(dir->abLabel), sizeof(dir->abLabel));
				di.disk_label = util::trim(di.disk_label);
			}
			break;

		case SamDosType::BDOS:
			// BDOS uses a null for 'no label'
			if (dir->abLabel[0])
			{
				di.disk_label = std::string(reinterpret_cast<const char *>(&dir->abLabel), sizeof(dir->abLabel));
				di.disk_label += std::string(reinterpret_cast<const char *>(&dir->extra.bDirTag), sizeof(dir->szLabelExtra));
				di.disk_label = util::trim(di.disk_label);
			}
			break;
	}

	return &di;
}

bool SetDiskInfo (uint8_t pb_, MGT_DISK_INFO &di)
{
	auto p = reinterpret_cast<MGT_DIR *>(pb_);
	auto modified = false;

	// Extract the remaining disk information from type-specific locations
	switch (di.dos_type)
	{
		case SamDosType::MasterDOS:
			// Set serial number
			p->extra.abSerial[0] = static_cast<uint8_t>(di.serial_number >> 8);
			p->extra.abSerial[1] = static_cast<uint8_t>(di.serial_number);

			// No label?
			if (di.disk_label.empty())
			{
				p->abLabel[0] = '*';
				memset(p->abLabel + 1, ' ', sizeof(p->abLabel) - 1);
			}
			else
			{
				std::string strPaddedLabel = di.disk_label + std::string(sizeof(p->abLabel), ' ');
				memcpy(p->abLabel, strPaddedLabel.data(), sizeof(p->abLabel));
			}
			break;

		case SamDosType::SAMDOS:
			// Leave as SAMDOS format if there's no label, or we're not allowed to write a BDOS signature
			if (di.disk_label.empty() || opt.fix == 0 || opt.nosig)
				break;

			// Convert to BDOS format
			memcpy(p->abBDOS, "BDOS", 4);
			di.dos_type = SamDosType::BDOS;
			modified = true;

			// fall through...

		case SamDosType::BDOS:
		{
			// Form space-padded version of the label
			std::string strPaddedLabel = di.disk_label + std::string(BDOS_LABEL_SIZE, ' ');

			// Split across the two locations used
			memcpy(p->abLabel, strPaddedLabel.data(), sizeof(p->abLabel));
			memcpy(&p->extra.bDirTag, strPaddedLabel.data() + sizeof(p->abLabel), 6);	// last 6 characters
			break;
		}
	}

	return modified;
}


bool GetFileTime (const MGT_DIR *p, struct tm* ptm_)
{
	memset(ptm_, 0, sizeof(*ptm_));

	// Check for a packed BDOS 1.4+ date
	if (p->bMonth & 0x80)
	{
		ptm_->tm_sec = (p->bMinute & 0x1f) << 1;
		ptm_->tm_min = ((p->bMinute & 0xe0) >> 2) | (p->bHour & 0x07);
		ptm_->tm_hour = (p->bHour & 0xf8) >> 3;
//		ptm_->tm_wday =   p->bMonth & 0x07;			// mktime does this for us
		ptm_->tm_mday = p->bDay;
		ptm_->tm_mon = (p->bMonth & 0x71) >> 3;
		ptm_->tm_year = p->bYear;
	}

	// Normal format
	else
	{
		ptm_->tm_min = p->bMinute;
		ptm_->tm_hour = p->bHour;
		ptm_->tm_mday = p->bDay;
		ptm_->tm_mon = p->bMonth - 1;
		ptm_->tm_year = p->bYear;
	}

	// Base smaller year values from 2000 (BDOS considers <80 to be invalid)
	if (ptm_->tm_year < 80)
		ptm_->tm_year += 100;

	// Fetch the current year, if not already cached
	static int nYear = 0;
	if (!nYear)
	{
		time_t ttNow = time(nullptr);
		struct tm *tmNow = localtime(&ttNow);
		nYear = 1900 + tmNow->tm_year;
	}

	// Reject obviously invalid date components
	if (1900 + ptm_->tm_year > nYear ||
		ptm_->tm_mon < 0 || ptm_->tm_mon >= 12 ||
		ptm_->tm_mday == 0 || ptm_->tm_mday >  31 ||
		ptm_->tm_hour > 23 || ptm_->tm_min > 60 || ptm_->tm_sec > 60)
		return false;

	  // All dates are taken to be in the local timezone, not UCT/GMT
	ptm_->tm_isdst = -1;

	// Validate the date components
	return mktime(ptm_) != -1;
}


bool IsSDIDEDisk (const HDD &hdd)
{
	return CheckSig(hdd, 1, 14, "Free_space");
}

bool IsProDOSDisk (const HDD &hdd, PRODOS_CAPS &pdc)
{
/*
	Sector 0    - MS-DOS BOOT sector
	Sector 1    - MS-DOS FAT
	Sector 2    - MS-DOS ROOT directory
	Sector 3    - Small "READ-ME.TXT" file visible to Windows/MS-DOS Etc.
	Sector 4-67 - Pro-DOS system code (if the card was formatted as "bootable")
	Sector 68   - CP/M "Disk" ONE start
	Sector 2116 - CP/M "Disk" TWO start
	Sector 4164 - CP/M "Disk" THREE start ... and so on.
*/
	// Sector size must be 512 bytes for PRODOS
	if (hdd.sector_size != PRODOS_SECTOR_SIZE)
		return false;

	pdc.base_sectors = PRODOS_BASE_SECTORS;
	pdc.records = static_cast<int>((hdd.total_sectors - pdc.base_sectors) / PRODOS_RECORD_SECTORS);
	pdc.bootable = false;

	return CheckSig(hdd, 1, 16, "\x00\x03\x08\x06\x08\x13\x00\x0B\x1E\x11\x04\x00\x0B\x08\x13\x18", 16);
}

bool IsBDOSDisk (const HDD &hdd, BDOS_CAPS &bdc)
{
	bool f = false;

	// Sector size must be 512 bytes for BDOS
	if (hdd.sector_size != BDOS_SECTOR_SIZE)
		return false;

	MEMORY mem(hdd.sector_size);

	// Determine BDOS parameters from the CHS geometry (as BDOS does)
	GetBDOSCaps(hdd.cyls * hdd.heads * hdd.sectors, bdc);

	// Read the first sector in record 1 to check for a BDOS signature
	if (ReadSector(hdd, bdc.base_sectors, mem))
	{
		f = bdc.need_byteswap = !memcmp(mem + 232, "DBSO", 4);
		if (!f) f = !memcmp(mem + 232, "BDOS", 4);
	}

	// If that didn't work, try record 1 using LBA sector count
	if (!f)
	{
		// Determine the BDOS parameters from the LBA sector count (as Trinity does, and SAMdisk used to)
		BDOS_CAPS bdcLBA;
		GetBDOSCaps(hdd.total_sectors, bdcLBA);

		// Only check the base sector if it differs from the CHS position
		if (bdcLBA.base_sectors != bdc.base_sectors && ReadSector(hdd, bdcLBA.base_sectors, mem))
		{
			f = bdcLBA.need_byteswap = !memcmp(mem + 232, "DBSO", 4);
			if (!f) f = !memcmp(mem + 232, "BDOS", 4);
			bdcLBA.lba = f;

			// If the disk is relying on the LBA sector count, use the new details
			if (f)
				bdc = bdcLBA;
		}
	}

	// If we're still not sure, check for a BDOS boot sector
	if (!f && ReadSector(hdd, 0, mem))
	{
		// Clear bits 7 and 5 (case) for the boot signature check
		for (int i = 0; i < 4; ++i) { mem[i + 0x000] &= ~0xa0; mem[i + 0x100] &= ~0xa0; }

		// Check for the boot sector signatures at the appropriate offsets for Atom and Atom Lite
		bdc.need_byteswap = !memcmp(mem + 0x000, "OBTO", 4);
		bdc.bootable = bdc.need_byteswap || !memcmp(mem + 0x100, "BOOT", 4);
		f = bdc.bootable;
	}

	return f;
}

bool UpdateBDOSBootSector (uint8_t *pb_, const HDD &hdd)
{
	// Check for a compatible boot sector version
	if (memcmp(pb_ + 3, "ALB2", 4))
		return false;

	BDOS_CAPS bdc;
	GetBDOSCaps(hdd.total_sectors, bdc);

/*
4:
 Name:      Generic Flash HS-CF
 Capacity:  1025136 sectors (524MB)
 Geometry:  1017 Cyls, 16 Heads, 63 Sectors
 Format:    Atom Lite, 641 records, bootable
 Volumes:   B:

0100  42 4F 4F 54 16 00 00 E0 01 06 01 04 B7 80 07 D0  BOOT............
0110  D0 44 02 20 9F 05 17 40 00 10 00 00 F9 03 F0 03  .D. ...@........
0120  70 A4 0F 16 00 81 02 01 00 00 01 00 01 00 00 00  p...............
*/

	// Save a copy of the the boot sector before the update
	MEMORY mem(hdd.sector_size);
	memcpy(mem, pb_, mem.size);

	// Set the LBA location of the first record
	pb_[0x104] = bdc.base_sectors & 0xff;
	pb_[0x105] = (bdc.base_sectors >> 8) & 0xff;
	pb_[0x106] = (bdc.base_sectors >> 16) & 0xff;
	pb_[0x107] = ((bdc.base_sectors >> 24) & 0x1f) | 0xe0;	// merge in drive select (LBA)
/*
	// No splash screen sector,track
	pb_[0x108] = 0;
	pb_[0x109] = 0;
*/

	auto base_cyl = bdc.base_sectors / (hdd.heads * hdd.sectors);
	auto base_head = (bdc.base_sectors / hdd.sectors) % hdd.heads;
	auto base_sector = bdc.base_sectors % hdd.sectors;
	auto cyl_sectors = hdd.heads * hdd.sectors;
	auto total_sectors = hdd.cyls * hdd.heads * hdd.sectors;

	uint8_t *pbD0 = pb_ + 0x10e;	// offset of DVAR 0

	pbD0[8] = LOBYTE(1 + base_sector);			// 8  base sector for record
	pbD0[9] = LOBYTE(1 + hdd.sectors);		// 9  sectors per track +1
	pbD0[10] = LOBYTE(base_head);				// 10 base head
	pbD0[11] = LOBYTE(hdd.heads);			// 11 max heads
	pbD0[12] = LOBYTE(base_cyl);				// 12 base cylinder
	pbD0[13] = HIBYTE(base_cyl);
	pbD0[14] = LOBYTE(hdd.cyls);		// 14 cylinders
	pbD0[15] = HIBYTE(hdd.cyls);
	pbD0[16] = LOBYTE(cyl_sectors);				// 16 sectors * heads
	pbD0[17] = HIBYTE(cyl_sectors);
	pbD0[18] = LOBYTE(total_sectors);			// 18 total sectors of CF card
	pbD0[19] = HIBYTE(total_sectors);
	pbD0[20] = LOBYTE(total_sectors >> 16);
	pbD0[21] = LOBYTE(bdc.base_sectors);				// 21 base/record list sectors
	pbD0[22] = HIBYTE(bdc.base_sectors);

	// If anything changed, report the update
	if (memcmp(mem, pb_, mem.size))
	{
		Message(msgFix, "updated BDOS boot parameters to match target media");
		return true;
	}

	// No changes were made
	return false;
}

void GetBDOSCaps (int64_t sectors, BDOS_CAPS &bdc)
{
	if (sectors)
	{
		// Maximum BDOS sector count, to give 65535 records (53GB)
		auto bdos_sectors = std::min(static_cast<int>(sectors), 104858050);

		bdc.list_sectors = bdos_sectors / ((BDOS_SECTOR_SIZE / BDOS_LABEL_SIZE) * BDOS_RECORD_SECTORS) + 1;
		bdc.base_sectors = 1 + bdc.list_sectors;	// +1 for boot sector
		bdc.records = (bdos_sectors - bdc.base_sectors) / BDOS_RECORD_SECTORS;
		bdc.extra_sectors = bdos_sectors - (bdc.base_sectors + bdc.records * BDOS_RECORD_SECTORS);
		bdc.need_byteswap = false;
		bdc.bootable = false;
		bdc.lba = false;

		// If the final block has enough for directory plus 1 track, add it as a partial record
		if ((bdc.extra_sectors / MGT_SECTORS) >= (MGT_DIR_TRACKS + 1))
			++bdc.records;
	}
}

int FindBDOSRecord (HDD &hdd, const char *pcsz_, BDOS_CAPS &bdc)
{
	// No label means no match
	if (!*pcsz_)
		return 0;

	auto record = 0;
	char szFind[BDOS_LABEL_SIZE + 1] = {}, sz[BDOS_LABEL_SIZE + 1] = {};

	size_t nLen = strlen(pcsz_);
	memset(szFind, ' ', BDOS_LABEL_SIZE);
	memcpy(szFind, pcsz_, std::min(nLen, static_cast<size_t>(BDOS_LABEL_SIZE)));

	auto uRecordList = bdc.list_sectors * hdd.sector_size;
	MEMORY mem(uRecordList);

	if (hdd.Seek(1) && (hdd.Read(mem, bdc.list_sectors) == uRecordList))
	{
		uint8_t *pb = mem;

		for (auto i = 0; i < bdc.records; ++i, pb += BDOS_LABEL_SIZE)
		{
			memcpy(sz, pb, BDOS_LABEL_SIZE);

			if (!strcasecmp(szFind, sz))
			{
				record = i;
				break;
			}
		}
	}

	return record;
}
