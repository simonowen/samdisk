// UDI = Ultra Disk Image for Spectrum, by Alex Makeev:
//  http://scratchpad.wikia.com/wiki/Spectrum_emulator_file_format:_udi

#include "SAMdisk.h"
#include "BitstreamTrackBuilder.h"

#define UDI_SIGNATURE				"UDI!"
#define UDI_SIGNATURE_COMPRESSED	"udi!"
const int MAX_UDI_TRACK_SIZE = 8192;

typedef struct
{
	uint8_t signature[4];	// signature
	uint8_t filesize[4];	// fileSize-4 (last 4 bytes is checksum)
	uint8_t version;		// version (current version is 00)
	uint8_t max_cyl;		// max cylinder, 0x4F for 80 cylinders on disk
	uint8_t max_head;		// max head, 0x00 for single-sided, 0x01 for double-sided, 0x02..0xFF reserved
	uint8_t unused;			// unused (should be 00)
	uint8_t ext_hdr_len[4];	// EXTHDL - extended header size (should be 00)
} UDI_HEADER;

typedef struct
{
	uint8_t format;			// 0=MFM, 1=FM, 2=mixed MFM/FM
	uint8_t length[2];		// raw track size in bytes (usually 6250 bytes)
} UDI_TRACK;

static uint32_t crc32 (const uint8_t *buf, int len)
{
	int32_t crc = ~0;
	for (int i = 0; i < len; i++)
	{
		crc ^= ~buf[i];
		for (int j = 0; j < 8; ++j)
		{
			if (crc & 1)
				crc = (crc >> 1) ^ 0xedb88320;
			else
				crc = (crc >> 1);
		}
		crc = ~crc;
	}

	return static_cast<uint32_t>(crc);
}

bool ReadUDI(MemFile &file, std::shared_ptr<Disk> &disk)
{
	UDI_HEADER uh{};
	if (!file.rewind() || !file.read(&uh, sizeof(uh)))
		return false;

	if (!memcmp(&uh.signature, UDI_SIGNATURE_COMPRESSED, sizeof(uh.signature)))	// "udi!")
		throw util::exception("compressed UDI images are not currently supported");
	else if (!memcmp(&uh.signature, UDI_SIGNATURE, 3) && uh.signature[3] != '!')
		throw util::exception("old format UDI images are not currently supported");
	else if (memcmp(&uh.signature, UDI_SIGNATURE, sizeof(uh.signature)))
		return false;

	if (uh.unused)
		Message(msgWarning, "unused header field isn't zero");

	uint8_t crc_buf[4];
	auto file_size = static_cast<int>(util::le_value(uh.filesize));

	if (file.size() != file_size + 4)
		Message(msgWarning, "file size (%u) doesn't match header size field (%u)", file.size(), file_size + 4);
	else if (file.seek(file_size) && file.read(&crc_buf, sizeof(crc_buf)))
	{
		auto crc_file = util::le_value(crc_buf);
		auto crc = crc32(file.data().data(), file.size() - 4);
		if (crc != crc_file)
			Message(msgWarning, "invalid file CRC");
		file.seek(sizeof(uh));
	}

	int cyls = uh.max_cyl + 1;
	int heads = (uh.max_head & 1) + 1;
	Format::Validate(cyls, heads);

	for (uint8_t cyl = 0; cyl < cyls; ++cyl)
	{
		for (uint8_t head = 0; head < heads; ++head)
		{
			CylHead cylhead(cyl, head);

			UDI_TRACK ut;
			if (!file.read(&ut, sizeof(ut)))
				throw util::exception("short file reading header on ", cylhead);

			unsigned  tlen = util::le_value(ut.length);
			if (!tlen)
				continue;
			else if (tlen > MAX_UDI_TRACK_SIZE)
				throw util::exception("track size (", tlen, ") too big on ", cylhead);

			Data data(tlen);
			if (!file.read(data))
				throw util::exception("short file reading data on ", cylhead);

			Data clock((tlen + 7) / 8);
			if (!file.read(clock))
				throw util::exception("short file reading clock bits on ", cylhead);

			// Accept MFM or FM only.
			if (ut.format >= 2)
				throw util::exception("unsupported track type (", ut.format, ") on ", cylhead);

			auto encoding = (ut.format == 0x01) ? Encoding::FM : Encoding::MFM;
			auto datarate = (tlen > 6400) ? DataRate::_500K : DataRate::_250K;
			BitstreamTrackBuilder bitbuf(datarate, encoding);

			for (unsigned u = 0; u < tlen; u++)
			{
				if (!(clock[u >> 3] & (1 << (u & 7))))
					bitbuf.addByte(data[u]);
				else if (encoding == Encoding::FM)
				{
					if (data[u] == 0xfc)
						bitbuf.addByteWithClock(0xfc, 0xd7);
					else
						bitbuf.addByteWithClock(data[u], 0xc7);
				}
				else
				{
					if (data[u] == 0xa1)
						bitbuf.addByteWithClock(0xa1, 0x0a);
					else if (data[u] == 0xc2)
						bitbuf.addByteWithClock(0xc2, 0x14);
					else
						bitbuf.addByte(data[u]);
				}
			}

			disk->write(cylhead, std::move(bitbuf.buffer()));
		}
	}

	disk->strType = "UDI";
	return true;
}
