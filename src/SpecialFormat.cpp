// Copy-protected formats that require special support
//
// Contains detection and generation for each track format.
// Output will be in bitstream or flux format (or both),
// depending on the format requirements.

#include "SAMdisk.h"
#include "IBMPC.h"
#include "BitstreamTrackBuffer.h"
#include "FluxTrackBuffer.h"

////////////////////////////////////////////////////////////////////////////////

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

TrackData GenerateKBI19Track (const CylHead &cylhead, const Track &track)
{
	assert(IsKBI19Track(track));
	(void)track;

	static const uint8_t ids[]{ 0,1,4,7,10,13,16,2,5,8,11,14,17,3,6,9,12,15,18 };
	static const Data gap2_sig{
		0x4E,0x4E,0x4E,0x4E,0x4E,0x4E,0x4E,0x4E,
		0x20,0x4B,0x42,0x49,0x20,	// " KBI "
		0x4E,0x4E,0x4E,0x4E,0x4E,0x4E,0x4E,0x4E,0x4E
	};
	static const Data data_sig{
		// "(c)1986 for KBI"
		0x28,0x63,0x29,0x20,0x31,0x39,0x38,0x36,0x20,0x66,0x6F,0x72,0x20,0x4B,0x42,0x49,
		// "by L. TOURNIER"
		0x20,0x62,0x79,0x20,0x4C,0x2E,0x20,0x54,0x4F,0x55,0x52,0x4E,0x49,0x45,0x52
	};
	static const Data end_sig{
		0x20,0x4D,0x41,0x53,0x54,0x45,0x52,0x20		// " MASTER "
	};

	BitstreamTrackBuffer bitbuf(DataRate::_250K, Encoding::MFM);

	// Track start with slightly shorter gap4a.
	bitbuf.addGap(64);
	bitbuf.addSync();
	bitbuf.addIAM();
	bitbuf.addGap(50);

	// Initial full-sized sector
	bitbuf.addSync();
	bitbuf.addSectorHeader(Header(cylhead, 0, 2));
	bitbuf.addBlock(gap2_sig);
	bitbuf.addSync();
	bitbuf.addAM(0xfb);
	bitbuf.addBlock(Data(512, 0xf6));
	bitbuf.addCrc(4 + 512);

	auto sector_index{0};
	for (int j = 0; j < 6; ++j)
	{
		// Two short headers that overlap the next sector.
		for (int k = 0; k < 2; ++k)
		{
			bitbuf.addSync();
			bitbuf.addSectorHeader(Header(cylhead, ids[++sector_index], 2));
			bitbuf.addBlock(gap2_sig);
			bitbuf.addSync();
			bitbuf.addAM(0xfb);
			bitbuf.addBlock(data_sig);
			bitbuf.addGap(30);
		}

		// Full-sized sector with data completing CRCs above.
		bitbuf.addSync();
		bitbuf.addSectorHeader(Header(cylhead, ids[++sector_index], 2));
		bitbuf.addBlock(gap2_sig);
		bitbuf.addSync();
		bitbuf.addAM(0xfb);
		bitbuf.addBlock(data_sig);
		bitbuf.addBlock(0xe5, 239);
		bitbuf.addCrc(4 + 512);		// CRC for 1st short sector
		bitbuf.addGap(50);
		bitbuf.addBlock(0xe5, 69);
		bitbuf.addCrc(4 + 512);		// CRC for 2nd short sector
		bitbuf.addGap(50);

		if (ids[sector_index] != 18)
			bitbuf.addBlock(0xe5, 69);
		else
		{
			// Final sector has MASTER signature.
			bitbuf.addBlock(0xe5, 69 - end_sig.size());
			bitbuf.addBlock(end_sig);
		}
		bitbuf.addCrc(4 + 512);		// CRC for full-sized sector
		bitbuf.addGap(80);
	}

	// Pad up to normal track size.
	bitbuf.addGap(170);

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

	BitstreamTrackBuffer bitbuf(DataRate::_500K, Encoding::MFM);

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

	FluxTrackBuffer fluxbuf(cylhead, DataRate::_250K, Encoding::MFM);
	fluxbuf.addTrackStart();

	BitstreamTrackBuffer bitbuf(DataRate::_250K, Encoding::MFM);
	bitbuf.addTrackStart();

	for (auto &sector : track)
	{
		auto &data_copy = sector.data_copy();
		auto is_weak{ &sector == &track[1] };

		if (!is_weak)
			fluxbuf.addSector(sector.header, data_copy, 0x54, sector.is_deleted());
		else
		{
			fluxbuf.addSectorUpToData(sector.header, sector.is_deleted());
			fluxbuf.addBlock(Data(data_copy.begin(), data_copy.begin() + weak_offset));
			fluxbuf.addWeakBlock(weak_size);
			fluxbuf.addBlock(Data(
				data_copy.begin() + weak_offset + weak_size,
				data_copy.begin() + sector.size()));
		}

		bitbuf.addSector(sector.header, data_copy, 0x2e, sector.is_deleted(), is_weak);

		// Add duplicate weak sector half way around track.
		if (&sector == &track[5])
		{
			auto &sector1{ track[1] };
			auto data1{ sector1.data_copy() };
			std::fill(data1.begin() + weak_offset, data1.begin() + weak_offset + weak_size, uint8_t(0xee));
			bitbuf.addSector(sector1.header, data1, 0x2e, sector1.is_deleted(), true);
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
		weak_offset = 5;	// -512
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

	FluxTrackBuffer fluxbuf(cylhead, DataRate::_250K, Encoding::MFM);
	fluxbuf.addTrackStart();

	BitstreamTrackBuffer bitbuf(DataRate::_250K, Encoding::MFM);
	bitbuf.addTrackStart();

	for (auto &sector : track)
	{
		auto &data_copy = sector.data_copy();
		auto is_weak{ &sector == &track[7] };

		if (!is_weak)
			fluxbuf.addSector(sector.header, data_copy, 0x54, sector.is_deleted());
		else
		{
			fluxbuf.addSectorUpToData(sector.header, sector.is_deleted());
			fluxbuf.addBlock(Data(data_copy.begin(), data_copy.begin() + weak_offset));
			fluxbuf.addWeakBlock(weak_size);
			fluxbuf.addBlock(Data(
				data_copy.begin() + weak_offset + weak_size,
				data_copy.begin() + sector.size()));
		}

		bitbuf.addSector(sector.header, data_copy, 0x2e, sector.is_deleted(), is_weak);

		// Add duplicate weak sector half way around track.
		if (&sector == &track[1])
		{
			auto &sector7{ track[7] };
			auto data7{ sector7.data_copy() };
			std::fill(data7.begin() + weak_offset, data7.begin() + weak_offset + weak_size, uint8_t(0xee));
			bitbuf.addSector(sector7.header, data7, 0x2e, sector7.is_deleted(), true);
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

	FluxTrackBuffer fluxbuf(cylhead, DataRate::_250K, Encoding::MFM);
	fluxbuf.addTrackStart();

	BitstreamTrackBuffer bitbuf(DataRate::_250K, Encoding::MFM);
	bitbuf.addTrackStart();

	for (auto &sector : track)
	{
		auto &data_copy = sector.data_copy();
		auto is_weak{ &sector == &track[1] };

		if (!is_weak)
			fluxbuf.addSector(sector.header, data_copy, 0x54, sector.is_deleted());
		else
		{
			fluxbuf.addSectorUpToData(sector.header, sector.is_deleted());
			fluxbuf.addBlock(Data(data_copy.begin(), data_copy.begin() + weak_offset));
			fluxbuf.addWeakBlock(weak_size);
			fluxbuf.addBlock(Data(
				data_copy.begin() + weak_offset + weak_size,
				data_copy.begin() + sector.size()));
		}

		bitbuf.addSector(sector.header, data_copy, 0x2e, sector.is_deleted(), is_weak);

		// Add duplicate weak sector half way around track.
		if (&sector == &track[5])
		{
			// Add a duplicate of the weak sector, with different data from the weak position
			auto &sector1{ track[1] };
			auto data1{ sector1.data_copy() };
			std::fill(data1.begin() + weak_offset, data1.begin() + weak_offset + weak_size, uint8_t(0xee));
			bitbuf.addSector(sector1.header, data1, 0x2e, sector1.is_deleted(), true);
		}
	}

	TrackData trackdata(cylhead);
	trackdata.add(std::move(bitbuf.buffer()));
	//trackdata.add(FluxData({ fluxbuf.buffer() }));
	return trackdata;
}

////////////////////////////////////////////////////////////////////////////////

// KBI-10 weak sector for CPC?
bool IsKBI10Track (const Track &track, int &weak_offset, int &weak_size)
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

	// =4 -4 =124 -4 =120
	weak_offset = 4;
	weak_size = 4;

	if (opt.debug) util::cout << "detected KBI-10 track\n";
	return true;
}

TrackData GenerateKBI10Track (const CylHead &cylhead, const Track &track, int weak_offset, int weak_size)
{
#ifdef _DEBUG
	int temp_offset, temp_size;
	assert(IsKBI10Track(track, temp_offset, temp_size));
	assert(weak_offset == temp_offset && weak_size == temp_size);
#endif

	FluxTrackBuffer fluxbuf(cylhead, DataRate::_250K, Encoding::MFM);
	fluxbuf.addTrackStart();

	BitstreamTrackBuffer bitbuf(DataRate::_250K, Encoding::MFM);
	bitbuf.addTrackStart();

	for (auto &sector : track)
	{
		auto &data_copy = sector.data_copy();
		auto is_weak{ &sector == &track[9] };

		if (!is_weak)
			fluxbuf.addSector(sector.header, data_copy, 0x54, sector.is_deleted());
		else
		{
			fluxbuf.addSectorUpToData(sector.header, sector.is_deleted());
			fluxbuf.addBlock(Data(data_copy.begin(), data_copy.begin() + weak_offset));
			fluxbuf.addWeakBlock(weak_size);
			fluxbuf.addBlock(Data(
				data_copy.begin() + weak_offset + weak_size,
				data_copy.begin() + sector.size()));
		}

		bitbuf.addSector(sector.header, data_copy, 1, sector.is_deleted(), is_weak);

		if (&sector == &track[3])
		{
			auto &sector9{ track[9] };
			auto data9{ sector9.data_copy() };
			std::fill(data9.begin() + weak_offset, data9.begin() + weak_offset + weak_size, uint8_t(0xee));
			bitbuf.addSector(sector9.header, data9, 1, sector9.is_deleted(), true);
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

	BitstreamTrackBuffer bitbuf(DataRate::_250K, Encoding::MFM);
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

	BitstreamTrackBuffer bitbuf(DataRate::_250K, Encoding::MFM);
	bitbuf.addTrackStart();
	bitbuf.addGap(600);

	for (auto &sector : track)
	{
		if (sector.header.sector != 8)
			bitbuf.addSector(sector, 0x100);
		else
		{
			auto &sector7 = track[7];
			auto &sector8 = track[8];

			bitbuf.addSectorUpToData(sector8.header, sector8.is_deleted());
			bitbuf.addBlock(Data(256, 0x55));
			bitbuf.addCrc(4 + 256);
			bitbuf.addBlock(Data(0x512 - 256 - 2, 0x4e));
			bitbuf.addBlock(sector7.data_copy());
		}
	}

	return TrackData(cylhead, std::move(bitbuf.buffer()));
}

////////////////////////////////////////////////////////////////////////////////
