// BIOS Parameter Block, for MS-DOS and compatible disks

#include "SAMdisk.h"

typedef struct
{
	uint8_t abJump[3];				// x86 jump instruction, either 0xeb or 0xe9
	uint8_t bOemName[8];			// OEM string

	uint8_t abBytesPerSec[2];		// bytes per sector
	uint8_t bSecPerClust;			// sectors per cluster
	uint8_t abResSectors[2];		// number of reserved sectors
	uint8_t bFATs;					// number of FATs
	uint8_t abRootDirEnts[2];		// number of root directory entries
	uint8_t abSectors[2];			// total number of sectors
	uint8_t bMedia;					// media descriptor
	uint8_t abFATSecs[2];			// number of sectors per FAT
	uint8_t abSecPerTrack[2];		// sectors per track
	uint8_t abHeads[2];				// number of heads
	uint8_t abHiddenSecs[4];		// number of hidden sectors
	uint8_t abLargeSecs[4];			// number of large sectors
	// extended fields below
	uint8_t abLargeSectorsPerFat[4];
	uint8_t abFlags[2];
	uint8_t abFsVersion[2];
	uint8_t abRootDirFirstCluster[4];
	uint8_t abFsInfoSector[2];
	uint8_t BackupBootSector[2];
	uint8_t abReserved[12];
} BIOS_PARAMETER_BLOCK;


static bool IsValidBPB (BIOS_PARAMETER_BLOCK &bpb)
{
	// Check for known jump-to-bootstrap code patterns
//	if ((bpb.abJump[0] != 0xeb || bpb.abJump[2] != 0x90) && bpb.abJump[0] != 0xe9)
//		return false;

	// Check some basic BPB entries
	if (!bpb.bFATs || bpb.bFATs > 2 || !bpb.abResSectors[0] || !bpb.abRootDirEnts[0])
		return false;

	// Check for a sensible media byte
	if (bpb.bMedia != 0xf0 && bpb.bMedia < 0xf8)
		return false;

	if (!bpb.bSecPerClust)
	{
//		Message(msgWarning, "sectors-per-cluster missing, assuming 2 sectors");
//		mpoke(f_, offsetof(BOOT_SECTOR, bSecPerClust), 2);
	}

/*
	// Only allow "true" DOS disks to be short?
	if ((bpb.abJump[0] != 0xeb || bpb.abJump[2] != 0x90) && bpb.abJump[0] != 0xe9 && msize(f_) < (uSectorSize * uTotalSectors))
		return false;
*/

	return true;
}


bool ReadBPB (MemFile &file, std::shared_ptr<Disk> &disk)
{
	BIOS_PARAMETER_BLOCK bpb;

	if (!file.rewind() || !file.read(&bpb, sizeof(bpb)))
		return false;

	if (!IsValidBPB(bpb))
		return false;

	// Extract the full geometry
	auto total_sectors = bpb.abSectors[0] | (bpb.abSectors[1] << 8);
	auto sector_size = bpb.abBytesPerSec[0] | (bpb.abBytesPerSec[1] << 8);
	auto sectors = bpb.abSecPerTrack[0] | (bpb.abSecPerTrack[1] << 8);
	auto heads = bpb.abHeads[0] | (bpb.abHeads[1] << 8);
	auto cyls = (sectors && heads) ? (total_sectors / (sectors * heads)) : 0;

	Format fmt { RegularFormat::PC720 };
	fmt.cyls = static_cast<uint8_t>(cyls);
	fmt.heads = static_cast<uint8_t>(heads);
	fmt.sectors = static_cast<uint8_t>(sectors);
	fmt.size = SizeToCode(sector_size);

	ValidateGeometry(fmt);

	if (fmt.track_size() < 6000)
		fmt.datarate = DataRate::_250K;
	else if (fmt.track_size() < 12000)
		fmt.datarate = DataRate::_500K;
	else
		fmt.datarate = DataRate::_1M;

	// Reject disks larger than geometry suggests, but accept space-saver truncated images
	if (file.size() > fmt.disk_size())
		return false;

	file.rewind();
	disk->format(fmt, file.data());
	disk->strType = "BPB";

	return true;
}
