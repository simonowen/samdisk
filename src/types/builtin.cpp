// Built-in disk images used for testing

#include "SAMdisk.h"
#include "IBMPC.h"

static Track &complete (Track &track)
{
	uint8_t fill = 0;

	for (auto &sector : track.sectors())
	{
		// Add test data to sectors that lack it
		if (!sector.copies())
			sector.add(Data(sector.size(), fill));

		// Remove data from sectors with 0 bytes of data (for no-data sectors)
		else if (!sector.data_size())
			sector.datas().clear();

		++fill;
	}

	return track;
}


bool ReadBuiltin (const std::string &path, std::shared_ptr<Disk> &disk)
{
	CylHead cylhead(0, 0);
	uint8_t i;

	auto type = std::strtol(path.c_str() + 1, NULL, 0);
	switch (type)
	{
		// 500Kbps
		case 0:
		{
#if 0
			// 21 sectors/track
		{
			Track track(21);

			for (i = 0; i < 21; ++i)
			{
				Sector sector(DataRate::_500K, Encoding::MFM, Header(cylhead, 1 + i, 2));
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Sega System 24
		{
			Track track(6);

			for (i = 0; i < 6; ++i)
			{
				static const uint8_t sizes[] = { 6,3,3,3,2,1 };
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, sizes[i]));
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}
#endif
			// Missing data fields
		{
			Track track(18);

			for (i = 0; i < 18; ++i)
			{
				Sector sector(DataRate::_500K, Encoding::MFM, Header(cylhead, 1 + i, 2));
				if (!(i % 10)) sector.add(Data());
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Data CRC errors
		{
			Track track(18);

			for (i = 0; i < 18; ++i)
			{
				Sector sector(DataRate::_500K, Encoding::MFM, Header(cylhead, 1 + i, 2));
				if (!((i + 5) % 10)) sector.add(Data(sector.size(), i), true);
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// 65 x 128-byte sectors
		{
			Track track(65);

			for (i = 0; i < 65; ++i)
			{
				Sector sector(DataRate::_500K, Encoding::MFM, Header(cylhead, 1 + i, 0));
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		break;
		}

		// 300Kbps
		case 1:
		{
			// BBC FM
			{
				Track track(10);

				for (i = 0; i < 10; ++i)
				{
					Sector sector(DataRate::_300K, Encoding::FM, Header(cylhead, 1 + i, 1));
					track.add(std::move(sector));
				}

				disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
			}

			break;
		}

		// 250Kbps
		case 2:
		{
#if 0
			// 11-sector Atari ST
		{
			Track track(11);

			for (i = 0; i < 11; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, i + 1, 2));
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}
#endif
			// +3 Speedlock sector (fully weak - Arctic Fox)
		{
			Track track(9);

			for (i = 0; i < 9; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 2));

				if (i >= 2 && i <= 7)
					sector.add(Data(sector.size(), i), false, 0xf8);

				track.add(std::move(sector));
			}

			// Add Speedlock signature to first sector
			Data data0(512, 0);
			std::string sig = "SPEEDLOCK";
			std::copy(sig.begin(), sig.end(), data0.begin() + 304);
			track[0].add(Data(data0));

			// Create 3 different copies of the weak sector
			std::iota(data0.begin(), data0.end(), 0);
			track[1].add(Data(data0), true);
			std::iota(data0.begin(), data0.end(), 1);
			track[1].add(Data(data0), true);
			std::iota(data0.begin(), data0.end(), 2);
			track[1].add(Data(data0), true);

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}


		// +3 Speedlock sector (partially weak - Robocop)
		{
			Track track(9);

			for (i = 0; i < 9; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 2));

				if (i >= 2 && i <= 7)
					sector.add(Data(sector.size(), i), false, 0xf8);

				track.add(std::move(sector));
			}

			// Add Speedlock signature to first sector
			Data data0(512, 0);
			const std::string sig = "SPEEDLOCK";
			std::copy(sig.begin(), sig.end(), data0.begin() + 304);
			track[0].add(std::move(data0));

			// Add 3 copies with differences matching the Robocop weakness (and SAMdisk v3)
			Data data1(512, 0);
			std::fill(data1.begin(), data1.begin() + 256, 0xe5);
			track[1].add(Data(data1), true);
			std::iota(data1.begin() + 256, data1.end(), 1);
			std::fill(data1.begin() + 256 + 32, data1.begin() + 256 + 32 + 48, 2);
			track[1].add(Data(data1), true);
			std::iota(data1.begin() + 256, data1.end(), 0);
			std::fill(data1.begin() + 256 + 32, data1.begin() + 256 + 32 + 48, 1);
			track[1].add(Data(data1), true);

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// CPC Speedlock sector (partially weak)
		{
			Track track(9);

			for (i = 0; i < 9; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 64 + i, 2));
				track.add(std::move(sector));
			}

			// Add Speedlock signature to first sector
			Data data0(512, 0);
			const std::string sig = "SPEEDLOCK";
			std::copy(sig.begin(), sig.end(), data0.begin() + 9);
			track[0].add(std::move(data0));

			// Add 3 copies with differences matching the typical weak sector
			Data data7(512, 0);
			std::fill(data7.begin(), data7.begin() + 256, 0xe5);
			track[7].add(Data(data7), true);
			std::iota(data7.begin() + 256, data7.end(), 1);
			std::fill(data7.begin() + 256 + 40, data7.begin() + 256 + 40 + 40, 2);
			track[7].add(Data(data7), true);
			std::iota(data7.begin() + 256, data7.end(), 0);
			std::fill(data7.begin() + 256 + 40, data7.begin() + 256 + 40 + 40, 1);
			track[7].add(Data(data7), true);

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Electronic Arts partially weak sector
		{
			Track track(9);

			for (i = 0; i < 9; ++i)
			{
				static const uint8_t ids[] = { 193,198,194,109,195,200,196,201,197 };
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, ids[i], 2));
				track.add(std::move(sector));
			}

			// Add Speedlock signature to first sector
			Data data0(512, 0);
			const std::string sig = "SPEEDLOCK";
			std::copy(sig.begin(), sig.end(), data0.begin() + 9);
			track[0].add(std::move(data0));

			// Add 3 copies with differences matching the typical weak sector
			Data data1(512, 0);
			std::fill(data1.begin(), data1.begin() + 256, 0xe5);
			track[1].add(Data(data1), true);
			std::iota(data1.begin() + 100, data1.begin() + 100 + 207, 101);
			std::fill(data1.begin() + 100 + 207, data1.end(), 2);
			track[1].add(Data(data1), true);
			std::iota(data1.begin() + 100, data1.begin() + 100 + 207, 101);
			std::fill(data1.begin() + 100 + 207, data1.end(), 1);
			track[1].add(Data(data1), true);

			Data data3(512, 0);
			const std::string codesig = "\x2a\x6d\xa7\x01\x30\x01\xaf\xed\x42\x4d\x44\x21\x70\x01";
//				static const uint8_t codesig[] = { 0x2a, 0x6d, 0xa7, 0x01, 0x30, 0x01, 0xaf, 0xed, 0x42, 0x4d, 0x44, 0x21, 0x70, 0x01 };
			std::copy(codesig.begin(), codesig.end(), data3.begin());

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// KBI-10 weak sector
		{
			Track track(10);

			for (i = 0; i < 10; ++i)
			{
				static const uint8_t ids[] = { 193,198,194,109,195,200,196,201,197,202 };
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, ids[i], (i == 9) ? 1 : 2));
				track.add(std::move(sector));
			}

			Data data9(256, 0xe5);
			const std::string sig = "KBI.";
			std::copy(sig.begin(), sig.end(), data9.begin());
			track[9].add(Data(data9), true);

			std::iota(data9.begin() + 4, data9.begin() + 4 + 4, 5);
			std::iota(data9.begin() + 4 + 4 + 124, data9.begin() + 4 + 4 + 124 + 4, 125);
			track[9].add(Data(data9), true);
			std::iota(data9.begin() + 4, data9.begin() + 4 + 4, 4);
			std::iota(data9.begin() + 4 + 4 + 124, data9.begin() + 4 + 4 + 124 + 4, 124);
			track[9].add(Data(data9), true);

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Missing data fields
		{
			Track track(10);

			for (i = 0; i < 10; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 2));
				if (!(i % 5)) sector.add(Data());
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// ID field CRC error
		{
			Track track(10);

			for (i = 0; i < 10; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 2));
				if (!(i % 5)) sector.set_badidcrc();
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Data CRC error
		{
			Track track(10);

			for (i = 0; i < 10; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 2));
				if (!(i % 5)) sector.add(Data(sector.size(), i), true);
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}


		// Jim Power with data hidden in gap after first sector (broken in SAMdisk v3.7+ 23???)
		{
			Track track(10);

			for (i = 0; i < 10; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 2));
				track.add(std::move(sector));
			}

			const std::string sig = "HELLO!";
			Data data0(512 + 2 + sig.size(), 0x01);
			std::copy(sig.begin(), sig.end(), data0.begin() + 512 + 2);

			CRC16 crc("\xa1\xa1\xa1", 3);
			crc.add(0xfb);
			crc.add(data0.data(), 512);
			data0[512] = crc >> 8;
			data0[513] = crc & 0xff;
			track[0].add(std::move(data0));

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Single sector with gap data
		{
			Track track(1);
			Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1, 0));
			track.add(std::move(sector));

			const std::string sig = "HELLO!";
			Data data0(6144, 0x01);
			std::fill(data0.begin(), data0.begin() + sector.size(), 0x00);
			std::copy(sig.begin(), sig.end(), std::prev(data0.end(), sig.size()));

			CRC16 crc("\xa1\xa1\xa1", 3);
			crc.add(0xfb);
			crc.add(data0.data(), sector.size());
			data0[sector.size() + 0] = crc >> 8;
			data0[sector.size() + 1] = crc & 0xff;
			track[0].add(std::move(data0));

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Le Necromancien typical track, with gap data conflicting with EDSK multiple copies extension
		{
			Track track(10);

			for (i = 0; i < 10; ++i)
			{
				static const uint8_t ids[] = { 1,1,6,2,7,3,8,4,9,5 };
				Header header((i == 1) ? 1 : cylhead.cyl, (i == 1) ? 4 : cylhead.head, ids[i], i ? 2 : 1);
				Sector sector(DataRate::_250K, Encoding::MFM, header);
				if (i == 0) sector.add(Data(512, 0xf7), true);
				if (i == 1) sector.add(Data(sector.size(), i), false, 0xf8);
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Defenders of the Earth (+3)
		{
			Track track(10);

			for (i = 0; i < 10; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(206, 201, 196, 191));
				sector.add(Data(sector.size(), i), true);
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// World Games (CPC)
		{
			Track track(9);

			for (i = 0; i < 9; ++i)
			{
				Header header(cylhead, (i == 8) ? 121 : 129 + i, (i == 8) ? 0 : 2);
				Sector sector(DataRate::_250K, Encoding::MFM, header);
				if (i == 8) sector.add(Data());
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// SP6
		{
			Track track(9);

			for (i = 0; i < 9; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, (i == 8) ? 0 : 2));
				if (i == 7) sector.header = Header(203, 253, 188, 221);
				if (i == 7 || i == 8) sector.add(Data(sector.size(), i), true);
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Tomahawk (track 3)
		{
			Track track(9);

			for (i = 0; i < 9; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(0, 0, 0, 2));
				sector.add(Data(sector.size(), i), false, 0xf8);
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// 8K sector
		{
			Track track(1);

			Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 193, 6));
			sector.add(Data(sector.size(), i), true, 0xf8);
			track.add(std::move(sector));

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

#if 0
			// Logo Professor (overformatted track, old style)
		{
			Track track(11);

			for (i = 0; i < 11; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 2));
				if (i == 0) sector.add(Sector::Flag::NoHeader);
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}
#endif
			// Logo Professor (overformatted track, with offsets)
		{
			Track track(10);

			for (i = 0; i < 10; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 2 + i, 2));
				sector.offset = TRACK_OVERHEAD_MFM + (SECTOR_OVERHEAD_MFM + 512 + 28) * (i + 1);
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Edd the Duck (track 7)
		{
			Track track(10);
			static const uint8_t ids[] { 193, 65, 70, 66, 71, 67, 72, 68, 73, 69, 74 };

			for (auto id : ids)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, id, (id == 193) ? 6 : 2));
				if (id == 193) sector.add(Data(sector.size(), i), true);
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Alternative for KBI-10 weak sector
		{
			Track track(11);
			static const uint8_t ids[] { 193,198,194,199,202,195,200,196,201,197,202 };

			for (auto id : ids)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, id, (id == 202) ? 1 : 2));
				if (id == 202) sector.add(Data(sector.size(), i), true);
				track.add(std::move(sector));
			}

			std::string sig = "KBI";
			Data data3(512, 0);
			std::copy(sig.begin(), sig.end(), data3.begin());
			track[3].add(std::move(data3));

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// KBI-19: CPC Titan (cyl 14) and Mach 3 (cyl 40)
		{
			Track track(19);
			static const uint8_t ids[] { 0,1,4,7,10,13,16,2,5,8,11,14,17,3,6,9,12,15,18 };

			for (auto id : ids)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, id, 2));
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// CAL2BOOT.DMK (track 9)
		{
			Track track(5);
			static const uint8_t ids[] { 1,8,17,9,18 };

			for (auto id : ids)
			{
				Header header(39, cylhead.head, id, 2);
				if (id == 1) header = Header(20, 0, id, 3);
				Sector sector(DataRate::_250K, Encoding::MFM, header);
				if (id == 1 || id == 18) sector.add(Data(sector.size(), i), true);
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Prehistoric 2 (track 30)
		{
			Track track(14);
			static const uint8_t ids[] { 1,193,2,194,3,195,4,196,5,197,6,198,7,199 };

			for (auto id : ids)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, id, 2));
				if (id <= 7) sector.add(Data(sector.size(), i), true);
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Tetris
		{
			Track track(16);

			for (i = 0; i < 16; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(i, i, i, i));
				if (i > 0) sector.add(Data(sector.size(), i), true);
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Terre et Conquerants
		{
			Track track(30);

			for (i = 0; i < 30; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, i, (i == 29) ? 2 : 5));
				if (i != 29) sector.add(Data(sector.size(), i), true);
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// 19 x 256-byte sectors
		{
			Track track(19);

			for (i = 0; i < 19; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 1));
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Opera Soft 32K sector
		{
			Track track(9);

			for (i = 0; i < 9; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, i, (i == 8) ? 8 : 1));
				if (i == 8) sector.add(Data(sector.size(), i), true);
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Sports Hero + Mugsy (+3)
		{
			Track track(18);
			static const uint8_t ids[] { 7,14,3,10,17,6,13,2,9,16,5,12,1,8,15,4,11,0 };

			for (auto id : ids)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, id, (id == 7) ? 0 : 1));
				if (id == 7 || id == 9) sector.add(Data(sector.size(), i), true);
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Mirage (5*1024 + 1*512)
		{
			Track track(6);

			for (i = 0; i < 6; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, (i < 5) ? 3 : 2));
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Prophet 2000 (5*1024 + 1*256)
		{
			Track track(6);

			for (i = 0; i < 6; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, i, (i < 5) ? 3 : 1));
				if (i == 3 || i == 4) sector.add(Data(sector.size(), i), false, 0xf8);
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Lemmings (SAM)
		{
			Track track(6);

			for (i = 0; i < 6; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, (i == 0) ? 2 : 3));
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// Puffy's Saga (CPC)
		{
			Track track(4);

			for (i = 0; i < 4; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 65 + i, (i == 0) ? 2 : (i == 1) ? 3 : 4));
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

		// 32 x 128-byte sectors
		{
			Track track(32);

			for (i = 0; i < 32; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, 0));
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}

