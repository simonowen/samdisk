// fdrawcmd.sys device

#include "SAMdisk.h"
#include "FdrawcmdSys.h"

#ifdef HAVE_FDRAWCMD_H

/*static*/ std::unique_ptr<FdrawcmdSys> FdrawcmdSys::Open(int device_index)
{
	auto path = util::format(R"(\\.\fdraw)", device_index);

	Win32Handle hdev{CreateFile(
		path.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_WRITE | FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED,
		nullptr)};

	if (hdev.get() != INVALID_HANDLE_VALUE)
		return std::make_unique<FdrawcmdSys>(hdev.release());

	return std::unique_ptr<FdrawcmdSys>();
}

FdrawcmdSys::FdrawcmdSys(HANDLE hdev)
{
	m_hdev.reset(hdev);
}

bool FdrawcmdSys::Ioctl(DWORD code, void *inbuf, int insize, void *outbuf, int outsize)
{
	DWORD dwRet{0};
	return !!DeviceIoControl(m_hdev.get(), code, inbuf, insize, outbuf, outsize, &dwRet, NULL);
}

constexpr uint8_t FdrawcmdSys::DtlFromSize(int size)
{
	// Data length used only for 128-byte sectors.
	return (size == 0) ? 0x80 : 0xff;
}

////////////////////////////////////////////////////////////////////////////////

bool FdrawcmdSys::GetResult(FD_CMD_RESULT &result)
{
	return Ioctl(IOCTL_FD_GET_RESULT,
		nullptr, 0,
		&result, sizeof(result));
}

bool FdrawcmdSys::SetEncRate(Encoding encoding, DataRate datarate)
{
	if (encoding != Encoding::MFM && encoding != Encoding::FM)
		throw util::exception("unsupported encoding (", encoding, ") for fdrawcmd.sys");

	// Set perpendicular mode and write-enable for 1M data rate
	FD_PERPENDICULAR_PARAMS pp{};
	pp.ow_ds_gap_wgate = (datarate == DataRate::_1M) ? 0xbc : 0x00;
	Ioctl(IOCTL_FDCMD_PERPENDICULAR_MODE, &pp, sizeof(pp));

	uint8_t rate;
	switch (datarate)
	{
	case DataRate::_250K:	rate = FD_RATE_250K; break;
	case DataRate::_300K:	rate = FD_RATE_300K; break;
	case DataRate::_500K:	rate = FD_RATE_500K; break;
	case DataRate::_1M:		rate = FD_RATE_1M; break;
	default:
		throw util::exception("unsupported datarate (", datarate, ")");
	}

	return Ioctl(IOCTL_FD_SET_DATA_RATE, &rate, sizeof(rate));
}

bool FdrawcmdSys::SetHeadSettleTime(int ms)
{
	auto hst = static_cast<uint8_t>(std::max(0, std::min(255, ms)));
	return Ioctl(IOCTL_FD_SET_HEAD_SETTLE_TIME, &hst, sizeof(hst));
}

bool FdrawcmdSys::SetMotorTimeout(int seconds)
{
	auto timeout = static_cast<uint8_t>(std::max(0, std::min(3, seconds)));
	return Ioctl(IOCTL_FD_SET_MOTOR_TIMEOUT, &timeout, sizeof(timeout));
}

bool FdrawcmdSys::SetMotorOff()
{
	return Ioctl(IOCTL_FD_MOTOR_OFF);
}

bool FdrawcmdSys::SetDiskCheck(bool enable)
{
	uint8_t check{enable ? 1U : 0};
	return Ioctl(IOCTL_FD_SET_DISK_CHECK, &check, sizeof(check));
}

bool FdrawcmdSys::GetFdcInfo(FD_FDC_INFO &info)
{
	return Ioctl(IOCTL_FD_GET_FDC_INFO,
		nullptr, 0,
		&info, sizeof(info));
}

bool FdrawcmdSys::Configure(uint8_t eis_efifo_poll_fifothr, uint8_t pretrk)
{
	FD_CONFIGURE_PARAMS cp{};
	cp.eis_efifo_poll_fifothr = eis_efifo_poll_fifothr;
	cp.pretrk = pretrk;

	return Ioctl(IOCTL_FDCMD_CONFIGURE, &cp, sizeof(cp));
}

