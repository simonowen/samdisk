// SuperCard Pro device base class

#include "SAMdisk.h"
#include "SuperCardPro.h"
#include "FluxTrackBuffer.h"

#ifdef HAVE_FTD2XX
#include "SCP_FTDI.h"
#endif
#include "SCP_USB.h"
#ifdef _WIN32
#include "SCP_Win32.h"
#endif

const int SuperCardPro::MAX_FLUX_REVS;	// SCP firmware hard limit


std::unique_ptr<SuperCardPro> SuperCardPro::Open ()
{
	std::unique_ptr<SuperCardPro> p;

#ifdef HAVE_FTD2XX
	if (!p)
		p = SuperCardProFTDI::Open();
#endif
	if (!p)
		p = SuperCardProUSB::Open();
#ifdef _WIN32
	if (!p)
		p = SuperCardProWin32::Open();
#endif

	return p;
}


bool SuperCardPro::ReadExact (void *buf, int len)
{
	uint8_t *p = reinterpret_cast<uint8_t*>(buf);
	auto bytes_read = 0;

	while (len > 0)
	{
		if (!Read(p, len, &bytes_read))
			return false;

		p += bytes_read;
		len -= bytes_read;
	}
	return true;
}

bool SuperCardPro::WriteExact (const void *buf, int len)
{
	auto p = reinterpret_cast<const uint8_t*>(buf);
	auto bytes_written = 0;

	while (len > 0)
	{
		if (!Write(p, len, &bytes_written))
			return false;

		len -= bytes_written;
		p += bytes_written;
	}
	return true;
}

bool SuperCardPro::SendCmd (uint8_t cmd, void *buf, int len, void *bulkbuf, int bulklen)
{
	auto p = reinterpret_cast<uint8_t*>(buf);
	uint8_t datasum = CHECKSUM_INIT;
	for (auto i = 0; i < len; ++i)
		datasum += p[i];

	Data data(2 + len + 1);
	data[0] = cmd;
	data[1] = static_cast<uint8_t>(len);
	if (len) memcpy(&data[2], buf, len);
	data[2 + len] = data[0] + data[1] + datasum;

	if (!WriteExact(&data[0], data.size()))
		return false;

	if (cmd == CMD_LOADRAM_USB && !WriteExact(bulkbuf, bulklen))
		return false;
	else if (cmd == CMD_SENDRAM_USB && !ReadExact(bulkbuf, bulklen))
		return false;

	uint8_t result[2];
	if (!ReadExact(result, sizeof(result)))
		return false;

	if (result[0] != cmd || result[1] != pr_Ok)
	{
		m_error = result[1];
		return false;
	}

	m_error = result[1];	// pr_Ok
	return true;
}

bool SuperCardPro::SelectDrive (int drive)
{
	auto drv_idx = static_cast<uint8_t>(drive);

	return SendCmd(CMD_SELA + drv_idx) &&
		SendCmd(CMD_MTRAON + drv_idx);
}

bool SuperCardPro::DeselectDrive (int drive)
{
	auto drv_idx = static_cast<uint8_t>(drive);

	return SendCmd(CMD_MTRAOFF + drv_idx) &&
		SendCmd(CMD_DSELA + drv_idx);
}

bool SuperCardPro::SelectDensity (int /*density*/)
{
	// ToDo
	return false;
}

bool SuperCardPro::Seek (int cyl_, int head_)
{
	auto cyl = static_cast<uint8_t>(cyl_);
	auto head = static_cast<uint8_t>(head_);

	return SendCmd(CMD_SIDE, &head, sizeof(head)) &&
		(cyl ? SendCmd(CMD_STEPTO, &cyl, sizeof(cyl)) : SendCmd(CMD_SEEK0));
}

