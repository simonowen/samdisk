// Copy-protected formats that require special support
//
// Contains detection and generation for each track format.
// Output will be in bitstream or flux format (or both),
// depending on the format requirements.

#include "SAMdisk.h"
#include "IBMPC.h"
#include "BitstreamTrackBuilder.h"
#include "FluxTrackBuilder.h"

////////////////////////////////////////////////////////////////////////////////

bool IsEmptyTrack (const Track &track)
{
	return track.size() == 0;
}

TrackData GenerateEmptyTrack (const CylHead &cylhead, const Track &track)
{
	assert(IsEmptyTrack(track));
	(void)track;

	// Generate a DD track full of gap filler. It shouldn't really matter
	// which datarate and encoding as long as there are no sync marks.
	BitstreamTrackBuilder bitbuf(DataRate::_250K, Encoding::MFM);
	bitbuf.addBlock(0x4e, 6250);

	return TrackData(cylhead, std::move(bitbuf.buffer()));
}

////////////////////////////////////////////////////////////////////////////////

// KBI-19 protection for CPC? (19 valid sectors)
bool IsKBI19Track (const Track &track)
{
	static const uint8_t ids[]{ 0,1,4,7,10,13,16,2,5,8,11,14,17,3,6,9,12,15,18 };

	if (track.size() != arraysize(ids))
		return false;

	int idx = 0;
	for (auto &s : track.sectors())
	{
		if (s.datarate != DataRate::_250K || s.encoding != Encoding::MFM ||
			s.header.sector != ids[idx++] || s.size() != 512 || !s.has_good_data())
			return false;
	}

	if (opt.debug) util::cout << "detected KBI-19 track\n";
	return true;
}

TrackData GenerateKBI19Track (const CylHead &cylhead, const Track &track)
{
	assert(IsKBI19Track(track));

	static const Data gap2_sig{ 0x20,0x4B,0x42,0x49,0x20 };	// " KBI "
	BitstreamTrackBuilder bitbuf(DataRate::_250K, Encoding::MFM);

	// Track start with slightly shorter gap4a.
	bitbuf.addGap(64);
	bitbuf.addIAM();
	bitbuf.addGap(50);

	int sector_index = 0;
	for (auto &s : track)
	{
		bitbuf.addSectorHeader(s.header);

		if (s.header.sector == 0)
		{
			bitbuf.addGap(17);
			bitbuf.addBlock(gap2_sig);
		}
		else
		{
			bitbuf.addGap(8);
			bitbuf.addBlock(gap2_sig);
			bitbuf.addGap(9);
		}

		bitbuf.addAM(s.dam);
		auto data = s.data_copy();

		// Short or full sector data?
		if (sector_index++ % 3)
		{
			data.resize(61);
			bitbuf.addBlock(data);
		}
		else
		{
			data.resize(s.size());
			bitbuf.addBlock(data);
			bitbuf.addCrc(3 + 1 + 512);

			if (s.header.sector != 0)
				bitbuf.addGap(80);
		}
	}

	// Pad up to normal track size.
	bitbuf.addGap(90);

	return TrackData(cylhead, std::move(bitbuf.buffer()));
}

////////////////////////////////////////////////////////////////////////////////

// Sega System 24 track? (currently just 0x2f00 variant)
bool IsSystem24Track (const Track &track)
{
	static const uint8_t sizes[] = { 4,4,4,4,4,3,1 };
	auto i = 0;

	if (track.size() != arraysize(sizes))
		return false;

	for (auto &s : track)
	{
		if (s.datarate != DataRate::_500K || s.encoding != Encoding::MFM ||
			s.header.size != sizes[i++] || !s.has_data())
			return false;
	}

	if (opt.debug) util::cout << "detected System-24 track\n";
	return true;
}

TrackData GenerateSystem24Track (const CylHead &cylhead, const Track &track)
{
	assert(IsSystem24Track(track));

	BitstreamTrackBuilder bitbuf(DataRate::_500K, Encoding::MFM);

	for (auto &s : track)
	{
		auto gap3{ (s.header.sector < 6) ? 52 : 41 };
		bitbuf.addSector(s, gap3);
	}

	return TrackData(cylhead, std::move(bitbuf.buffer()));
}

