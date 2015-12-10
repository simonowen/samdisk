#ifndef SAM_H
#define SAM_H

const int MGT_TRACKS = 80;
const int MGT_SIDES = 2;
const int MGT_SECTORS = 10;
const int MGT_TRACK_SIZE = MGT_SECTORS * SECTOR_SIZE;
const int MGT_DISK_SIZE = NORMAL_SIDES * NORMAL_TRACKS * MGT_TRACK_SIZE;
const int MGT_DISK_SECTORS = MGT_DISK_SIZE / SECTOR_SIZE;
const int MGT_DIR_TRACKS = 4;

// From SAM Technical Manual  (bType, wSize, wOffset, wUnused, bPages, bStartPage)
const int MGT_FILE_HEADER_SIZE = 9;

// Maximum size of a file that will fit on a SAM disk
const int MAX_SAM_FILE_SIZE = (MGT_TRACKS * MGT_SIDES - MGT_DIR_TRACKS) * MGT_SECTORS * (SECTOR_SIZE - 2) - MGT_FILE_HEADER_SIZE;


typedef struct
{
	uint8_t abUnk1;			// +210
	uint8_t abTypeZX;		// +211  ZX type byte
	uint8_t abZXLength[2];	// +212  ZX length
	uint8_t abZXStart[2];	// +214  ZX start address
	uint8_t abZXUnk2[2];	// +216  ?
	uint8_t abZXExec[2];	// +218  ZX autostart line number (BASIC) or address (code):
							//       BASIC line active if b15+b14 are clear
							//       CODE address active if not &0000 or &ffff
} DIR_ZX;

typedef struct
{
	uint8_t bDirTag;			// +250 Directory tag for directory entry; BDOS uses next 6 chars for disk name (fi$
	uint8_t bReserved;			// +251 Reserved
	uint8_t abSerial[2];		// +252 Random word for serial number (first entry only)
	uint8_t bDirCode;			// +254 Directory tag number for non-directory entries
	uint8_t bExtraDirTracks;	// +255 Number of directory tracks minus 4 (first entry only)
} DIR_EXTRA;

typedef struct
{
	uint8_t bType;				// +0   File type (b7=hidden, b6=protected)
	uint8_t abName[10];			// +1   Filename padded with spaces; first char is zero for unused

	uint8_t bSectorsHigh;		// +11  MSB of number of sectors used
	uint8_t bSectorsLow;		// +12  LSB of number of sectors used

	uint8_t bStartTrack;		// +13  Track number for start of file
	uint8_t bStartSector;		// +14  Sector number for start of file
	uint8_t abSectorMap[195];	// +15  Sector Address Map for file

	union
	{
		uint8_t abLabel[10];	// +210 +D/Disciple data, or disk label when in first entry (0|255 for none (SAMDOS/BDOS))
		DIR_ZX  zx;
	};
	uint8_t bFlags;				// +220 Flags (MGT use only) ; start of snapshot registers (22 bytes)
								//  iy, ix, de', bc', hl', af', de, bc, hl, iff1+I, sp, (af from stack)

	uint8_t abFileInfo[11];		// +221 File type information; first byte is mode-1 for screen$

	uint8_t abBDOS[4];			// +232 Spare; holds "BDOS" for first entry on BDOS disks

	uint8_t bStartPage;			// +236 Start page number in b4-b0 (b7-b5 are undefined)
	uint8_t bPageOffsetLow;		// +237 Start offset in range 32768-49151
	uint8_t bPageOffsetHigh;

	uint8_t bLengthInPages;		// +239 Number of pages in length
	uint8_t bLengthOffsetLow;	// +240 Length MOD 16384 (b15 and b14 are undefined)
	uint8_t bLengthOffsetHigh;

	uint8_t bExecutePage;		// +242 Execution page for CODE files, or 255 if no autorun
	uint8_t bExecAddrLow;		// +243 Execution offset (32768-49151) or BASIC autorun line (or 65535)
	uint8_t bExecAddrHigh;

	uint8_t bDay;				// +245 Day of save (1-31), or 255 for invalid/none
	uint8_t bMonth;				// +246 Month (1-12); bit 7 set for BDOS 1.7a+ format: b6-b3 month, b2-b0 day-of-week (0=Sun)
	uint8_t bYear;				// +247 Year (year-1900); invalid if 255; MasterDOS has Y2K problem and uses (year%100)
	uint8_t bHour;				// +248 Hour; new BDOS: b7-b3 hour, b2-b0 low 3 bits of minute
	uint8_t bMinute;			// +249 Minute; new BDOS: b7-b5 upper 3 bits of minute, b4-b0 seconds/2
	union
	{
		DIR_EXTRA extra;
		char szLabelExtra[6];
	};
} MGT_DIR;

static_assert(sizeof(MGT_DIR) == 256, "MGT_DIR size is wrong");

const int BDOS_LABEL_SIZE = 16;		// Many things expect this to be 16
const int BDOS_SECTOR_SIZE = 512;
const int BDOS_RECORD_SECTORS = MGT_DISK_SECTORS;


typedef struct
{
	int list_sectors = 0;		// Sectors reserved for the record list
	int base_sectors = 0;		// Sectors used by boot sector plus record list
	int records = 0;			// Number of records, including possible partial last record
	int extra_sectors = 0;		// Extra sectors: >= 40 will form a small final record)
	bool need_byteswap = false;	// True if this is an ATOM disk that needs byte-swapping
	bool bootable = false;		// True if the boot sector suggests the disk is bootable
	bool lba = false;			// True if disk needs LBA sector count instead of CHS
} BDOS_CAPS;


const int PRODOS_SECTOR_SIZE = 512;
const int PRODOS_BASE_SECTORS = 68;
const int PRODOS_RECORD_SECTORS = 2048;

typedef struct
{
	int base_sectors;	// Sectors used by boot sector plus record list
	int records;		// Number of records, including possible partial last record
	bool bootable;		// True if the boot sector suggests the disk is bootable
} PRODOS_CAPS;


enum class SamDosType { SAMDOS, MasterDOS, BDOS };

typedef struct
{
	SamDosType dos_type = SamDosType::SAMDOS;	// SAMDOS, MasterDOS or BDOS
	int dir_tracks = MGT_DIR_TRACKS;			// Number of tracks in directory listing (min=4, max=39)
	std::string	disk_label {};					// Up to BDOS_LABEL_SIZE (16) characters
	int serial_number = 0;						// Serial number (MasterDOS only)
} MGT_DISK_INFO;


MGT_DISK_INFO *GetDiskInfo (const uint8_t *p, MGT_DISK_INFO &di);
bool SetDiskInfo (uint8_t *pb_, MGT_DISK_INFO &di);
bool GetFileTime (const MGT_DIR *p, struct tm* ptm_);

bool IsSDIDEDisk (const HDD &hdd);
bool IsProDOSDisk (const HDD &hdd, PRODOS_CAPS &pdc);
bool IsBDOSDisk (const HDD &hdd, BDOS_CAPS &bdc);

bool UpdateBDOSBootSector(uint8_t *pb_, const HDD &hdd);
void GetBDOSCaps (int64_t sectors, BDOS_CAPS &bdc);
int FindBDOSRecord (const HDD &hdd, const std::string &path, BDOS_CAPS &bdc);

#endif // SAM_H