bool SuperCardPro::ReadFlux (int revs, std::vector<std::vector<uint32_t>> &flux_revs)
{
	revs = std::max(1, std::min(revs, MAX_FLUX_REVS));

	uint8_t info[2] = { static_cast<uint8_t>(revs), ff_Index };
	if (!SendCmd(CMD_READFLUX, info, sizeof(info)))
		return false;

	if (!SendCmd(CMD_GETFLUXINFO, nullptr, 0))
		return false;

	uint32_t rev_index[MAX_FLUX_REVS * 2];
	if (!ReadExact(rev_index, sizeof(rev_index)))
		return false;

	uint32_t flux_offset = 0;
	flux_revs.clear();

	for (auto i = 0; i < revs; ++i)
	{
//		auto index_time = util::betoh(rev_index[i*2 + 0]);
		auto flux_count = util::betoh(rev_index[i * 2 + 1]);
		auto flux_bytes = static_cast<uint32_t>(flux_count * sizeof(uint16_t));

		std::vector<uint16_t> flux_data(flux_count);	// NB: time values are big-endian

		uint32_t start_len[2] { util::htobe(flux_offset), util::htobe(flux_bytes) };
		if (!SendCmd(CMD_SENDRAM_USB, &start_len, sizeof(start_len), flux_data.data(), flux_bytes))
			return false;

		flux_offset += flux_bytes;

		std::vector<uint32_t> flux_times;
		flux_times.reserve(flux_count);

		uint32_t total_time = 0;
		for (auto time : flux_data)
		{
			if (!time)
				total_time += 0x10000;
			else
			{
				total_time += util::betoh<uint16_t>(time);
				flux_times.push_back(total_time * NS_PER_BITCELL);	// 25ns per bit cell
				total_time = 0;
			}
		}

		flux_revs.push_back(std::move(flux_times));
	}

	return true;
}

bool SuperCardPro::WriteFlux (const void *flux, int nr_bitcells)
{
	uint32_t length = nr_bitcells * sizeof(uint16_t);

	uint32_t start_len[2];
	start_len[0] = 0;
	start_len[1] = util::htobe(static_cast<uint32_t>(length));
	if (!SendCmd(CMD_LOADRAM_USB, &start_len, sizeof(start_len), const_cast<void *>(flux), length))
		return false;

	uint8_t params[5] = {};
	params[0] = static_cast<uint8_t>(nr_bitcells >> 24);	// big endian
	params[1] = static_cast<uint8_t>(nr_bitcells >> 16);
	params[2] = static_cast<uint8_t>(nr_bitcells >> 8);
	params[3] = static_cast<uint8_t>(nr_bitcells);
	params[4] = ff_Wipe | ff_Index;

	if (!SendCmd(CMD_WRITEFLUX, &params, sizeof(params)))
		return false;

	return true;
}

bool SuperCardPro::GetInfo (uint8_t *hwversion, uint8_t *fwversion)
{
	uint8_t version[2] = {};
	if (SendCmd(CMD_SCPINFO, &version, sizeof(version)))
	{
		*hwversion = version[0];
		*fwversion = version[1];
		return true;
	}
	return false;
}

bool SuperCardPro::RamTest ()
{
	return SendCmd(CMD_RAMTEST);
}


std::string SuperCardPro::GetErrorStatusText () const
{
	switch (m_error)
	{
		case pr_Unused:			return "null response";
		case pr_BadCommand:		return "bad command";
		case pr_CommandErr:		return "command error";
		case pr_Checksum:		return "packet checksum failed";
		case pr_Timeout:		return "USB timeout";
		case pr_NoTrk0:			return "track 0 not found";
		case pr_NoDriveSel:		return "no drive selected";
		case pr_NoMotorSel:		return "motor not enabled";
		case pr_NotReady:		return "drive not ready";
		case pr_NoIndex:		return "no index pulse detected";
		case pr_ZeroRevs:		return "zero revolutions chosen";
		case pr_ReadTooLong:	return "read data too big";
		case pr_BadLength:		return "invalid length";
		case pr_BadData:		return "bit cell time is invalid";
		case pr_BoundaryOdd:	return "location boundary is odd";
		case pr_WPEnabled:		return "disk is write protected";
		case pr_BadRAM:			return "RAM test failed";
		case pr_NoDisk:			return "no disk in drive";
		case pr_BadBaud:		return "bad baud rate selected";
		case pr_BadCmdOnPort:	return "bad command for port type";
		case pr_Ok:				return "OK";
	}

	return util::fmt("unknown (%02X)", m_error);
}
