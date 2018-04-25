// Bit buffer used for assembling tracks

#include "SAMdisk.h"
#include "TrackBuffer.h"
#include "IBMPC.h"

TrackBuffer::TrackBuffer (DataRate datarate, Encoding encoding)
: m_datarate(datarate)
{
	setEncoding(encoding);
}

void TrackBuffer::setEncoding (Encoding encoding)
{
	switch (encoding)
	{
	case Encoding::MFM:
	case Encoding::FM:
	case Encoding::RX02:
	case Encoding::Amiga:
		m_encoding = encoding;
		break;
	default:
		throw util::exception("unsupported track encoding (", encoding, ")");
	}
}

void TrackBuffer::addBit (bool bit)
{
	addRawBit(bit);

	if (m_encoding == Encoding::FM)
		addRawBit(false);
}

void TrackBuffer::addDataBit (bool bit)
{
	if (m_encoding == Encoding::FM)
	{
		// FM has a reversal before every data bit
		addBit(true);
		addBit(bit);
	}
	else
	{
		// MFM has a reversal between consecutive zeros (clock or data)
		addBit(!m_lastbit && !bit);
		addBit(bit);
	}

	m_lastbit = bit;
}

void TrackBuffer::addByte (int byte)
{
	for (auto i = 0; i < 8; ++i)
	{
		addDataBit((byte & 0x80) != 0);
		byte <<= 1;
	}
}

void TrackBuffer::addByteUpdateCrc (int byte)
{
	addByte(byte);
	m_crc.add(byte);
}

void TrackBuffer::addByteWithClock (int data, int clock)
{
	for (auto i = 0; i < 8; ++i)
	{
		addBit((clock & 0x80) != 0);
		addBit((data & 0x80) != 0);
		clock <<= 1;
		data <<= 1;
	}

	m_lastbit = (data & 0x100) != 0;
}

void TrackBuffer::addBlock (int byte, int count)
{
	for (int i = 0; i < count; ++i)
		addByte(byte);
}

void TrackBuffer::addBlock (const Data &data)
{
	for (auto & byte : data)
		addByte(byte);
}

void TrackBuffer::addBlockUpdateCrc (const Data &data)
{
	for (auto & byte : data)
	{
		addByte(byte);
		m_crc.add(byte);
	}
}

void TrackBuffer::addGap (int count, int fill)
{
	if (fill < 0)
		fill = (m_encoding == Encoding::FM) ? 0xff : 0x4e;

	addBlock(fill, count);
}

void TrackBuffer::addGap2 (int fill)
{
	int gap2_bytes = (m_encoding == Encoding::FM) ? 11 :
		(m_datarate == DataRate::_1M) ? 41 : 22;
	addGap(gap2_bytes, fill);
}

void TrackBuffer::addSync ()
{
	auto sync{0x00};
	addBlock(sync, (m_encoding == Encoding::FM) ? 6 : 12);
}

void TrackBuffer::addAM (int type, bool omit_sync)
{
	if (!omit_sync)
		addSync();

	if (m_encoding == Encoding::FM)
	{
		addByteWithClock(type, 0xc7);	// FM AM uses C7 clock pattern

		m_crc.init();
		m_crc.add(type);
	}
	else
	{
		addByteWithClock(0xa1, 0x0a);	// A1 with missing clock bit
		addByteWithClock(0xa1, 0x0a);	// clock: 0 0 0 0 1 X 1 0
		addByteWithClock(0xa1, 0x0a);	// data:   1 0 1 0 0 0 0 1

		m_crc.init(0xcdb4);				// A1A1A1
		addByteUpdateCrc(type);
	}
}

void TrackBuffer::addIAM ()
{
	addSync();

	if (m_encoding == Encoding::FM)
	{
		addByteWithClock(0xfc, 0xd7);	// FM IAM uses D7 clock pattern
	}
	else
	{
		addByteWithClock(0xc2, 0x14);	// C2 with missing clock bit
		addByteWithClock(0xc2, 0x14);	// clock: 0 0 0 1 X 1 0 0
		addByteWithClock(0xc2, 0x14);	// data:   1 1 0 0 0 0 1 0
		addByte(0xfc);
	}
}

void TrackBuffer::addCrcBytes (bool bad_crc)
{
	uint16_t adjust = bad_crc ? 0x5555 : 0;
	addByte((m_crc ^ adjust) >> 8);
	addByte((m_crc ^ adjust) & 0xff);
}

void TrackBuffer::addTrackStart ()
{
	switch (m_encoding)
	{
	case Encoding::MFM:
	case Encoding::FM:
		addGap((m_encoding == Encoding::FM) ? 40 : 80);	// gap 4a
		addIAM();
		addGap((m_encoding == Encoding::FM) ? 26 : 50);	// gap 1
		break;
	case Encoding::Amiga:
	{
		auto fill{ 0x00 };
		addBlock(fill, 60);
		break;
	}
	case Encoding::RX02:
		setEncoding(Encoding::FM);
		addGap(32);	// gap 4a
		addIAM();
		addGap(27);	// gap 1
		setEncoding(Encoding::RX02);
		break;
	default:
		throw util::exception("unsupported track start (", m_encoding, ")");
	}
}

void TrackBuffer::addSectorHeader(const Header &header, bool crc_error)
{
	addAM(0xfe);
	addByteUpdateCrc(header.cyl);
	addByteUpdateCrc(header.head);
	addByteUpdateCrc(header.sector);
	addByteUpdateCrc(header.size);
	addCrcBytes(crc_error);
}

