// Buffer for assembling bitstream data (incomplete)

#include "SAMdisk.h"
#include "BitstreamTrackBuilder.h"

#define INIT_BITSIZE   5000000

BitstreamTrackBuilder::BitstreamTrackBuilder(DataRate datarate, Encoding encoding)
    : TrackBuilder(datarate, encoding), m_buffer(datarate, encoding)
{
}

int BitstreamTrackBuilder::size() const
{
    return m_buffer.size();
}

void BitstreamTrackBuilder::setEncoding(Encoding encoding)
{
    TrackBuilder::setEncoding(encoding);
    m_buffer.encoding = encoding;
}

void BitstreamTrackBuilder::addRawBit(bool bit)
{
    m_buffer.add(bit);
}

void BitstreamTrackBuilder::addCrc(int size)
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
    addByte(crc >> 8);
    addByte(crc & 0xff);
}

BitBuffer& BitstreamTrackBuilder::buffer()
{
    return m_buffer;
}

DataRate BitstreamTrackBuilder::datarate() const
{
    return m_buffer.datarate;
}

Encoding BitstreamTrackBuilder::encoding() const
{
    return m_buffer.encoding;
}
