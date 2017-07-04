// Bit buffer used for assembling tracks

#include "SAMdisk.h"
#include "TrackBuffer.h"

TrackBuffer::TrackBuffer (Encoding encoding)
	: m_encoding(encoding)
{
	switch (encoding)
	{
	case Encoding::MFM:
	case Encoding::FM:
	case Encoding::Amiga:
		break;
	default:
		throw util::exception("unsupported bitstream encoding (", encoding, ")");
	}
}

void TrackBuffer::addDataBit (bool one)
{
	if (m_encoding == Encoding::FM)
	{
		// FM has a reversal before every data bit
		addBit(true);
		addBit(one);
	}
	else
	{
		// MFM has a reversal between consecutive zeros (clock or data)
		addBit(!m_onelast && !one);
		addBit(one);
	}

	m_onelast = one;
}

void TrackBuffer::addByte (int byte)
{
	for (auto i = 0; i < 8; ++i)
	{
		addDataBit((byte & 0x80) != 0);
		byte <<= 1;
	}
}

void TrackBuffer::addByteBits (int byte, int num_bits)
{
	byte <<= (8 - num_bits);

	for (auto i = 0; i < num_bits; ++i)
	{
		// Add bottom 'num_bits' bits, most-significant first.
		addDataBit((byte & 0x80) != 0);
		byte <<= 1;
	}
}

void TrackBuffer::addByteWithClock(int data, int clock)
{
	for (auto i = 0; i < 8; ++i)
	{
		addBit((clock & 0x80) != 0);
		addBit((data & 0x80) != 0);
		clock <<= 1;
		data <<= 1;
	}

	m_onelast = (data & 0x100) != 0;
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
	addBlock(fill, count);
}

void TrackBuffer::addSync ()
{
	auto sync{0x00};
	addBlock(sync, (m_encoding == Encoding::FM) ? 6 : 12);
}

void TrackBuffer::addAM (int type)
{
	if (m_encoding == Encoding::FM)
	{
		// FM address marks use clock of C7
		addByteWithClock(type, 0xc7);

		m_crc.init();
		m_crc.add(type);
	}
	else
	{
		addByteWithClock(0xa1, 0x0a);	// A1 with missing clock bit
		addByteWithClock(0xa1, 0x0a);	// clock: 0 0 0 0 1 X 1 0
		addByteWithClock(0xa1, 0x0a);	// data:   1 0 1 0 0 0 0 1
		addByte(type);

		m_crc.init(0xcdb4);		// A1A1A1
		m_crc.add(type);
	}
}

void TrackBuffer::addIAM ()
{
	if (m_encoding == Encoding::FM)
	{
		// FM IAM uses a clock of D7
		addByteWithClock(0xfc, 0xd7);
	}
	else
	{
		addByteWithClock(0xc2, 0x14);	// C2 with missing clock bit
		addByteWithClock(0xc2, 0x14);	// clock: 0 0 0 1 X 1 0 0
		addByteWithClock(0xc2, 0x14);	// data:   1 1 0 0 0 0 1 0
		addByte(0xfc);
	}
}

void TrackBuffer::addIDAM ()
{
	addAM(0xfe);
}

void TrackBuffer::addDAM ()
{
	addAM(0xfb);
}

void TrackBuffer::addDDAM ()
{
	addAM(0xf8);
}

void TrackBuffer::addAltDAM ()
{
	addAM(0xfa);
}

void TrackBuffer::addAltDDAM ()	// RX02
{
	addAM(0xfd);
}

void TrackBuffer::addCRC ()
{
	addByte(m_crc >> 8);
	addByte(m_crc & 0xff);
}

void TrackBuffer::addTrackStart ()
{
	//  System/34 double density
	addGap(80);	// gap 4a
	addSync();
	addIAM();
	addGap(50);	// gap 1
}

void TrackBuffer::addSectorHeader (int cyl, int head, int sector, int size)
{
	addIDAM();
	addByte(cyl);
	addByte(head);
	addByte(sector);
	addByte(size);

	m_crc.add(cyl);
	m_crc.add(head);
	m_crc.add(sector);
	m_crc.add(size);
	addCRC();
}

void TrackBuffer::addSectorHeader(const Header &header)
{
	addSectorHeader(header.cyl, header.head, header.sector, header.size);
}

void TrackBuffer::addSectorData(const Data &data, bool deleted)
{
	auto am{deleted ? 0xf8 : 0xfb};
	addAM(am);

	if (data.size() > 0)
	{
		addBlockUpdateCrc(data);
		addCRC();
	}
}

void TrackBuffer::addSector (int cyl, int head, int sector, int size, const Data &data, int gap3, bool deleted)
{
	addSync();
	addSectorHeader(cyl, head, sector, size);
	addGap((m_encoding == Encoding::FM) ? 11 : 22);	// gap 2
	addSync();
	addSectorData(data, deleted);
	addGap(gap3);	// gap 3
}

void TrackBuffer::addSector (const Header &header, const Data &data, int gap3, bool deleted)
{
	addSector(header.cyl, header.head, header.sector, header.size, data, gap3, deleted);
}

// Sector header and DAM, but no data, CRC, or gap3 -- for weak sectors.
void TrackBuffer::addSectorUpToData (const Header &header, bool deleted)
{
	addSector(header.cyl, header.head, header.sector, header.size, Data(), 0, deleted);
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

void TrackBuffer::addAmigaSector (int cyl, int head, int sector, int remain, const void *buf)
{
	addByte(0x00);
	addByte(0x00);
	addByteWithClock(0xa1, 0x0a);	// A1 with missing clock bit
	addByteWithClock(0xa1, 0x0a);

	uint32_t checksum = 0;
	uint32_t info = (0xff << 24) | (((cyl << 1) | head) << 16) | (sector << 8) | remain;
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

void TrackBuffer::addAmigaSector(const CylHead &cylhead, int sector, int remain, const void *buf)
{
	addAmigaSector(cylhead.cyl, cylhead.head, sector, remain, buf);
}
