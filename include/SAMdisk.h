#pragma once

#include "config.h"

#if defined(_WIN32) && !defined(WINVER)
#define WINVER 0x0500
#define _WIN32_WINNT 0x0501
#define _WIN32_IE 0x0501
#define _RICHEDIT_VER 0x0100
#endif


#ifdef _WIN32
#define PATH_SEPARATOR_CHR  '\\'
#else
#define PATH_SEPARATOR_CHR  '/'
#endif

#ifndef _WIN32

// ToDo: fix BlockDevice so it doesn't need these
typedef void* HANDLE;
#define INVALID_HANDLE_VALUE reinterpret_cast<void *>(-1)

#define MAX_PATH    512

#define O_SEQUENTIAL    0
#define O_BINARY        0

#define DeviceIoControl(a,b,c,d,e,f,g,h)    (*g = 0)

#define CTL_CODE(a,b,c,d)   (b)
#define FILE_READ_DATA      0
#define FILE_WRITE_DATA     0
#define METHOD_BUFFERED     0
#define METHOD_OUT_DIRECT   0
#define METHOD_IN_DIRECT    0

#endif // WIN32


#ifdef _MSC_VER
#define _CRT_SECURE_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE
#define _WINSOCK_DEPRECATED_NO_WARNINGS
// #define _ITERATOR_DEBUG_LEVEL 0  // ToDo: remove?

#pragma warning(default:4062)       // enumerator 'identifier' in a switch of enum 'enumeration' is not handled
// #pragma warning(default:4242)    // 'identifier' : conversion from 'type1' to 'type2', possible loss of data
// #pragma warning(default:4265)    // 'class': class has virtual functions, but destructor is not virtual
#endif // _MSC_VER

#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <array>
#include <vector>
#include <map>
#include <set>
#include <memory>    // for unique_ptr
#include <algorithm> // for sort
#include <functional>
#include <numeric>
#include <bitset>
#include <limits>

#include <cstdio>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <cctype>
#include <fstream>
#include <sys/stat.h>
#include <cerrno>
#include <ctime>
#include <csignal>
#include <fcntl.h>
#include <chrono>
#include <thread>
#include <cassert>
#include <system_error>


#ifndef HAVE_O_BINARY
#define O_BINARY    0
#endif

#ifndef O_DIRECT
#define O_DIRECT    0
#endif

#if !defined(HAVE_STRCASECMP) && defined(HAVE__STRCMPI)
#define strcasecmp  _stricmp
#define HAVE_STRCASECMP
#endif

#if !defined(HAVE_SNPRINTF) && defined(HAVE__SNPRINTF)
#define snprintf    _snprintf
#define HAVE_SNPRINTF
#endif

#ifndef HAVE_LSEEK64
#ifdef HAVE__LSEEKI64
#define lseek64 _lseeki64
#else
#define lseek64 lseek
#endif
#endif

#ifdef _WIN32
#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#ifndef DBG_NEW
#define DBG_NEW new ( _NORMAL_BLOCK , __FILE__ , __LINE__ )
#define new DBG_NEW
#endif
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define STRICT
#include <windows.h>
#include <devguid.h>
#include <winioctl.h>
#include <shellapi.h>
#include "CrashDump.h"
#else
#endif // WIN32

#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>   // include before windows.h to avoid winsock.h
#include <ws2tcpip.h>   // for socklen_t
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#ifndef _WIN32
#define SOCKET int
#define closesocket close
#endif
#endif

#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_SYS_TIMEB_H
#include <sys/timeb.h>
#endif

#ifdef HAVE_IO_H
#include <io.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_PATHS_H
#include <paths.h>
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>   // for gettimeofday()
#endif

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#include "utils.h"
#include "win32_error.h"
#include "CRC16.h"
#include "Disk.h"
#include "DiskUtil.h"
#include "Header.h"
#include "MemFile.h"
#include "Image.h"
#include "HDD.h"
#include "Util.h"
#include "SAMCoupe.h"