void TrackBuffer::addSectorData(const Data &data, int size, uint8_t dam, bool crc_error)
{
	// Ensure this isn't used for over-sized protected sectors.
	assert(Sector::SizeCodeToLength(size) == Sector::SizeCodeToLength(size));

	addAM(dam);

	// Ensure the written data matches the sector size code.
	auto len_bytes{ Sector::SizeCodeToLength(size) };
	if (data.size() > len_bytes)
	{
		Data data_head(data.begin(), data.begin() + len_bytes);
		addBlockUpdateCrc(data_head);
	}
	else
	{
		addBlockUpdateCrc(data);
		Data data_pad(len_bytes - data.size(), 0x00);
		addBlockUpdateCrc(data_pad);
	}

	addCrcBytes(crc_error);
}

void TrackBuffer::addSector(const Sector &sector, int gap3_bytes)
{
	setEncoding(sector.encoding);

	switch (m_encoding)
	{
	case Encoding::MFM:
	case Encoding::FM:
	{
		addSectorHeader(sector.header);
		addGap2();
		if (sector.has_data())
			addSectorData(sector.data_copy(), sector.header.size, sector.dam, sector.has_baddatacrc());
		addGap(gap3_bytes);
		break;
	}
	case Encoding::Amiga:
		addAmigaSector(sector.header, sector.header.sector, sector.data_copy().data());
		break;
	case Encoding::RX02:
		addRX02Sector(sector.header, sector.data_copy(), gap3_bytes);
		setEncoding(sector.encoding);
		break;
	default:
		throw util::exception("unsupported sector encoding (", sector.encoding, ")");
	}
}

void TrackBuffer::addSector (const Header &header, const Data &data, int gap3_bytes, uint8_t dam, bool crc_error)
{
	Sector sector(m_datarate, m_encoding, header, gap3_bytes);
	sector.add(Data(data), crc_error, dam);
	addSector(sector, sector.gap3);
}

// Sector header and DAM, but no data, CRC, or gap3 -- for weak sectors.
void TrackBuffer::addSectorUpToData (const Header &header, uint8_t dam)
{
	addSectorHeader(header);
	addGap2();
	addAM(dam);
}


void TrackBuffer::addAmigaTrackStart ()
{
	auto fill{0x00};
	addBlock(fill, 60);		// Shift the first sector away from the index
}

void TrackBuffer::addAmigaDword (uint32_t dword, uint32_t &checksum)
{
	dword = util::htobe(dword);
	std::vector<uint32_t> bits = splitAmigaBits(&dword, sizeof(uint32_t), checksum);
	addAmigaBits(bits);
}

void TrackBuffer::addAmigaBits (std::vector<uint32_t> &bits)
{
	for (auto it = bits.begin(); it != bits.end(); ++it)
	{
		uint32_t data = *it;
		for (auto i = 0; i < 16; ++i)
		{
			addDataBit((data & 0x40000000) != 0);
			data <<= 2;
		}
	}
}

std::vector<uint32_t> TrackBuffer::splitAmigaBits (const void *buf, int len, uint32_t &checksum)
{
	auto dwords = len / static_cast<int>(sizeof(uint32_t));
	const uint32_t *pdw = reinterpret_cast<const uint32_t*>(buf);
	std::vector<uint32_t> odddata;
	odddata.reserve(dwords * 2);

	// Even then odd passes over the data
	for (auto i = 0; i < 2; ++i)
	{
		// All DWORDs in the block
		for (int j = 0; j < dwords; ++j)
		{
			uint32_t bits = 0;
			uint32_t data = frombe32(pdw[j]) << i;

			// 16 bits (odd or even) from each DWORD pass
			for (auto k = 0; k < 16; ++k)
			{
				bits |= ((data & 0x80000000) >> (1 + k * 2));
				data <<= 2;
			}

			odddata.insert(odddata.end(), bits);
			checksum ^= bits;
		}
	}

	return odddata;
}

void TrackBuffer::addAmigaSector (const CylHead &cylhead, int sector, const void *buf)
{
	addByte(0x00);
	addByte(0x00);
	addByteWithClock(0xa1, 0x0a);	// A1 with missing clock bit
	addByteWithClock(0xa1, 0x0a);

	auto sectors = (m_datarate == DataRate::_500K) ? 22 : 11;
	auto remain = sectors - sector;

	uint32_t checksum = 0;
	uint32_t info = (0xff << 24) | (((cylhead.cyl << 1) | cylhead.head) << 16) |
		(sector << 8) | remain;
	addAmigaDword(info, checksum);

	uint32_t sector_label[4] = {};
	auto bits = splitAmigaBits(sector_label, sizeof(sector_label), checksum);
	addAmigaBits(bits);
	addAmigaDword(checksum, checksum);

	checksum = 0;
	bits = splitAmigaBits(buf, 512, checksum);
	addAmigaDword(checksum, checksum);
	addAmigaBits(bits);
}


void TrackBuffer::addRX02Sector(const Header &header, const Data &data, int gap3_bytes)
{
	setEncoding(Encoding::FM);

	addSectorHeader(header);
	addGap2();
	addAM(0xfd);	// RX02 DAM

	setEncoding(Encoding::MFM);

	addBlockUpdateCrc(data);
	addCrcBytes();
	addGap(gap3_bytes);
}
