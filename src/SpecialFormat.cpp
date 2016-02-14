// Copy-protected formats that require special support

#include "SAMdisk.h"
#include "IBMPC.h"

// KBI-19 protection for CPC? (19 valid sectors)
bool IsKBI19Track (const Track &track)
{
	uint32_t sector_mask = 0;

	if (track.size() != 19)
		return false;

	for (auto &s : track.sectors())
	{
		if (s.datarate != DataRate::_250K || s.encoding != Encoding::MFM || s.size() != 512)
			return false;

		sector_mask |= (1 << s.header.sector);
	}

	// Sectors must be numbered 0 to 18
	if (sector_mask != ((1 << 19) - 1))
		return false;

	if (opt.debug) util::cout << "detected KBI-19 track\n";
	return true;
}

// Sega System 24 track?
bool IsSystem24Track (const Track &track)
{
	static uint8_t sizes[] = { 6, 3, 3, 3, 2, 1 };
	auto i = 0;

	if (track.size() != 6)
		return false;

	for (auto &s : track.sectors())
	{
		if (s.datarate != DataRate::_500K || s.encoding != Encoding::MFM ||
			s.header.size != sizes[i++] || !s.has_data())
			return false;
	}

	if (opt.debug) util::cout << "detected System-24 track\n";
	return true;
}

// Speedlock weak sector for Spectrum +3?
bool IsSpectrumSpeedlockTrack (const Track &track, int &random_offset)
{
	if (track.size() != 9)
		return false;

	auto &sector0 = track[0];
	auto &sector1 = track[1];

	if (sector0.encoding != Encoding::MFM || sector1.encoding != Encoding::MFM ||
		sector0.datarate != DataRate::_250K || sector1.datarate != DataRate::_250K ||
		sector0.size() != 512 || sector1.size() != 512 ||
		sector0.data_size() < 512 || sector1.data_size() < 512 ||
		!sector1.has_baddatacrc())	// weak sector
		return false;

	auto &data0 = sector0.data_copy();
	auto &data1 = sector1.data_copy();

	// Check for signature in the 2 known positions
	if (memcmp(data0.data() + 304, "SPEEDLOCK", 9) && memcmp(data0.data() + 176, "SPEEDLOCK", 9))
		return false;

	// If there's no common block at the start, assume fully random
	// Buggy Boy has only 255, so don't check the full first half!
	if (memcmp(data1.data(), data1.data() + 1, (sector1.size() / 2) - 1))
		random_offset = 5;		// -512
	else
		random_offset = 336;	// =256 -33 +47 -176

	if (opt.debug) util::cout << "detected Spectrum Speedlock track\n";
	return true;
}

// Speedlock weak sector for Amstrad CPC?
bool IsCpcSpeedlockTrack (const Track &track, int &random_offset)
{
	if (track.size() != 9)
		return false;

	auto &sector0 = track[0];
	auto &sector7 = track[7];

	if (sector0.encoding != Encoding::MFM || sector7.encoding != Encoding::MFM ||
		sector0.datarate != DataRate::_250K || sector7.datarate != DataRate::_250K ||
		sector0.size() != 512 || sector7.size() != 512 ||
		sector0.data_size() < 512 || sector7.data_size() < 512 ||
		!sector7.has_baddatacrc())	// weak sector
		return false;

	auto &data0 = sector0.data_copy();
	auto &data7 = sector7.data_copy();

	// Check for signature in the boot sector
	if (memcmp(data0.data() + 257, "SPEEDLOCK", 9) && memcmp(data0.data() + 129, "SPEEDLOCK", 9))
	{
		// If that's missing, look for a code signature
		if (memcmp(data0.data() + 208, "\x4a\x00\x09\x46\x00\x00\x00\x42\x02\x47\x2a\xff", 12) ||
			CRC16(data0.data() + 49, 220 - 49) != 0x62c2)
			return false;
	}

	// If there's no common block at the start, assume fully random
	// Buggy Boy has only 255, so don't check the full first half!
	if (memcmp(data7.data(), data7.data() + 1, (sector7.size() / 2) - 1))
		random_offset = 5;	// -512
	else if (data0[129] == 'S')
		random_offset = 256;
	else
		random_offset = 336;	// =256 -33 +47 -176

	if (opt.debug) util::cout << "detected CPC Speedlock track\n";
	return true;
}

// Rainbow Arts weak sector for CPC?
bool IsRainbowArtsTrack (const Track &track, int &random_offset)
{
	if (track.size() != 9)
		return false;

	auto &sector1 = track[1];
	auto &sector3 = track[3];

	if (sector1.encoding != Encoding::MFM || sector3.encoding != Encoding::MFM ||
		sector1.datarate != DataRate::_250K || sector3.datarate != DataRate::_250K ||
		sector1.size() != 512 || sector3.size() != 512 ||
		sector1.data_size() < 512 || sector3.data_size() < 512 ||
		sector1.header.sector != 198 || !sector1.has_baddatacrc())	// weak sector 198
		return false;

	auto &data3 = sector3.data_copy();

	// Check for code signature at the start of the 4th sector
	if (memcmp(data3.data(), "\x2a\x6d\xa7\x01\x30\x01\xaf\xed\x42\x4d\x44\x21\x70\x01", 14))
		return false;

	// The first 100 bytes are constant
	random_offset = 100;	// =100 -258 +151 -3

	if (opt.debug) util::cout << "detected Rainbow Arts weak sector track\n";
	return true;
}

// KBI-10 weak sector for CPC?
bool IsKBI10Track (const Track &track)
{
	if (track.size() != 10)
		return false;

	auto &sector0 = track[0];
	auto &sector9 = track[9];

	if (sector0.encoding != Encoding::MFM || sector9.encoding != Encoding::MFM ||
		sector0.datarate != DataRate::_250K || sector9.datarate != DataRate::_250K ||
		sector0.size() != 512 ||
		sector0.data_size() < 512 || sector9.data_size() < 256 ||
		sector9.header.size != 1 || !sector9.has_baddatacrc())	// 256-byte weak sector
		return false;

	auto &data9 = sector9.data_copy();

	// Check for the signature at the start of the weak sector
	if (memcmp(data9.data(), "KBI", 3))
		return false;

	// The weak patches at offsets 4 and 128+4 ?
	// =4 -4 =124 -4 =120

	if (opt.debug) util::cout << "detected KBI-10 track\n";
	return true;
}

// Logo Professor track?
bool IsLogoProfTrack (const Track &track)
{
	// Accept track with or without placeholder
	if (track.size() != 10 && track.size() != 11)
		return false;

	for (auto &s : track.sectors())
	{
		// Ignore placeholder sector
		if (s.has_badidcrc() && s.header.sector == 1)
			continue;

		// Ensure each sector is double-density MFM, 512-bytes, with good data
		if (s.datarate != DataRate::_250K || s.encoding != Encoding::MFM ||
			s.size() != 512 || !s.has_data() || !s.has_baddatacrc())
			return false;
	}

	// If there's no placeholder, the first sector must begin late
	if (track.size() == 10)
	{
		// Determine an offset considered late
		auto min_offset = Sector::SizeCodeToLength(1) + GetSectorOverhead(Encoding::MFM);

		// Reject if first sector doesn't start late on the track
		if (track[0].offset < min_offset)
			return false;
	}

	if (opt.debug) util::cout << "detected Logo Professor track\n";
	return true;
}