////////////////////////////////////////////////////////////////////////////////

// Speedlock weak sector for Spectrum +3?
bool IsSpectrumSpeedlockTrack(const Track &track, int &weak_offset, int &weak_size)
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
	{
		// -512
		weak_offset = 0;
		weak_size = 512;
	}
	else
	{
		// =256 -33 +47 -176
		weak_offset = 336;
		weak_size = 32;
	}

	if (opt.debug) util::cout << "detected Spectrum Speedlock track\n";
	return true;
}

TrackData GenerateSpectrumSpeedlockTrack (const CylHead &cylhead, const Track &track, int weak_offset, int weak_size)
{
#ifdef _DEBUG
	int temp_offset, temp_size;
	assert(IsSpectrumSpeedlockTrack(track, temp_offset, temp_size));
	assert(weak_offset == temp_offset && weak_size == temp_size);
#endif

	FluxTrackBuilder fluxbuf(cylhead, DataRate::_250K, Encoding::MFM);
	fluxbuf.addTrackStart();

	BitstreamTrackBuilder bitbuf(DataRate::_250K, Encoding::MFM);
	bitbuf.addTrackStart();

	for (auto &sector : track)
	{
		auto &data_copy = sector.data_copy();
		auto is_weak{ &sector == &track[1] };

		if (!is_weak)
			fluxbuf.addSector(sector.header, data_copy, 0x54, sector.dam);
		else
		{
			fluxbuf.addSectorUpToData(sector.header, sector.dam);
			fluxbuf.addBlock(Data(data_copy.begin(), data_copy.begin() + weak_offset));
			fluxbuf.addWeakBlock(weak_size);
			fluxbuf.addBlock(Data(
				data_copy.begin() + weak_offset + weak_size,
				data_copy.begin() + sector.size()));
		}

		bitbuf.addSector(sector.header, data_copy, 0x2e, sector.dam, is_weak);

		// Add duplicate weak sector half way around track.
		if (&sector == &track[5])
		{
			auto &sector1{ track[1] };
			auto data1{ sector1.data_copy() };
			std::fill(data1.begin() + weak_offset, data1.begin() + weak_offset + weak_size, uint8_t(0xee));
			bitbuf.addSector(sector1.header, data1, 0x2e, sector1.dam, true);
		}
	}

	TrackData trackdata(cylhead);
	trackdata.add(std::move(bitbuf.buffer()));
	//trackdata.add(FluxData({ fluxbuf.buffer() }));
	return trackdata;
}

////////////////////////////////////////////////////////////////////////////////

// Speedlock weak sector for Amstrad CPC?
bool IsCpcSpeedlockTrack (const Track &track, int &weak_offset, int &weak_size)
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
	{
		// -512
		weak_offset = 0;
		weak_size = 512;
	}
	else if (data0[129] == 'S')
	{
		// =256 -256
		weak_offset = 256;
		weak_size = 256;
	}
	else
	{
		// =256 -33 +47 -176
		weak_offset = 336;
		weak_size = 32;
	}

	if (opt.debug) util::cout << "detected CPC Speedlock track\n";
	return true;
}

