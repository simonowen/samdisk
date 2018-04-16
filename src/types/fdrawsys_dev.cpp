// fdrawcmd.sys real device wrapper:
//  http://simonowen.com/fdrawcmd/

#include "SAMdisk.h"
#include "DemandDisk.h"
#include "IBMPC.h"
#include "FdrawcmdSys.h"

#ifdef HAVE_FDRAWCMD_H
#include "fdrawcmd.h"

class FdrawSysDevDisk final : public DemandDisk
{
public:
	FdrawSysDevDisk(const std::string &path, std::unique_ptr<FdrawcmdSys> fdrawcmd)
		: m_fdrawcmd(std::move(fdrawcmd))
	{
		try
		{
			SetMetadata(path);

			m_fdrawcmd->SetMotorTimeout(0);
			m_fdrawcmd->Recalibrate();

			if (!opt.newdrive)
				m_fdrawcmd->SetDiskCheck(false);
		}
		catch (...)
		{
			throw util::exception("failed to initialise fdrawcmd.sys device");
		}
	}

protected:
	TrackData load(const CylHead &cylhead, bool /*first_read*/) override
	{
		m_fdrawcmd->Seek(cylhead.cyl);

		auto firstSectorSeen{0};
		auto track = BlindReadHeaders(cylhead, firstSectorSeen);

		int i;
		for (i = 0; !g_fAbort && i < track.size(); i += 2)
			ReadSector(cylhead, track, i, firstSectorSeen);
		for (i = 1 ; !g_fAbort && i < track.size(); i += 2)
			ReadSector(cylhead, track, i, firstSectorSeen);

		if (opt.gaps >= GAPS_CLEAN)
			ReadFirstGap(cylhead, track);

		return TrackData(cylhead, std::move(track));
	}

	bool preload(const Range &/*range*/, int /*cyl_step*/) override
	{
		return false;
	}


private:
	void SetMetadata(const std::string &path);
	bool DetectEncodingAndDataRate(int head);
	Track BlindReadHeaders(const CylHead &cylhead, int &firstSectorSeen);
	void ReadSector(const CylHead &cylhead, Track &track, int index, int firstSectorSeen=0);
	void ReadFirstGap(const CylHead &cylhead, Track &track);

	std::unique_ptr<FdrawcmdSys> m_fdrawcmd;
	Encoding m_lastEncoding{Encoding::Unknown};
	DataRate m_lastDataRate{DataRate::Unknown};
	bool m_warnedMFM128{false};
};


void FdrawSysDevDisk::SetMetadata(const std::string &path)
{
	auto device_path = R"(\\.\)" + path;
	Win32Handle hdev{
		CreateFile(device_path.c_str(), 0, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, NULL) };

	if (hdev.get() != INVALID_HANDLE_VALUE)
	{
		DWORD dwRet = 0;
		DISK_GEOMETRY dg[8]{};
		if (DeviceIoControl(hdev.get(), IOCTL_STORAGE_GET_MEDIA_TYPES,
			nullptr, 0, &dg, sizeof(dg), &dwRet, NULL) && dwRet > sizeof(DISK_GEOMETRY))
		{
			auto count = dwRet / sizeof(dg[0]);
			metadata["bios_type"] = to_string(dg[count - 1].MediaType);
		}
	}

	FD_FDC_INFO info{};
	if (m_fdrawcmd->GetFdcInfo(info))
	{
		static const std::vector<std::string> fdc_types{
			"Unknown", "Unknown1", "Normal", "Enhanced", "82077", "82077AA", "82078_44", "82078_64", "National" };
		static const std::vector<std::string> data_rates{
			"250K", "300K", "500K", "1M", "2M" };

		std::stringstream ss;
		for (size_t i = 0, n = 0; i < data_rates.size(); ++i)
		{
			if (!(info.SpeedsAvailable & (1U << i)))
				continue;

			if (n++) ss << " / ";
			ss << data_rates[i];
		}

		metadata["fdc_type"] = (info.ControllerType < fdc_types.size()) ? fdc_types[info.ControllerType] : "???";
		metadata["data_rates"] = ss.str();
	}
}

