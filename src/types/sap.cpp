// SAP - Systeme D'archivage Pukall (Thomson TO8/TO8D/TO9/TO9+)
//

#include "SAMdisk.h"
#include "BitstreamTrackBuilder.h"

#define SAP_SIGNATURE	"SYSTEME D'ARCHIVAGE PUKALL S.A.P. (c) Alexandre PUKALL Avril 1998"
#define SAP_SECTORS_PER_TRACK	16
#define SAP_CRYPT_BYTE			0xb3	// data fields are XORed with this

typedef struct
{
	uint8_t version;
	char signature[sizeof(SAP_SIGNATURE) - 1];
} SAP_HEADER;

typedef struct
{
	uint8_t format;
	uint8_t protection;
	uint8_t track;
	uint8_t sector;
} SAP_SECTOR;


static uint16_t crc16(uint8_t b, uint16_t crc)
{
	for (int i = 0; i < 2; ++i)
	{
		crc = ((crc >> 4) & 0xfff) ^ (0x1081 * ((crc ^ b) & 0xf));
		b >>= 4;
	}
	return crc;
}

static uint16_t crc_decrypt_sector (const SAP_SECTOR &ss, Data &data)
{
	uint16_t crc = 0xffff;

	for (size_t i = 0; i < sizeof(ss); ++i)
		crc = crc16(reinterpret_cast<const uint8_t*>(&ss)[i], crc);

	for (auto &x : data)
	{
		x ^= SAP_CRYPT_BYTE;
		crc = crc16(x, crc);
	}

	return crc;
}

static uint16_t crc_encrypt_sector (const SAP_SECTOR &ss, Data &data)
{
	uint16_t crc = 0xffff;

	for (size_t i = 0; i < sizeof(ss); ++i)
		crc = crc16(reinterpret_cast<const uint8_t*>(&ss)[i], crc);

	for (auto &x : data)
	{
		crc = crc16(x, crc);
		x ^= SAP_CRYPT_BYTE;
	}

	return crc;
}

bool ReadSAP (MemFile &file, std::shared_ptr<Disk> &disk)
{
	SAP_HEADER sh{};
	if (!file.rewind() || !file.read(&sh, sizeof(sh)))
		return false;

	if (memcmp(sh.signature, SAP_SIGNATURE, sizeof(sh.signature) - 1))
		return false;

	// 00-03 and 80-83 are only supported versions.
	if (sh.version & 0x7c)
		throw util::exception("unsupported SAP version (", std::hex, sh.version, std::dec, ")");

	bool is_regular = !opt.verbose;
	Format fmt = RegularFormat::TO_640K_MFM;
	fmt.cyls = (sh.version & 0x02) ? 40 : 80;
	fmt.heads = (sh.version & 0x80) ? 2 : 1;
	fmt.size = (sh.version & 0x01) ? 1 : 0;
	fmt.encoding = (sh.version & 0x01) ? Encoding::MFM : Encoding::FM;
	fmt.Override();

	Data disk_data;
	disk_data.reserve(fmt.disk_size());

	for (int head = 0; head < fmt.heads; ++head)
	{
		for (int cyl = 0; cyl < fmt.cyls; ++cyl)
		{
			CylHead cylhead(cyl, head);
			BitstreamTrackBuilder bitbuf(fmt.datarate, fmt.encoding);
			uint8_t track_fill{};

			for (int sec = 0; sec < fmt.sectors; ++sec)
			{
				SAP_SECTOR ss{};
				if (!file.read(&ss, sizeof(ss)))
					throw util::exception("short file reading header for ", cylhead);

				Encoding encoding{};
				int size_code{};
				track_fill = 0x4e;

				switch (ss.format)
				{
				case 0: encoding = Encoding::MFM; size_code = 1; break;
				case 1: encoding = Encoding::FM; size_code = 0; break;
				case 4:
					encoding = Encoding::MFM;
					size_code = 1;
					track_fill = 0xf7;
					break;
				default:
					throw util::exception("unsupported sector format (", ss.format, ") on ", cylhead);
				}

				switch (ss.protection)
				{
				case 0: break;
				case 7: track_fill = 0xf7; break;
				default:
					throw util::exception("unsupported protection (", ss.protection, ") on ", cylhead);
				}

				Data data(Sector::SizeCodeToLength(size_code));
				uint8_t data_crc[2];
				if (!file.read(data) || !file.read(&data_crc, sizeof(data_crc)))
					throw util::exception("short file reading data for ", cylhead);

				auto crc = crc_decrypt_sector(ss, data);
				bool bad_crc = (crc != util::be_value(data_crc));
#ifdef _DEBUG
				if (bad_crc)
				{
					Message(msgWarning, "bad CRC on %s, found %02X %02X expected %02X %02X",
						CHS(cyl, head, ss.sector), data_crc[0], data_crc[1], crc >> 8, crc & 0xff);
				}
#endif

				// Detect if anything deviated from the image regular format.
				is_regular &= !bad_crc && ss.protection == 0 && ss.format < 2 &&
					encoding == fmt.encoding && size_code == fmt.size &&
					ss.track == cyl && ss.sector == (sec + 1);

				if (is_regular)
					disk_data.insert(disk_data.end(), data.cbegin(), data.cend());

				bitbuf.setEncoding(encoding);

				if (sec == 0)
					bitbuf.addGap(32, track_fill);

				uint8_t gap_fill = (ss.format == 4) ? data[0] :
					(ss.protection == 7) ? 0xf7 : 0x4e;

				bitbuf.addSectorHeader(Header(ss.track, head, ss.sector, size_code));
				bitbuf.addGap((encoding == Encoding::MFM ? 22 : 11), gap_fill);
				bitbuf.addAM(0xfb);
				bitbuf.addBlock(data);

				if (bad_crc)
				{
					bitbuf.addByte(data_crc[0]);
					bitbuf.addByte(data_crc[1]);
				}
				else
				{
					auto sync_am = (encoding == Encoding::MFM) ? 4 : 1;
					bitbuf.addCrc(sync_am + data.size());
				}

				bitbuf.addGap(40, gap_fill);
			}

			bitbuf.addBlock(track_fill, 492);
			disk->write(cylhead, std::move(bitbuf.buffer()));
		}
	}

	if (!file.eof())
		Message(msgWarning, "unexpected data at end of SAP image");

	if (is_regular)
		disk->format(fmt, disk_data, fmt.cyls_first);

	disk->strType = "SAP";
	return true;
}