TrackData GenerateCpcSpeedlockTrack (const CylHead &cylhead, const Track &track, int weak_offset, int weak_size)
{
#ifdef _DEBUG
	int temp_offset, temp_size;
	assert(IsCpcSpeedlockTrack(track, temp_offset, temp_size));
	assert(weak_offset == temp_offset && weak_size == temp_size);
#endif

	FluxTrackBuilder fluxbuf(cylhead, DataRate::_250K, Encoding::MFM);
	fluxbuf.addTrackStart();

	BitstreamTrackBuilder bitbuf(DataRate::_250K, Encoding::MFM);
	bitbuf.addTrackStart();

	for (auto &sector : track)
	{
		auto &data_copy = sector.data_copy();
		auto is_weak{ &sector == &track[7] };

		if (!is_weak)
			fluxbuf.addSector(sector.header, data_copy, 0x54, sector.dam);
		else
		{
			fluxbuf.addSectorUpToData(sector.header, sector.dam);
			fluxbuf.addBlock(Data(data_copy.begin(), data_copy.begin() + weak_offset));
			fluxbuf.addWeakBlock(weak_size);
			fluxbuf.addBlock(Data(
				data_copy.begin() + weak_offset + weak_size,
				data_copy.begin() + sector.size()));
		}

		bitbuf.addSector(sector.header, data_copy, 0x2e, sector.dam, is_weak);

		// Add duplicate weak sector half way around track.
		if (&sector == &track[1])
		{
			auto &sector7{ track[7] };
			auto data7{ sector7.data_copy() };
			std::fill(data7.begin() + weak_offset, data7.begin() + weak_offset + weak_size, uint8_t(0xee));
			bitbuf.addSector(sector7.header, data7, 0x2e, sector7.dam, true);
		}
	}

	TrackData trackdata(cylhead);
	trackdata.add(std::move(bitbuf.buffer()));
	//trackdata.add(FluxData({ fluxbuf.buffer() }));
	return trackdata;
}

////////////////////////////////////////////////////////////////////////////////

// Rainbow Arts weak sector for CPC?
bool IsRainbowArtsTrack (const Track &track, int &weak_offset, int &weak_size)
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
	weak_offset = 100;	// =100 -258 +151 -3
	weak_size = 256;

	if (opt.debug) util::cout << "detected Rainbow Arts weak sector track\n";
	return true;
}

TrackData GenerateRainbowArtsTrack (const CylHead &cylhead, const Track &track, int weak_offset, int weak_size)
{
#ifdef _DEBUG
	int temp_offset, temp_size;
	assert(IsRainbowArtsTrack(track, temp_offset, temp_size));
	assert(weak_offset == temp_offset && weak_size == temp_size);
#endif

	FluxTrackBuilder fluxbuf(cylhead, DataRate::_250K, Encoding::MFM);
	fluxbuf.addTrackStart();

	BitstreamTrackBuilder bitbuf(DataRate::_250K, Encoding::MFM);
	bitbuf.addTrackStart();

	for (auto &sector : track)
	{
		auto &data_copy = sector.data_copy();
		auto is_weak{ &sector == &track[1] };

		if (!is_weak)
			fluxbuf.addSector(sector.header, data_copy, 0x54, sector.dam);
		else
		{
			fluxbuf.addSectorUpToData(sector.header, sector.dam);
			fluxbuf.addBlock(Data(data_copy.begin(), data_copy.begin() + weak_offset));
			fluxbuf.addWeakBlock(weak_size);
			fluxbuf.addBlock(Data(
				data_copy.begin() + weak_offset + weak_size,
				data_copy.begin() + sector.size()));
		}

		bitbuf.addSector(sector.header, data_copy, 0x2e, sector.dam, is_weak);

		// Add duplicate weak sector half way around track.
		if (&sector == &track[5])
		{
			// Add a duplicate of the weak sector, with different data from the weak position
			auto &sector1{ track[1] };
			auto data1{ sector1.data_copy() };
			std::fill(data1.begin() + weak_offset, data1.begin() + weak_offset + weak_size, uint8_t(0xee));
			bitbuf.addSector(sector1.header, data1, 0x2e, sector1.dam, true);
		}
	}

	TrackData trackdata(cylhead);
	trackdata.add(std::move(bitbuf.buffer()));
	//trackdata.add(FluxData({ fluxbuf.buffer() }));
	return trackdata;
}

////////////////////////////////////////////////////////////////////////////////

