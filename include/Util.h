#ifndef UTIL_H
#define UTIL_H

#ifndef HIWORD
#define HIWORD(l)	static_cast<uint16_t>(static_cast<uint32_t>(l) >> 16)
#define LOWORD(l)	static_cast<uint16_t>(static_cast<uint32_t>(l))
#define HIBYTE(w)	static_cast<uint8_t>(static_cast<uint16_t>(w) >> 8)
#define LOBYTE(w)	static_cast<uint8_t>(static_cast<uint16_t>(w))
#endif


inline uint16_t tobe16 (uint16_t u16)
{
	return ((u16 & 0x00ff) << 8) | (u16 >> 8);
}

inline uint16_t frombe16 (uint16_t be16)
{
	return tobe16(be16);
}

inline uint32_t tobe32 (uint32_t u32)
{
	return ((u32 & 0xff) << 24) | ((u32 & 0xff00) << 8) | ((u32 & 0xff0000) >> 8) | ((u32 & 0xff000000) >> 24);
}

inline uint32_t frombe32 (uint32_t be)
{
	return tobe32(be);
}

template<size_t SIZE, typename T>
inline size_t array_size(T(&)[SIZE]) { return SIZE; }

#ifndef arraysize
template <typename T, size_t N>
char (&ArraySizeHelper(T (&array)[N]))[N];
#define arraysize(array) (sizeof(ArraySizeHelper(array)))
#endif

#define DAY ( \
  (__DATE__[4] == ' ' ? 0 : __DATE__[4]-'0') * 10 + \
  (__DATE__[5]-'0') \
)

#define MONTH ( \
  __DATE__[2] == 'n' ? (__DATE__[1] == 'a' ? 0 : 5) \
: __DATE__[2] == 'b' ? 1 \
: __DATE__[2] == 'r' ? (__DATE__[0] == 'M' ? 2 : 3) \
: __DATE__[2] == 'y' ? 4 \
: __DATE__[2] == 'l' ? 6 \
: __DATE__[2] == 'g' ? 7 \
: __DATE__[2] == 'p' ? 8 \
: __DATE__[2] == 't' ? 9 \
: __DATE__[2] == 'v' ? 10 : 11 \
)

#define YEAR ( \
  (__DATE__[7]-'0') * 1000 + \
  (__DATE__[8]-'0') * 100 + \
  (__DATE__[9]-'0') * 10 + \
   __DATE__[10]-'0' \
)


#define USECS_PER_MINUTE	60000000

//const int MIN_TRACK_OVERHEAD = 32;		// 32 bytes of 0x4e at the start of a track
//const int MIN_SECTOR_OVERHEAD = 95;		// 22+12+3+1+6+22+8+3+1+1+16

const int NORMAL_SIDES = 2;
const int NORMAL_TRACKS = 80;

const int DOS_SECTORS = 9;
const int DOS_TRACK_SIZE = DOS_SECTORS * SECTOR_SIZE;
const int DOS_DISK_SIZE = NORMAL_SIDES * NORMAL_TRACKS * DOS_TRACK_SIZE;

typedef struct
{
	uint8_t bLoadLow, bLoadHigh;		// Load address
	uint8_t bExecLow, bExecHigh;		// Execute address
	uint8_t bLengthLow, bLengthHigh;	// File length
	uint8_t bFlags;						// b0-b1: file start sector b8-b9
										// b2-b3: file load address b16-b17
										// b4-b5: file length b16-b17
										// b6-b7: file execution address b16-b17
	uint8_t bStartSector;				// File start sector b0-b7
}
DFS_DIR;

enum MsgType { msgStatus, msgInfo, msgFix, msgWarning, msgError };


const char *ValStr (int val, const char *pcszDec_, const char *pcszHex_, bool fForceDecimal_ = false);

const char *NumStr (int n);
const char *ByteStr (int b);
const char *WordStr (int w);

const char *CylStr (int cyl);
const char *HeadStr (int head);
const char *RecordStr (int record);
const char *SizeStr (int size);