// Detect encoding and data rate of the track under the given drive head.
bool FdrawSysDevDisk::DetectEncodingAndDataRate(int head)
{
	FD_CMD_RESULT result{};

	if (m_lastEncoding != Encoding::Unknown && m_lastDataRate != DataRate::Unknown)
	{
		// Try the last successful encoding and data rate.
		m_fdrawcmd->SetEncRate(m_lastEncoding, m_lastDataRate);

		// Return if we found a sector.
		if (m_fdrawcmd->CmdReadId(head, result))
			return true;
	}

	for (auto encoding : {Encoding::MFM, Encoding::FM})
	{
		for (auto datarate : {DataRate::_1M, DataRate::_500K, DataRate::_300K, DataRate::_250K})
		{
			// Skip FM if we're only looking for MFM, or the data rate is 1Mbps.
			if (encoding == Encoding::FM && (opt.encoding == Encoding::MFM || datarate == DataRate::_1M))
				continue;

			// Skip rates not matching user selection.
			if (opt.datarate != DataRate::Unknown && datarate != opt.datarate)
				continue;

			// Skip 1Mbps if the FDC doesn't report it's supported.
			if (datarate == DataRate::_1M)
			{
				FD_FDC_INFO fi{};
				if (!m_fdrawcmd->GetFdcInfo(fi) || !(fi.SpeedsAvailable & FDC_SPEED_1M))
				{
					// Fail if user selected the rate.
					if (opt.datarate == DataRate::_1M)
						throw util::exception("FDC doesn't support 1Mbps data rate");

					continue;
				}
			}

			m_fdrawcmd->SetEncRate(encoding, datarate);

			// Retry in case of spurious header CRC errors.
			for (auto i = 0; i <= opt.retries; ++i)
			{
				if (m_fdrawcmd->CmdReadId(head, result))
				{
					// Remember the settings for the first try next time.
					m_lastEncoding = encoding;
					m_lastDataRate = datarate;
					return true;
				}

				// Give up on the current settings if nothing was found.
				if (GetLastError() == ERROR_FLOPPY_ID_MARK_NOT_FOUND ||
					GetLastError() == ERROR_SECTOR_NOT_FOUND)
					break;

				// Fail for any reason except a CRC error
				if (GetLastError() != ERROR_CRC)
					throw win32_error(GetLastError(), "READ_ID");
			}
		}
	}

	// Nothing detected.
	return false;
}

Track FdrawSysDevDisk::BlindReadHeaders(const CylHead &cylhead, int &firstSectorSeen)
{
	Track track;

	auto scan_size = sizeof(FD_TIMED_SCAN_RESULT) + sizeof(FD_TIMED_ID_HEADER) * MAX_SECTORS;
	MEMORY mem(scan_size);
	auto scan_result = reinterpret_cast<FD_TIMED_SCAN_RESULT *>(mem.pb);

	if (m_lastEncoding == Encoding::Unknown || m_lastDataRate == DataRate::Unknown)
		DetectEncodingAndDataRate(cylhead.head);

	if (!m_fdrawcmd->CmdTimedScan(cylhead.head, scan_result, scan_size))
		throw win32_error(GetLastError(), "scan");

	// If nothing was found and we have valid settings, they might have changed.
	if (!scan_result->count && m_lastEncoding != Encoding::Unknown)
	{
		DetectEncodingAndDataRate(cylhead.head);

		if (!m_fdrawcmd->CmdTimedScan(cylhead.head, scan_result, scan_size))
			throw win32_error(GetLastError(), "scan");
	}

	// If the track time is slower than 200rpm, an index-halving cable must be present
	if (scan_result->tracktime > RPM_TIME_200)
		throw util::exception("index-halving cables are no longer supported");

	firstSectorSeen = scan_result->firstseen;

	if (scan_result->count > 0)
	{
		auto bit_us = GetDataTime(m_lastDataRate, m_lastEncoding) / 16;
		track.tracktime = scan_result->tracktime;
		track.tracklen = track.tracktime / bit_us;

		for (int i = 0; i < scan_result->count; ++i)
		{
			const auto &scan_header = scan_result->Header[i];
			Header header(scan_header.cyl, scan_header.head, scan_header.sector, scan_header.size);
			Sector sector(m_lastDataRate, m_lastEncoding, header);

			sector.offset = scan_header.reltime / bit_us;
			track.add(std::move(sector));
		}
	}

	return track;
}