// KBI-10 weak sector for CPC?
bool IsKBIWeakSectorTrack (const Track &track, int &weak_offset, int &weak_size)
{
	auto sectors = track.size();

	// Most titles use the 10-sector version, but some have 3x2K sectors.
	int size_code;
	if (sectors == 3)
		size_code = 4;
	else if (sectors == 10)
		size_code = 2;
	else
		return false;

	// Weak sector is always last on track and 256 bytes
	auto &sectorW = track[sectors - 1];
	if (sectorW.encoding != Encoding::MFM ||
		sectorW.datarate != DataRate::_250K ||
		sectorW.header.size != 1 ||
		sectorW.data_size() < 256 ||
		!sectorW.has_baddatacrc())
	{
		return false;
	}

	// The remaining sector must be the correct type and size code.
	for (int i = 0; i < sectors - 1; ++i)
	{
		auto &sector = track[i];
		if (sector.encoding != Encoding::MFM ||
			sector.datarate != DataRate::_250K ||
			sector.header.size != size_code ||
			sector.data_size() < Sector::SizeCodeToLength(size_code))
		{
			return false;
		}
	}

	auto &dataW = sectorW.data_copy();

	// The first character of the weak sector is 'K', and the next two are alphabetic.
	if (dataW[0] != 'K' ||
		!std::isalpha(static_cast<uint8_t>(dataW[1])) ||
		!std::isalpha(static_cast<uint8_t>(dataW[2])))
	{
		return false;
	}

	// =4 -4 =124 -4 =120
	weak_offset = 4;
	weak_size = 4;

	if (opt.debug) util::cout << "detected KBI weak sector track\n";
	return true;
}

TrackData GenerateKBIWeakSectorTrack (const CylHead &cylhead, const Track &track, int weak_offset, int weak_size)
{
#ifdef _DEBUG
	int temp_offset, temp_size;
	assert(IsKBIWeakSectorTrack(track, temp_offset, temp_size));
	assert(weak_offset == temp_offset && weak_size == temp_size);
#endif

	FluxTrackBuilder fluxbuf(cylhead, DataRate::_250K, Encoding::MFM);
	fluxbuf.addTrackStart();

	BitstreamTrackBuilder bitbuf(DataRate::_250K, Encoding::MFM);
	bitbuf.addTrackStart();

	auto sectors = track.size();
	for (auto &sector : track)
	{
		auto &data_copy = sector.data_copy();
		auto is_weak = sector.header.size == 1;

		if (!is_weak)
			fluxbuf.addSector(sector.header, data_copy, 0x54, sector.dam);
		else
		{
			fluxbuf.addSectorUpToData(sector.header, sector.dam);
			fluxbuf.addBlock(Data(data_copy.begin(), data_copy.begin() + weak_offset));
			fluxbuf.addWeakBlock(weak_size);
			fluxbuf.addBlock(Data(
				data_copy.begin() + weak_offset + weak_size,
				data_copy.begin() + sector.size()));
		}

		bitbuf.addSector(sector.header, data_copy, 1, sector.dam, is_weak);

		// Insert the duplicate sector earlier on the track.
		if (&sector == &track[((sectors - 1) / 2) - 1])
		{
			auto &sectorW = track[sectors - 1];
			auto dataW{ sectorW.data_copy() };
			std::fill(dataW.begin() + weak_offset, dataW.begin() + weak_offset + weak_size, uint8_t(0xee));
			bitbuf.addSector(sectorW.header, dataW, 1, sectorW.dam, true);
		}
	}

	TrackData trackdata(cylhead);
	trackdata.add(std::move(bitbuf.buffer()));
	//trackdata.add(FluxData({ fluxbuf.buffer() }));
	return trackdata;
}

////////////////////////////////////////////////////////////////////////////////

// Logo Professor track?
bool IsLogoProfTrack (const Track &track)
{
	// Accept track with or without placeholder sector
	if (track.size() != 10 && track.size() != 11)
		return false;

	for (auto &s : track)
	{
		// Ignore placeholder sector
		if (s.has_badidcrc() && s.header.sector == 1)
			continue;

		// Ensure each sector is double-density MFM, 512-bytes, with good data
		if (s.datarate != DataRate::_250K || s.encoding != Encoding::MFM ||
			s.size() != 512 || !s.has_data() || s.has_baddatacrc())
			return false;
	}

	// If there's no placeholder, the first sector must begin late
	if (track.size() == 10)
	{
		// Determine an offset considered late
		auto min_offset = Sector::SizeCodeToLength(1) + GetSectorOverhead(Encoding::MFM);

		// Reject if first sector doesn't start late on the track
		if (track[0].offset < (min_offset * 16))
			return false;
	}

	if (opt.debug) util::cout << "detected Logo Professor track\n";
	return true;
}