bool FdrawcmdSys::Specify(uint8_t srt_hut, uint8_t hlt_nd)
{
	FD_SPECIFY_PARAMS sp{};
	sp.srt_hut = srt_hut;
	sp.hlt_nd = hlt_nd;

	return Ioctl(IOCTL_FDCMD_SPECIFY, &sp, sizeof(sp));
}

bool FdrawcmdSys::Recalibrate()
{
	// ToDo: should we check TRACK0 and retry if not signalled?
	return Ioctl(IOCTL_FDCMD_RECALIBRATE);
}

bool FdrawcmdSys::Seek(int cyl)
{
	if (cyl == 0)
		return Recalibrate();

	FD_SEEK_PARAMS sp{};
	sp.cyl = static_cast<uint8_t>(cyl);

	return Ioctl(IOCTL_FDCMD_SEEK, &sp, sizeof(sp));
}

bool FdrawcmdSys::RelativeSeek(int head, int offset)
{
	FD_RELATIVE_SEEK_PARAMS rsp{};
	rsp.flags = (offset > 0) ? FD_OPTION_DIR : 0;
	rsp.head = static_cast<uint8_t>(head);
	rsp.offset = static_cast<uint8_t>(std::abs(offset));

	return Ioctl(IOCTL_FDCMD_RELATIVE_SEEK, &rsp, sizeof(rsp));
}

bool FdrawcmdSys::CmdVerify(int cyl, int head, int sector, int size, int eot)
{
	return CmdVerify(head, cyl, head, sector, size, eot);
}

bool FdrawcmdSys::CmdVerify(int phead, int cyl, int head, int sector, int size, int eot)
{
	FD_READ_WRITE_PARAMS rwp{};
	rwp.flags = m_encoding_flags;
	rwp.phead = static_cast<uint8_t>(phead);
	rwp.cyl = static_cast<uint8_t>(cyl);
	rwp.head = static_cast<uint8_t>(head);
	rwp.sector = static_cast<uint8_t>(sector);
	rwp.size = static_cast<uint8_t>(size);
	rwp.eot = static_cast<uint8_t>(eot);
	rwp.gap = RW_GAP;
	rwp.datalen = DtlFromSize(size);

	return Ioctl(IOCTL_FDCMD_VERIFY, &rwp, sizeof(rwp));
}

bool FdrawcmdSys::CmdReadTrack(int phead, int cyl, int head, int sector, int size, int eot, MEMORY &mem)
{
	FD_READ_WRITE_PARAMS rwp{};
	rwp.flags = m_encoding_flags;
	rwp.phead = static_cast<uint8_t>(phead);
	rwp.cyl = static_cast<uint8_t>(cyl);
	rwp.head = static_cast<uint8_t>(head);
	rwp.sector = static_cast<uint8_t>(sector);
	rwp.size = static_cast<uint8_t>(size);
	rwp.eot = static_cast<uint8_t>(eot);
	rwp.gap = RW_GAP;
	rwp.datalen = DtlFromSize(size);

	return Ioctl(IOCTL_FDCMD_READ_TRACK,
		&rwp, sizeof(rwp),
		mem, eot * Sector::SizeCodeToLength(rwp.size));
}

bool FdrawcmdSys::CmdRead(int phead, int cyl, int head, int sector, int size, int count, MEMORY &mem, size_t data_offset, bool deleted)
{
	FD_READ_WRITE_PARAMS rwp{};
	rwp.flags = m_encoding_flags;
	rwp.phead = static_cast<uint8_t>(phead);
	rwp.cyl = static_cast<uint8_t>(cyl);
	rwp.head = static_cast<uint8_t>(head);
	rwp.sector = static_cast<uint8_t>(sector);
	rwp.size = static_cast<uint8_t>(size);
	rwp.eot = static_cast<uint8_t>(sector + count);
	rwp.gap = RW_GAP;
	rwp.datalen = DtlFromSize(size);

	return Ioctl(deleted ? IOCTL_FDCMD_READ_DELETED_DATA : IOCTL_FDCMD_READ_DATA,
		&rwp, sizeof(rwp),
		mem + data_offset, count * Sector::SizeCodeToLength(size));
}