bool WriteSAP (FILE* f_, std::shared_ptr<Disk> &disk)
{
	auto track0 = disk->read_track(CylHead(0, 0));
	if (track0.size() != SAP_SECTORS_PER_TRACK)
		return false;

	auto track0_1 = disk->read_track(CylHead(0, 1));
	auto track40 = disk->read_track(CylHead(40, 0));
	auto cyls = track40.empty() ? 40 : 80;
	auto heads = track0_1.empty() ? 1 : 2;

	SAP_HEADER sh{};
	if (track0[0].header.size == 1) sh.version |= 0x01;
	if (cyls == 40) sh.version |= 0x02;
	if (heads == 2) sh.version |= 0x80;
	memcpy(sh.signature, SAP_SIGNATURE, sizeof(sh.signature));

	if (!fwrite(&sh, sizeof(sh), 1, f_))
		throw util::exception("write error");

	Range(cyls, heads).each([&] (const CylHead &cylhead)
	{
		auto &track = disk->read_track(cylhead);
		if (track.size() != 16)
			throw util::exception("SAP requires ", SAP_SECTORS_PER_TRACK, " sectors on ", cylhead);

		SAP_SECTOR ssTrack{};

		// Crude detection of format=4 and protection=7.
		for (auto &sector : track)
		{
			// At least 4 gap bytes present?
			if (sector.data_size() >= sector.size() + 4)
			{
				auto &data = sector.data_copy();

				// Sniff 2 bytes after the CRC to detect the likely gap3 filler.
				switch (data[sector.size() + 3])
				{
				case 0xf7:
					// Set protection 7 if we've not already set a format.
					if (!ssTrack.format)
						ssTrack.protection = 7;
					break;
				case 0x4e:
					// Ignore standard gap filler as it could be bogus.
					break;
				default:
					// Custom gap means format 4, which overrides format 7.
					ssTrack.format = 4;
					ssTrack.protection = 0;
					break;
				}
			}
		}

		for (auto &sector : track)
		{
			if (sector.header.head != cylhead.head)
				throw util::exception("head value mismatch on ", cylhead);
			else if (sector.data_size() < sector.size())
				throw util::exception("missing data on ", cylhead);

			SAP_SECTOR ss{ssTrack};
			ss.track = static_cast<uint8_t>(sector.header.cyl);
			ss.sector = static_cast<uint8_t>(sector.header.sector);

			if (sector.encoding == Encoding::MFM && sector.header.size == 1)
				ss.format = 0;
			else if (sector.encoding == Encoding::FM && sector.header.size == 0)
				ss.format = 1;
			else
				throw util::exception("unsupported encoding or sector size on ", cylhead);

			auto data = sector.data_copy();
			if (ss.format == 4 && data.size() > sector.size() + 4)
			{
				// Format 4 stores the gap filler in the sector's first byte.
				data[0] = data[sector.size() + 3];
			}
			data.resize(sector.size());

			auto crc_be = util::htobe(crc_encrypt_sector(ss, data));
			if (sector.has_baddatacrc())
				crc_be = ~crc_be;

			if (!fwrite(&ss, sizeof(ss), 1, f_) ||
				!fwrite(data.data(), data.size(), 1, f_) ||
				!fwrite(&crc_be, sizeof(crc_be), 1, f_))
				throw util::exception("write error");
		}
	}, true);

	return true;
}