TrackData GenerateLogoProfTrack (const CylHead &cylhead, const Track &track)
{
	assert(IsLogoProfTrack(track));

	BitstreamTrackBuilder bitbuf(DataRate::_250K, Encoding::MFM);
	bitbuf.addTrackStart();
	bitbuf.addGap(600);

	for (auto &sector : track)
		bitbuf.addSector(sector, 0x20);

	return TrackData(cylhead, std::move(bitbuf.buffer()));
}

////////////////////////////////////////////////////////////////////////////////

// OperaSoft track with 32K sector
bool IsOperaSoftTrack (const Track &track)
{
	uint32_t sector_mask = 0;
	int i = 0;

	if (track.size() != 9)
		return false;

	for (auto &s : track)
	{
		if (s.datarate != DataRate::_250K || s.encoding != Encoding::MFM)
			return false;

		static const uint8_t sizes[] = { 1,1,1,1,1,1,1,1,8 };
		if (s.header.size != sizes[i++])
			return false;

		sector_mask |= (1 << s.header.sector);
	}

	// Sectors must be numbered 0 to 8
	if (sector_mask != ((1 << 9) - 1))
		return false;

	if (opt.debug) util::cout << "detected OperaSoft track with 32K sector\n";
	return true;
}

TrackData GenerateOperaSoftTrack (const CylHead &cylhead, const Track &track)
{
	assert(IsOperaSoftTrack(track));

	BitstreamTrackBuilder bitbuf(DataRate::_250K, Encoding::MFM);
	bitbuf.addTrackStart();

	for (auto &sector : track)
	{
		if (sector.header.sector != 8)
			bitbuf.addSector(sector, 0xf0);
		else
		{
			auto &sector7 = track[7];
			auto &sector8 = track[8];

			bitbuf.addSectorUpToData(sector8.header, sector8.dam);
			bitbuf.addBlock(Data(256, 0x55));
			bitbuf.addCrc(4 + 256);
			bitbuf.addBlock(Data(0x512 - 256 - 2, 0x4e));
			bitbuf.addBlock(sector7.data_copy());
		}
	}

	return TrackData(cylhead, std::move(bitbuf.buffer()));
}

////////////////////////////////////////////////////////////////////////////////

// 8K sector track?
bool Is8KSectorTrack (const Track &track)
{
	// There must only be 1 sector.
	if (track.size() != 1)
		return false;

	auto &sector{ track[0] };
	if (sector.datarate != DataRate::_250K || sector.encoding != Encoding::MFM ||
		sector.size() != 8192 || !sector.has_data())
		return false;

	if (opt.debug) util::cout << "detected 8K sector track\n";
	return true;
}

TrackData Generate8KSectorTrack (const CylHead &cylhead, const Track &track)
{
	assert(Is8KSectorTrack(track));

	BitstreamTrackBuilder bitbuf(DataRate::_250K, Encoding::MFM);
	bitbuf.addGap(16);	// gap 4a
	bitbuf.addIAM();
	bitbuf.addGap(16);	// gap 1

	auto &sector{ track[0] };
	bitbuf.addSectorUpToData(sector.header, sector.dam);

	// Maximum size of long-track version used by Coin-Op Hits
	static constexpr auto max_size{ 0x18a3 };
	auto data = sector.data_copy();
	if (data.size() > max_size)
		data.resize(max_size);

	bitbuf.addBlock(data);
	bitbuf.addGap(max_size - data.size());

	return TrackData(cylhead, std::move(bitbuf.buffer()));
}

////////////////////////////////////////////////////////////////////////////////