bool FdrawcmdSys::CmdWrite(int phead, int cyl, int head, int sector, int size, int count, MEMORY &mem, bool deleted)
{
	FD_READ_WRITE_PARAMS rwp{};
	rwp.flags = m_encoding_flags;
	rwp.phead = static_cast<uint8_t>(phead);
	rwp.cyl = static_cast<uint8_t>(cyl);
	rwp.head = static_cast<uint8_t>(head);
	rwp.sector = static_cast<uint8_t>(sector);
	rwp.size = static_cast<uint8_t>(size);
	rwp.eot = static_cast<uint8_t>(sector + count);
	rwp.gap = RW_GAP;
	rwp.datalen = DtlFromSize(size);

	return Ioctl(deleted ? IOCTL_FDCMD_WRITE_DELETED_DATA : IOCTL_FDCMD_WRITE_DATA,
		&rwp, sizeof(rwp),
		mem, count * Sector::SizeCodeToLength(size));
}

bool FdrawcmdSys::CmdFormat(FD_FORMAT_PARAMS *params, int size)
{
	return Ioctl(IOCTL_FDCMD_FORMAT_TRACK, params, size);
}

bool FdrawcmdSys::CmdFormatAndWrite(FD_FORMAT_PARAMS *params, int size)
{
	return Ioctl(IOCTL_FDCMD_FORMAT_AND_WRITE, params, size);
}

bool FdrawcmdSys::CmdScan(int head, FD_SCAN_RESULT *scan, int size)
{
	FD_SCAN_PARAMS sp{};
	sp.flags = m_encoding_flags;
	sp.head = static_cast<uint8_t>(head);

	return Ioctl(IOCTL_FD_SCAN_TRACK,
		&sp, sizeof(sp),
		scan, size);
}

bool FdrawcmdSys::CmdTimedScan(int head, FD_TIMED_SCAN_RESULT *timed_scan, int size)
{
	FD_SCAN_PARAMS sp{};
	sp.flags = m_encoding_flags;
	sp.head = static_cast<uint8_t>(head);

	return Ioctl(IOCTL_FD_TIMED_SCAN_TRACK,
		&sp, sizeof(sp),
		timed_scan, size);
}

bool FdrawcmdSys::CmdReadId(int head, FD_CMD_RESULT &result)
{
	FD_READ_ID_PARAMS rip{};
	rip.flags = m_encoding_flags;
	rip.head = static_cast<uint8_t>(head);

	return Ioctl(IOCTL_FDCMD_READ_ID,
		&rip, sizeof(rip),
		&result, sizeof(result));
}

bool FdrawcmdSys::FdRawReadTrack(int head, int size, MEMORY &mem)
{
	FD_RAW_READ_PARAMS rrp{};
	rrp.flags = FD_OPTION_MFM;
	rrp.head = static_cast<uint8_t>(head);
	rrp.size = static_cast<uint8_t>(size);

	return Ioctl(IOCTL_FD_RAW_READ_TRACK,
		&rrp, sizeof(rrp),
		mem.pb, mem.size);
}

bool FdrawcmdSys::FdSetSectorOffset(int index)
{
	FD_SECTOR_OFFSET_PARAMS sop{};
	sop.sectors = static_cast<uint8_t>(std::max(0, std::min(255, index)));

	return Ioctl(IOCTL_FD_SET_SECTOR_OFFSET, &sop, sizeof(sop));
}

bool FdrawcmdSys::FdSetShortWrite(int length, int finetune)
{
	FD_SHORT_WRITE_PARAMS swp{};
	swp.length = static_cast<DWORD>(length);
	swp.finetune = static_cast<DWORD>(finetune);

	return Ioctl(IOCTL_FD_SET_SHORT_WRITE, &swp, sizeof(swp));
}

bool FdrawcmdSys::FdGetRemainCount(int &remain)
{
	return Ioctl(IOCTL_FD_GET_REMAIN_COUNT,
		nullptr, 0,
		&remain, sizeof(remain));
}

bool FdrawcmdSys::FdCheckDisk()
{
	return Ioctl(IOCTL_FD_CHECK_DISK);
}

bool FdrawcmdSys::FdGetTrackTime(int &microseconds)
{
	return Ioctl(IOCTL_FD_GET_TRACK_TIME,
		nullptr, 0,
		&microseconds, sizeof(microseconds));
}

bool FdrawcmdSys::FdReset()
{
	return Ioctl(IOCTL_FD_RESET);
}

#endif // HAVE_FDRAWCMD_H