const char *CH (int cyl, int head);
const char *CHS (int cyl, int head, int sector);
const char *CHR (int cyl, int head, int record);
const char *CHSR (int cyl, int head, int sector, int record);


template <typename ...Args>
void Message (MsgType type, const char* pcsz_, Args&& ...args)
{
	std::string msg = util::fmt(pcsz_, std::forward<Args>(args)...);

	if (type == msgError)
		throw util::exception(msg);

	switch (type)
	{
		case msgStatus:	 break;
		case msgInfo:	 util::cout << "Info: "; break;
		case msgFix:	 util::cout << colour::GREEN << "Fixed: "; break;
		case msgWarning: util::cout << colour::YELLOW << "Warning: "; break;
		case msgError:	 util::cout << colour::RED << "Error: "; break;
	}

	if (type == msgStatus)
		util::cout << ttycmd::statusbegin << "\r" << msg << ttycmd::statusend;
	else
		util::cout << msg << colour::none << '\n';
}

const char* LastError ();
bool Error(const char *pcsz_ = nullptr);

int GetMemoryPageSize ();

bool IsBlockDevice (const std::string &path);
bool IsFloppyDevice (const std::string &path);
bool GetMountedDevice (const char *pcszDrive_, char *pszDevice_, int cbDevice_);
/*
bool IsDriveRemoveable (HANDLE hDrive_);
bool IsHardDiskDevice (const char *pcsz_);
*/
bool CheckLibrary (const char *pcszLib_, const char *pcszFunc_);

bool IsFile (const std::string &path);
bool IsDir (const std::string &path);
bool IsFloppy (const std::string &path);
bool IsHddImage (const std::string &path);
bool IsBootSector (const std::string &path);
bool IsRecord (const std::string &path, int *pRecord = nullptr);
bool IsTrinity (const std::string &path);
bool IsBuiltIn (const std::string &path);

bool IsConsoleWindow ();

uint8_t *AllocMem (int len);
void FreeMem (void* pv_);

std::string FileExt (const std::string &path);
bool IsFileExt (const std::string &path, const std::string &ext);
int64_t FileSize (const std::string &path);
int GetFileType (const char *pcsz_);
bool GetInt (char *psz_, int &n_);
bool GetLong (char *psz_, long &l_);
bool GetRange (char *psz_, int &range_begin, int &range_end);
void ByteSwap (void *pv, size_t nSize_);
int TPeek (const uint8_t *buf, int offset = 0);
void TrackUsedInit (Disk &disk);
bool IsTrackUsed (int cyl_, int head_);

void CalculateGeometry (int64_t total_sectors, int &cyls, int &heads, int &sectors);
void ValidateRange (Range &range, int max_cyls, int max_heads, int cyl_step = 1, int def_cyls = -1, int def_heads = -1);

int SizeToCode (int sector_size);
bool ReadSector (const HDD &hdd, int sector, MEMORY &pm_);
bool CheckSig (const HDD &hdd, int sector, int offset, const char *sig, int len = 0);
bool DiskHasMBR (const HDD &hdd);

#ifndef S_ISDIR
#define _S_ISTYPE(mode,mask)    (((mode) & _S_IFMT) == (mask))
#define S_ISDIR(mode)           _S_ISTYPE((mode), _S_IFDIR)
#define S_ISREG(mode)           _S_ISTYPE((mode), _S_IFREG)
#endif


class MEMORY
{
public:
	explicit MEMORY (int uSize_) : size(uSize_), pb(AllocMem(size)) {}
	MEMORY (const MEMORY &) = delete;
	virtual ~MEMORY () { FreeMem(pb); }

	operator uint8_t* () { return pb; }
//	uint8_t& operator[] (size_t u_) { return pb[u_]; }

	int size = 0;
	uint8_t *pb = nullptr;

private:
	void operator= (const MEMORY &ref_) = delete;
};
typedef MEMORY * PMEMORY;

#ifndef HAVE_GETTIMEOFDAY
int gettimeofday(struct timeval *tv, struct timezone *tz);
#endif

#endif
