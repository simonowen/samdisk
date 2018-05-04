// SuperCard Pro device base class

#include "SAMdisk.h"
#include "SuperCardPro.h"

#ifdef HAVE_FTD2XX
#include "SCP_FTD2XX.h"
#endif
#ifdef HAVE_FTDI
#include "SCP_FTDI.h"
#endif
#include "SCP_USB.h"
#ifdef _WIN32
#include "SCP_Win32.h"
#endif

// Storage for class statics.
const int SuperCardPro::MAX_FLUX_REVS;	// SCP firmware hard limit


std::unique_ptr<SuperCardPro> SuperCardPro::Open ()
{
	std::unique_ptr<SuperCardPro> p;

#ifdef HAVE_FTD2XX
	if (!p)
		p = SuperCardProFTD2XX::Open();
#endif
#ifdef HAVE_FTDI
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

#ifndef _WIN32
	// ToDo: find why the Raspberry Pi needs this.
	usleep(5000);
#endif

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

	if (result[0] != cmd)
		throw util::exception("SCP result command mismatch");

	if (result[1] != pr_Ok)
	{
		m_error = result[1];
		return false;
	}

	m_error = result[1];	// pr_Ok
	return true;
}


bool SuperCardPro::SelectDrive (int drive)
{
	return SendCmd(CMD_SELA + (drive ? 1 : 0));
}

bool SuperCardPro::DeselectDrive (int drive)
{
	return SendCmd(CMD_DSELA + (drive ? 1 : 0));
}

bool SuperCardPro::EnableMotor (int drive)
{
	return SendCmd(CMD_MTRAON + (drive ? 1 : 0));
}

bool SuperCardPro::DisableMotor (int drive)
{
	return SendCmd(CMD_MTRAOFF + (drive ? 1 : 0));
}

bool SuperCardPro::Seek0 ()
{
	return SendCmd(CMD_SEEK0);
}

bool SuperCardPro::StepTo (int cyl)
{
	auto track = static_cast<uint8_t>(cyl);

	return track ? SendCmd(CMD_STEPTO, &track, sizeof(track)) : Seek0();
}

bool SuperCardPro::StepIn ()
{
	return SendCmd(CMD_STEPIN);
}

bool SuperCardPro::StepOut ()
{
	return SendCmd(CMD_STEPOUT);
}

bool SuperCardPro::SelectDensity (bool high)
{
	uint8_t density = high ? 1 : 0;

	return SendCmd(CMD_SELDENS, &density, sizeof(density));
}

bool SuperCardPro::SelectSide (int head)
{
	uint8_t side = head ? 1 : 0;

	return SendCmd(CMD_SIDE, &side, sizeof(side));
}

bool SuperCardPro::GetDriveStatus (int &status)
{
	uint16_t drv_status;

	if (!SendCmd(CMD_STATUS))
		return false;

	if (!ReadExact(&drv_status, sizeof(drv_status)))
		return false;

	status = util::betoh(drv_status);

	return true;
}

bool SuperCardPro::GetParameters (int &drive_select_delay, int &step_delay, int &motor_on_delay, int &seek_0_delay, int &motor_off_delay)
{
	if (!SendCmd(CMD_GETPARAMS))
		return false;

	uint16_t params[5] = {};
	if (!ReadExact(params, sizeof(params)))
		return false;

	drive_select_delay = util::betoh(params[0]);
	step_delay = util::betoh(params[1]);
	motor_on_delay = util::betoh(params[2]);
	seek_0_delay = util::betoh(params[3]);
	motor_off_delay = util::betoh(params[4]);

	return true;
}

bool SuperCardPro::SetParameters (int drive_select_delay_us, int step_delay_us, int motor_on_delay_ms, int seek_0_delay_ms, int motor_off_delay_ms)
{
	uint16_t params[5] = {};

	params[0] = util::betoh(static_cast<uint16_t>(drive_select_delay_us));
	params[1] = util::betoh(static_cast<uint16_t>(step_delay_us));
	params[2] = util::betoh(static_cast<uint16_t>(motor_on_delay_ms));
	params[3] = util::betoh(static_cast<uint16_t>(seek_0_delay_ms));
	params[4] = util::betoh(static_cast<uint16_t>(motor_off_delay_ms));

	return SendCmd(CMD_SETPARAMS, params, sizeof(params));
}

bool SuperCardPro::RamTest ()
{
	return SendCmd(CMD_RAMTEST);
}

bool SuperCardPro::SetPin33 (bool high)
{
	uint8_t value = high ? 1 : 0;

	return SendCmd(CMD_SETPIN33, &value, sizeof(value));
}

bool SuperCardPro::ReadFlux (int revs, FluxData &flux_revs)
{
	// Read at least 2 revolutions, as a sector data may span index position
	revs = std::max(2, std::min(revs, MAX_FLUX_REVS));

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
				flux_times.push_back(total_time * NS_PER_TICK);
				total_time = 0;
			}
		}

		flux_revs.push_back(std::move(flux_times));
	}

	return true;
}

bool SuperCardPro::WriteFlux (const std::vector<uint32_t> &flux_times)
{
	std::vector<uint16_t> flux_data;
	flux_data.reserve(flux_times.size());
	for (auto time_ns : flux_times)
	{
		auto time_ticks{(time_ns + (NS_PER_TICK / 2)) / NS_PER_TICK};
		time_ticks = time_ticks * opt.scale / 100;
		time_ticks |= 1;

		while (time_ticks >= 0x10000)
		{
			flux_data.push_back(0);
			time_ticks -= 0x10000;
		}

		flux_data.push_back(util::htobe(static_cast<uint16_t>(time_ticks)));
	}

	if (flux_data.empty())
		flux_data.push_back(4'000 / NS_PER_TICK);

	auto flux_count{flux_data.size()};
	auto flux_bytes{flux_count * sizeof(flux_data[0])};

	uint32_t start_len[2];
	start_len[0] = 0;
	start_len[1] = util::htobe(static_cast<uint32_t>(flux_bytes));
	if (!SendCmd(CMD_LOADRAM_USB, &start_len, static_cast<int>(sizeof(start_len)), flux_data.data(), static_cast<int>(flux_bytes)))
		return false;

	uint8_t params[5] = {};
	params[0] = static_cast<uint8_t>(flux_count >> 24);	// big endian
	params[1] = static_cast<uint8_t>(flux_count >> 16);
	params[2] = static_cast<uint8_t>(flux_count >> 8);
	params[3] = static_cast<uint8_t>(flux_count);
	params[4] = ff_Wipe | ff_Index;

	if (!SendCmd(CMD_WRITEFLUX, &params, sizeof(params)))
		return false;

	return true;
}

bool SuperCardPro::GetInfo (int &hwversion, int &fwversion)
{
	uint8_t version[2] = {};
	if (!SendCmd(CMD_SCPINFO))
		return false;

	if (!ReadExact(&version, sizeof(version)))
		return false;

	hwversion = version[0];
	fwversion = version[1];

	return true;
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