#if 1
			// Some HP disks described on cctalk
		{
			Track track(18);

			for (i = 0; i < 18; ++i)
			{
				Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, i, (i == 17) ? 0 : 1));
				track.add(std::move(sector));
			}

			disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
		}
#endif
		break;
		}

		// 1Mbps
		case 3:
		{
			// Normal ED format
			{
				Track track(36);

				for (i = 0; i < 36; ++i)
				{
					Sector sector(DataRate::_1M, Encoding::MFM, Header(cylhead, 1 + i, 2));
					track.add(std::move(sector));
				}

				disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
			}

			// 130 x 128-byte sectors
			{
				Track track(130);

				for (i = 0; i < 130; ++i)
				{
					Sector sector(DataRate::_1M, Encoding::MFM, Header(cylhead, 1 + i, 0));
					track.add(std::move(sector));
				}

				disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
			}

			// GPT (Siemens) exchange format
			{
				Track track(20);

				for (i = 0; i < 20; ++i)
				{
					Sector sector(DataRate::_1M, Encoding::MFM, Header(cylhead, i, 3));
					track.add(std::move(sector));
				}

				disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
			}

			break;
		}

		// 500Kbps MFM simple sector size test
		case 4 + 0:
		{
			// MFM sizes 0 to 6
			for (uint8_t size = 0; size < 7; ++size)
			{
				Track track(7 - size);

				for (i = 0; i < 7 - size; ++i)
				{
					Sector sector(DataRate::_500K, Encoding::MFM, Header(cylhead, 1 + i, size));
					track.add(std::move(sector));
				}

				disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
			}

			break;
		}

		// 500Kbps FM simple sector size test
		case 8 + 0:
		{
			// FM sizes 0 to 5
			for (uint8_t size = 0; size < 6; ++size)
			{
				Track track(6 - size);

				for (i = 0; i < 6 - size; ++i)
				{
					Sector sector(DataRate::_500K, Encoding::FM, Header(cylhead, 1 + i, size));
					track.add(std::move(sector));
				}

				disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
			}

			break;
		}


		// 250Kbps MFM simple sector size test
		case 4 + 2:
		{
			// MFM sizes 0 to 5
			for (uint8_t size = 0; size < 6; ++size)
			{
				Track track(6 - size);

				for (i = 0; i < 6 - size; ++i)
				{
					Sector sector(DataRate::_250K, Encoding::MFM, Header(cylhead, 1 + i, size));
					track.add(std::move(sector));
				}

				disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
			}

			break;
		}


		// 250Kbps FM simple sector size test
		case 8 + 2:
		{
			// FM sizes 0 to 4
			for (uint8_t size = 0; size < 5; ++size)
			{
				Track track(5 - size);

				for (i = 0; i < 5 - size; ++i)
				{
					Sector sector(DataRate::_250K, Encoding::FM, Header(cylhead, 1 + i, size));
					track.add(std::move(sector));
				}

				disk->write_track(cylhead.next_cyl(), std::move(complete(track)));
			}

			break;
		}

		default:
			throw util::exception("unknown built-in type");
	}

	// Append a blank track
	disk->write_track(cylhead, std::move(Track()));

	disk->strType = "<builtin>";
	return true;
}
