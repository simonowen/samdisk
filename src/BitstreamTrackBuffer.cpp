// Buffer for assembling bitstream data (incomplete)

#include "SAMdisk.h"
#include "BitstreamTrackBuffer.h"

#define INIT_BITSIZE   5000000

BitstreamTrackBuffer::BitstreamTrackBuffer (DataRate datarate, Encoding encoding)
	: m_buffer(datarate, encoding)
{
	setEncoding(encoding);
}

void BitstreamTrackBuffer::setEncoding(Encoding encoding)
{
	TrackBuffer::setEncoding(encoding);
	m_buffer.encoding = encoding;
}

void BitstreamTrackBuffer::addRawBit (bool bit)
{
	m_buffer.add(bit);
}

void BitstreamTrackBuffer::addCrc (int size)
{
	auto old_bitpos{ m_buffer.tell() };
	auto byte_bits{ (m_buffer.encoding == Encoding::FM) ? 32 : 16 };
	assert(old_bitpos >= size * byte_bits);
	m_buffer.seek(old_bitpos - size * byte_bits);

	CRC16 crc{};
	while (size-- > 0)
		crc.add(m_buffer.read_byte());

	// Seek back to the starting position to write the CRC.
	m_buffer.seek(old_bitpos);
	addWord(crc);
}

BitBuffer &BitstreamTrackBuffer::buffer()
{
	return m_buffer;
}

DataRate BitstreamTrackBuffer::datarate () const
{
	return m_buffer.datarate;
}

Encoding BitstreamTrackBuffer::encoding () const
{
	return m_buffer.encoding;
}
