#ifndef FDRAWCMD_SYS_H
#define FDRAWCMD_SYS_H

#ifdef HAVE_FDRAWCMD_H
#include "fdrawcmd.h"

struct handle_closer
{
	void operator() (HANDLE h) { if (h && h != INVALID_HANDLE_VALUE) ::CloseHandle(h); }
	typedef HANDLE pointer;
};

using Win32Handle = std::unique_ptr<std::remove_pointer<HANDLE>::type, handle_closer>;


class FdrawcmdSys
{
public:
	FdrawcmdSys(HANDLE hdev);
	static std::unique_ptr<FdrawcmdSys> Open(int device);

public:
	bool GetResult(FD_CMD_RESULT &result);
	bool SetEncRate(Encoding encoding, DataRate datarate);
	bool SetHeadSettleTime(int ms);
	bool SetMotorTimeout(int seconds);
	bool SetMotorOff();
	bool SetDiskCheck(bool enable);
	bool GetFdcInfo(FD_FDC_INFO &info);
	bool Configure(uint8_t eis_efifo_poll_fifothr, uint8_t pretrk);
	bool Specify(uint8_t srt_hut, uint8_t hlt_nd);
	bool Recalibrate();
	bool Seek(int cyl);
	bool RelativeSeek(int head, int offset);
	bool CmdVerify(int cyl, int head, int start, int size, int eot);
	bool CmdVerify(int phead, int cyl, int head, int sector, int size, int eot);
	bool CmdReadTrack(int phead, int cyl, int head, int sector, int size, int eot, MEMORY &mem);
	bool CmdRead(int phead, int cyl, int head, int sector, int size, int count, MEMORY &mem, size_t uOffset_ = 0, bool fDeleted_ = false);
	bool CmdWrite(int phead, int cyl, int head, int sector, int size, int count, MEMORY &mem, bool fDeleted_ = false);
	bool CmdFormat(FD_FORMAT_PARAMS *params, int size);
	bool CmdFormatAndWrite(FD_FORMAT_PARAMS *params, int size);
	bool CmdScan(int head, FD_SCAN_RESULT *scan, int size);
	bool CmdTimedScan(int head, FD_TIMED_SCAN_RESULT *timed_scan, int size);
	bool CmdReadId(int head, FD_CMD_RESULT &result);
	bool FdRawReadTrack(int head, int size, MEMORY &mem);
	bool FdSetSectorOffset(int index);
	bool FdSetShortWrite(int length, int finetune);
	bool FdGetRemainCount(int &remain);
	bool FdCheckDisk();
	bool FdGetTrackTime(int &microseconds);
	bool FdReset();

private:
	static constexpr int RW_GAP = 0x0a;
	static constexpr uint8_t DtlFromSize(int size);

	bool Ioctl(DWORD code, void *inbuf = nullptr, int insize = 0, void *outbuf = nullptr, int outsize = 0);

	uint8_t m_encoding_flags{FD_OPTION_MFM};	// FD_OPTION_FM or FD_OPTION_MFM only.
	Win32Handle m_hdev{};
};

#endif // HAVE_FDRAWCMD_H

#endif // FDRAWCMD_SYS_H
