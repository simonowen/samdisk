// fdrawcmd.sys real device wrapper

#include "SAMdisk.h"
#include "DemandDisk.h"
#include "IBMPC.h"
#include "BitstreamDecoder.h"
#include "FdrawcmdSys.h"

#ifdef HAVE_FDRAWCMD_H
#include "fdrawcmd.h"

class FdrawSysDevDisk final : public DemandDisk
{
public:
	explicit FdrawSysDevDisk(std::unique_ptr<FdrawcmdSys> fdrawcmd)
		: m_fdrawcmd(std::move(fdrawcmd))
	{
		try
		{
			m_fdrawcmd->Seek(0);
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
		int firstSectorSeen{0};
		auto track = BlindReadHeaders(cylhead, firstSectorSeen);

		for (int i = 0; i < track.size(); ++i)
		{
			auto start_time = std::chrono::system_clock::now();
			ReadSector(cylhead, track, i);

			// Time the 2nd sector, as the motor will still be running after the 1st.
			if (i == 1)
			{
				// How long between reading sector 0 and finishing sector 1?
				auto end_time = std::chrono::system_clock::now();
				auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>
					(end_time - start_time).count();

				// If it took more than half a revolution between sectors, they're
				// probably too close together to read sequentially.
				if (elapsed_us > track.tracktime / 2)
				{
					// Read remaining odd then even sectors.
					for (i = 3 ; i < track.size(); i += 2)
						ReadSector(cylhead, track, i);
					for (i = 2; i < track.size(); i += 2)
						ReadSector(cylhead, track, i);

					break;
				}
			}
		}

		return TrackData(cylhead, std::move(track));
#if 0
		Data data(fmt.track_size(), fmt.fill);
		data.reserve(fmt.track_size());

		Track track;
		track.format(cylhead, RegularFormat::MGT);
		track.populate(data.begin(), data.end());

		return TrackData(cylhead, std::move(track));
#endif

/*
		FluxData flux_revs;
		auto revs = first_read ? FIRST_READ_REVS : REMAIN_READ_REVS;

		m_fdrawcmd->EnableMotor(1);

		m_fdrawcmd->Seek(cylhead.cyl);
		m_fdrawcmd->SelectSide(cylhead.head);

		std::vector<std::string> warnings;
		m_fdrawcmd->ReadFlux(revs + 1, flux_revs, warnings);
		for (auto &w : warnings)
			Message(msgWarning, "%s on %s", w.c_str(), CH(cylhead.cyl, cylhead.head));
*/
		//return TrackData(cylhead);
	}

	bool preload(const Range &/*range*/) override
	{
		return false;
	}

private:
	bool DetectEncRate(int head);
	Track BlindReadHeaders(const CylHead &cylhead, int &firstSectorSeen);
	void ReadSector(const CylHead &cylhead, Track &track, int index, int firstSectorSeen=0);

	std::unique_ptr<FdrawcmdSys> m_fdrawcmd;
	Encoding m_lastEncoding{Encoding::Unknown};
	DataRate m_lastDataRate{DataRate::Unknown};
	bool m_warned_128{false};
};

// Detect encoding and data rate of the track under the given drive head.
bool FdrawSysDevDisk::DetectEncRate(int head)
{
	FD_CMD_RESULT result{};

	if (m_lastEncoding != Encoding::Unknown && m_lastDataRate != DataRate::Unknown)
	{
		// Try the last successful encoding and data rate.
		m_fdrawcmd->SetEncRate(m_lastEncoding, m_lastDataRate);

		// Return if we found a sector.
		if (m_fdrawcmd->CmdReadId(head, &result))
			return true;
	}

	for (auto encoding : {Encoding::MFM, Encoding::FM})
	{
		for (auto datarate : {DataRate::_1M, DataRate::_500K, DataRate::_300K, DataRate::_250K})
		{
			// Skip FM if disabled or the data rate is 1Mbps (not worth checking).
			if (encoding == Encoding::FM && (opt.fm == 0 || datarate == DataRate::_1M))
				continue;

			// Skip rates not matching user selection (note: legacy values!)
			static const std::array<DataRate, 4> rates{DataRate::_500K, DataRate::_300K, DataRate::_250K, DataRate::_1M};
			if (opt.rate >= 0 && opt.rate < static_cast<int>(rates.size()) && datarate != rates[opt.rate])
				continue;

			// Skip 1Mbps if the FDC doesn't report it's supported.
			if (datarate == DataRate::_1M)
			{
				FD_FDC_INFO fi{};
				if (!m_fdrawcmd->GetFdcInfo(&fi) || !(fi.SpeedsAvailable & FDC_SPEED_1M))
				{
					// Fail if user selected the rate.
					if (opt.rate != -1)
						throw util::exception("FDC doesn't support 1Mbps data rate");

					continue;
				}
			}

			m_fdrawcmd->SetEncRate(encoding, datarate);

			// Retry in case of spurious header CRC errors.
			for (auto i = 0; i <= opt.retries; ++i)
			{
				if (m_fdrawcmd->CmdReadId(head, &result))
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

	for (int scan = 0; scan <= opt.rescans; ++scan)
	{
		bool changed{false};

		if (m_lastEncoding != Encoding::Unknown || m_lastDataRate == DataRate::Unknown)
			DetectEncRate(cylhead.head);
#if 0
		if (!m_fdrawcmd->CmdTimedScan(cylhead.head, scan_result, scan_size))
			throw win32_error(GetLastError(), "scan");
#else
		m_fdrawcmd->FdCheckDisk();

		while (!m_fdrawcmd->CmdTimedScan(cylhead.head, scan_result, scan_size))
		{
			if (GetLastError() == ERROR_NOT_READY)
			{
				for (int i = 0; i < 1000; ++i)
				{
					//if (m_fdrawcmd->CmdRead(0, 0, 0, 1, 2, 1, mem))
					FD_CMD_RESULT res{};
					if (m_fdrawcmd->CmdReadId(0, &res))
					{
						util::cout << "Successful ReadId on iteration " << i << "\n";

						MEMORY mem2(512);
						if (m_fdrawcmd->CmdRead(0, 0, 0, 1, 2, 1, mem2))
						{
							util::cout << "Successful Read on iteration " << i << "\n";
						}
						else
						{
							util::cout << "Read " << i << " failed with " << std::hex << GetLastError() << "\n";
						}
//						break;
					}
					else
					{
						util::cout << "ReadId " << i << " failed with " << std::hex << GetLastError() << "\n";
//						m_fdrawcmd->Recalibrate();
//						m_fdrawcmd->FdCheckDisk();
					}
				}

//				if (!m_fdrawcmd->CmdTimedScan(cylhead.head, scan_result, scan_size))
//					throw win32_error(GetLastError(), "scan");
			}
			else
			{
				util::cout << "CmdScan failed with " << std::hex << GetLastError() << "\n";
			}
		}
#endif

		// If nothing was found and we have valid settings, they might have changed.
		if (!scan_result->count && m_lastEncoding != Encoding::Unknown)
		{
			DetectEncRate(cylhead.head);

			if (!m_fdrawcmd->CmdTimedScan(cylhead.head, scan_result, scan_size))
				throw win32_error(GetLastError(), "scan");
		}

		// If the track time is slower than 200rpm, an index-halving cable must be present
		if (scan_result->tracktime > RPM_TIME_200)
			throw util::exception("index-halving cables are no longer supported");

		firstSectorSeen = scan_result->firstseen;
		if (firstSectorSeen) util::cout << "firstSectorSeen = " << firstSectorSeen << "\n";

		auto bit_us{GetDataTime(m_lastDataRate, m_lastEncoding) / 16};
		track.tracktime = scan_result->tracktime;
		track.tracklen = track.tracktime / bit_us;

		for (int i = 0; i < scan_result->count; ++i)
		{
			const auto &scan_header{scan_result->Header[i]};
			Header header(scan_header.cyl, scan_header.head, scan_header.sector, scan_header.size);
			Sector sector(m_lastDataRate, m_lastEncoding, header);

			sector.offset = scan_header.reltime / bit_us;

			if (track.add(std::move(sector)) != Track::AddResult::Unchanged)
				changed = true;
		}
	}

	return track;
}

void FdrawSysDevDisk::ReadSector(const CylHead &cylhead, Track &track, int index, int firstSectorSeen)
{
	auto &sector = track.sectors()[index];
	if (sector.has_badidcrc())
		return;

	auto size = sector.SizeCodeToLength(sector.SizeCodeToRealSizeCode(sector.header.size));
	MEMORY mem(size);
	memset(mem.pb, 0xee, mem.size);
	//sector.remove_data();

//	for (int i = 0, nSlot = 0; !g_fAbort && i <= nRetries_; i++)
	{
		// If the sector id occurs more than once on the track, synchronise to the correct one
		if (track.is_repeated(sector))
		{
			auto offset{(index + track.size() - firstSectorSeen) % track.size()};
			m_fdrawcmd->FdSetSectorOffset(offset);
		}

		// Clear the buffer in case of short read
//		memset(mem, 0, uDataSize);

		// Attempt the sector read
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

			//util::cout << "error: " << error << "\n";
		}

		// Get the controller result for the read to find out more
		FD_CMD_RESULT result{};
		if (!m_fdrawcmd->GetResult(&result))
			throw win32_error(GetLastError(), "result");
		//util::cout << std::hex << "ST0: " << result.st0 << ", ST1: " << result.st1 << ", ST2: " << result.st2 << "\n";

		// If this is an 8K sector, guess whether it has a correct checksum
//		bool f8K = track.is_8k_sector();
//		auto chk8k_method = f8K ? Get8KChecksumMethod(mem.pb, size) : CHK8K_UNKNOWN;

/*
		// Read successful?
		if (!(result.st0 & 0xc0) || (chk8k_method >= CHK8K_FOUND))
		{
		}
		else if (result.st2 & STREG2_CRC_ERROR)	// Data CRC error?
		{
			bool fFound = false;

			// Compare only the first 6K of 8K sectors
			UINT uCompare = f8K ? 0x1800 : uDataSize;

			// Check if we've got the same data already, so we don't duplicate it
			for (int j = 0; !fFound && j < arraysize(ps->apbData) && ps->apbData[j]; j++)
				fFound = opt.nocopies || !memcmp(ps->apbData[j], mem.pb, uCompare);

			// Got this copy already, so retry
			if (fFound)
				continue;
		}
		else
			continue;	// Simply retry for any other failure
*/
		// Missing header or data field?
		if ((result.st1 & STREG1_ID_NOT_FOUND) || (result.st2 & STREG2_DATA_NOT_FOUND))
			return;

		// Missing sector header?
		if (result.st1 & STREG1_SECTOR_NOT_FOUND)
		{
			if (header.size == 0 && !m_warned_128)
			{
				Message(msgWarning, "FDC seems unable to read 128-byte sector %s",
					CHSR(cylhead.cyl, cylhead.head, index, header.sector));
				m_warned_128 = true;
			}

			return;
		}

		bool data_crc{(result.st2 & STREG2_CRC_ERROR) != 0};
		uint8_t dam = (result.st2 & STREG2_CONTROL_MARK) ? 0xf8 : 0xfb;

		Data data(mem.pb, mem.pb + mem.size);
		sector.add(std::move(data), data_crc, dam);

/*
		// If the read was successful we're all done
		if (!(result.st0 & 0xc0) || (chk8k_method >= CHK8K_FOUND))
			break;

		// If not an 8K sector and the data overlaps the next header or the track, skip extra copies
		if (!f8K && pt_->GetDataExtent(nSector_) < uDataSize)
			break;
*/
	}

/*
	// If the track was blank after multiple attempts, remove all sectors
	if ((result.st1 & STREG1_ID_NOT_FOUND) && !(result.st2 & STREG2_DATA_NOT_FOUND))
		pt_->Unformat();
	// If we found no sign of the sector, remove it from the track list
	else if ((result.st1 & STREG1_SECTOR_NOT_FOUND) && !ps->apbData[0])
	{
		if (fRemoveMissing_)
			pt_->DeleteSector(nSector_);
		else if (ps->size == 0)
			Message(msgWarning, "FDC unable to read 128-byte sector %s", CHSR(pt_->cyl, pt_->head, nSector_, ps->sector));
		else
			Message(msgWarning, "unable to read data from %s", CHSR(pt_->cyl, pt_->head, nSector_, ps->sector));
	}
	else
	{
		// Set the status flags from the read result
		if (result.st1 & STREG1_CRC_ERROR)		 ps->flags |= SF_IDCRC;
		if (result.st2 & STREG2_DATA_NOT_FOUND) ps->flags |= SF_NODATA;
		if (result.st2 & STREG2_CRC_ERROR) { ps->flags |= SF_DATACRC; ps->flags &= ~SF_IDCRC; }
		if (result.st2 & STREG2_CONTROL_MARK)	 ps->flags |= SF_DELETED;
	}
*/
}

bool ReadFdrawSysDev(const std::string &path, std::shared_ptr<Disk> &disk)
{
	auto devidx = (util::lowercase(path) == "b:") ? 1 : 0;
	auto fdrawcmd = FdrawcmdSys::Open(devidx);
	if (!fdrawcmd)
		throw util::exception("failed to open fdrawcmd.sys device");

	auto fdrawcmd_dev_disk = std::make_shared<FdrawSysDevDisk>(std::move(fdrawcmd));
	fdrawcmd_dev_disk->extend(CylHead(83 - 1, 2 - 1));

	fdrawcmd_dev_disk->strType = "fdrawcmd.sys";
	disk = fdrawcmd_dev_disk;

	return true;
}

#endif // HAVE_FDRAWCMD_H