static const int MAX_IMAGE_SIZE = 256 * 1024 * 1024;    // 256MiB

enum { GAPS_AUTO = -1, GAPS_NONE, GAPS_CLEAN, GAPS_ALL };

// copy
bool ImageToImage(const std::string& src_path, const std::string& dst_path);
bool Image2Trinity(const std::string& path, const std::string& trinity_path);
bool Hdd2Hdd(const std::string& src_path, const std::string& dst_path);
bool Hdd2Boot(const std::string& hdd_path, const std::string& boot_path);
bool Boot2Hdd(const std::string& boot_path, const std::string& hdd_path);
bool Boot2Boot(const std::string& src_path, const std::string& dst_path);

// create
bool CreateImage(const std::string& path, Range range);
bool CreateHddImage(const std::string& path, int nSizeMB_);

// dir
bool Dir(Disk& disk);
bool DirImage(const std::string& path);
bool IsMgtDirSector(const Sector& sector);

// list
bool ListDrives(int nVerbose_);
bool ListRecords(const std::string& path);
void ListDrive(const std::string& path, const HDD& hdd, int verbose);

// scan
bool ScanImage(const std::string& path, Range range);
void ScanTrack(const CylHead& cylhead, const Track& track, ScanContext& context);

// format, verify
bool FormatHdd(const std::string& path);
bool FormatBoot(const std::string& path);
bool FormatRecord(const std::string& path);
bool FormatImage(const std::string& path, Range range);
bool UnformatImage(const std::string& path, Range range);

// rpm
bool DiskRpm(const std::string& path);

// info
bool HddInfo(const std::string& path, int nVerbose_);
bool ImageInfo(const std::string& path);

// view
bool ViewImage(const std::string& path, Range range);
bool ViewHdd(const std::string& path, Range range);
bool ViewBoot(const std::string& path, Range range);

// fdrawcmd.sys driver functions
bool CheckDriver();
bool ReportDriverVersion();

enum class PreferredData { Unknown, Track, Bitstream, Flux };

struct OPTIONS
{
    Range range{};
    int step = 1;

    int base = -1, size = -1, gap3 = -1, interleave = -1, skew = -1, fill = -1;
    int gaps = -1, gap2 = -1, gap4b = -1, idcrc = -1, gapmask = -1, maxsplice = -1;
    int cylsfirst = -1, head0 = -1, head1 = -1, steprate = -1, check8k = -1;
    int offsets = -1, fix = -1, mt = -1, plladjust = -1, hardsectors = -1;

    int command = 0, hex = 0, debug = 0, verbose = 0, log = 0, force = 0, quick = 0;
    int merge = 0, repair = 0, trim = 0, calibrate = 0, newdrive = 0, byteswap = 0;
    int noweak = 0, nosig = 0, nodata = 0, nocfa = 0, noidentify = 0, nospecial = 0;
    int nozip = 0, nodiff = 0, noformat = 0, nodups = 0, nowobble = 0, nottb = 0;
    int bdos = 0, atom = 0, hdf = 0, resize = 0, cpm = 0, minimal = 0, legacy = 0;
    int absoffsets = 0, datacopy = 0, align = 0, keepoverlap = 0, fmoverlap = 0;
    int rescans = 0, flip = 0, multiformat = 0, rpm = 0, tty = 0, time = 0;
    int a1sync = 0;

    int retries = 5, maxcopies = 3;
    int scale = 100, pllphase = DEFAULT_PLL_PHASE;
    int bytes_begin = 0, bytes_end = std::numeric_limits<int>::max();
    int bitskip = -1;

    Encoding encoding{ Encoding::Unknown };
    DataRate datarate{ DataRate::Unknown };
    PreferredData prefer = PreferredData::Unknown;
    long sectors = -1;
    std::string label{}, boot{};

    char szSource[MAX_PATH], szTarget[MAX_PATH];

};

extern OPTIONS opt;