void FdrawSysDevDisk::ReadSector(const CylHead &cylhead, Track &track, int index, int firstSectorSeen)
{
	auto &sector = track[index];

	if (sector.has_badidcrc() || sector.has_good_data())
		return;

	auto size = sector.SizeCodeToLength(sector.SizeCodeToRealSizeCode(sector.header.size));
	MEMORY mem(size);

	for (int i = 0; !g_fAbort && i <= opt.retries; ++i)
	{
		// If the sector id occurs more than once on the track, synchronise to the correct one
		if (track.is_repeated(sector))
		{
			auto offset{(index + track.size() - firstSectorSeen) % track.size()};
			m_fdrawcmd->FdSetSectorOffset(offset);
		}

		// Invalidate the content so misbehaving FDCs can be identififed.
		memset(mem.pb, 0xee, mem.size);

		const Header &header = sector.header;
		if (!m_fdrawcmd->CmdRead(cylhead.head, header.cyl, header.head, header.sector, header.size, 1, mem))
		{
			// Reject errors other than CRC, sector not found and missing address marks
			auto error{GetLastError()};
			if (error != ERROR_CRC &&
				error != ERROR_SECTOR_NOT_FOUND &&
				error != ERROR_FLOPPY_ID_MARK_NOT_FOUND)
			{
				throw win32_error(error, "read");
			}
		}

		// Get the controller result for the read to find out more
		FD_CMD_RESULT result{};
		if (!m_fdrawcmd->GetResult(result))
			throw win32_error(GetLastError(), "result");

		// Try again if header or data field are missing.
		if (result.st1 & (STREG1_MISSING_ADDRESS_MARK | STREG1_NO_DATA))
			continue;

		// Header match not found for a sector we scanned earlier?
		if (result.st1 & STREG1_END_OF_CYLINDER)
		{
			// Warn the user if we suspect the FDC can't handle 128-byte MFM sectors.
			if (!m_warnedMFM128 && sector.encoding == Encoding::MFM && sector.size() == 128)
			{
				Message(msgWarning, "FDC seems unable to read 128-byte sectors correctly");
				m_warnedMFM128 = true;
			}
			continue;
		}

		bool data_crc_error{ (result.st2 & STREG2_DATA_ERROR_IN_DATA_FIELD) != 0 };
		uint8_t dam = (result.st2 & STREG2_CONTROL_MARK) ? 0xf8 : 0xfb;

		Data data(mem.pb, mem.pb + mem.size);
		sector.add(std::move(data), data_crc_error, dam);

		// If the read command was successful we're all done.
		if ((result.st0 & STREG0_INTERRUPT_CODE) == 0)
			break;

		// Accept sectors that overlap the next field, as they're unlikely to succeed.
		if (track.data_overlap(sector))
			break;

		// Accept 8K sectors with a recognised checksum method.
		if (track.is_8k_sector())
		{
			// If the 8K checksum is recognised we're done.
			auto chk8k_method = Get8KChecksumMethod(mem.pb, size);
			if (chk8k_method == CHK8K_VALID || chk8k_method >= CHK8K_FOUND)
				break;
		}
	}
}

void FdrawSysDevDisk::ReadFirstGap(const CylHead &cylhead, Track &track)
{
	if (track.empty())
		return;

	auto &sector = track[0];

	if (sector.has_badidcrc() || track.data_overlap(sector))
		return;

	// Read a size
	auto size_code = sector.header.size + 1;
	auto size = sector.SizeCodeToLength(sector.SizeCodeToRealSizeCode(size_code));
	MEMORY mem(size);

	for (int i = 0; !g_fAbort && i <= opt.retries; ++i)
	{
		// Invalidate the content so misbehaving FDCs can be identififed.
		memset(mem.pb, 0xee, mem.size);

		if (!m_fdrawcmd->CmdReadTrack(cylhead.head, 0, 0, 0, size_code, 1, mem))
		{
			// Reject errors other than CRC, sector not found and missing address marks
			auto error{ GetLastError() };
			if (error != ERROR_CRC &&
				error != ERROR_SECTOR_NOT_FOUND &&
				error != ERROR_FLOPPY_ID_MARK_NOT_FOUND)
			{
				throw win32_error(error, "read_track");
			}
		}

		FD_CMD_RESULT result{};
		if (!m_fdrawcmd->GetResult(result))
			throw win32_error(GetLastError(), "result");

		if (result.st1 & (STREG1_MISSING_ADDRESS_MARK | STREG1_END_OF_CYLINDER))
			continue;
		else if (result.st2 & STREG2_MISSING_ADDRESS_MARK_IN_DATA_FIELD)
			continue;

		// Sanity check the start of the track against a good copy.
		if (sector.has_good_data())
		{
			const auto data = sector.data_copy();
			if (std::memcmp(data.data(), mem.pb, data.size()))
			{
				Message(msgWarning, "track read of %s doesn't match first sector content", CH(cylhead.cyl, cylhead.head));
				break;
			}
		}

		auto extent = track.data_extent_bytes(sector);
		sector.add(Data(mem.pb, mem.pb + extent), sector.has_baddatacrc(), sector.dam);
		break;
	}
}

bool ReadFdrawSysDev(const std::string &path, std::shared_ptr<Disk> &disk)
{
	auto devidx = (util::lowercase(path) == "b:") ? 1 : 0;
	auto fdrawcmd = FdrawcmdSys::Open(devidx);
	if (!fdrawcmd)
		throw util::exception("failed to open fdrawcmd.sys device");

	auto fdrawcmd_dev_disk = std::make_shared<FdrawSysDevDisk>(path, std::move(fdrawcmd));
	fdrawcmd_dev_disk->extend(CylHead(83 - 1, 2 - 1));

	fdrawcmd_dev_disk->strType = "fdrawcmd.sys";
	disk = fdrawcmd_dev_disk;

	return true;
}

#endif // HAVE_FDRAWCMD_H
