#pragma once

// BIOS Parameter Block, for MS-DOS and compatible disks

struct BIOS_PARAMETER_BLOCK
{
    uint8_t abJump[3];              // usually x86 jump (0xeb or 0xe9)
    uint8_t bOemName[8];            // OEM string

    uint8_t abBytesPerSec[2];       // bytes per sector
    uint8_t bSecPerClust;           // sectors per cluster
    uint8_t abResSectors[2];        // number of reserved sectors
    uint8_t bFATs;                  // number of FATs
    uint8_t abRootDirEnts[2];       // number of root directory entries
    uint8_t abSectors[2];           // total number of sectors
    uint8_t bMedia;                 // media descriptor
    uint8_t abFATSecs[2];           // number of sectors per FAT
    uint8_t abSecPerTrack[2];       // sectors per track
    uint8_t abHeads[2];             // number of heads
    uint8_t abHiddenSecs[4];        // number of hidden sectors
    uint8_t abLargeSecs[4];         // number of large sectors
    // extended fields below
    uint8_t abLargeSectorsPerFat[4];
    uint8_t abFlags[2];
    uint8_t abFsVersion[2];
    uint8_t abRootDirFirstCluster[4];
    uint8_t abFsInfoSector[2];
    uint8_t BackupBootSector[2];
    uint8_t abReserved[12];
};

bool ReadBPB(MemFile& file, std::shared_ptr<Disk>& disk);
