// Buffer for assembling bitstream data (incomplete)

#include "SAMdisk.h"
#include "BitstreamTrackBuffer.h"

#define INIT_BITSIZE   5000000

BitstreamTrackBuffer::BitstreamTrackBuffer (bool mfm)
	: TrackBuffer(mfm)
{
	m_data.resize((INIT_BITSIZE + 7) / 8);
}

void BitstreamTrackBuffer::addBit (bool one)
{
	size_t offset = m_bitpos / 8;
	uint8_t bit_value = 0x80 >> (m_bitpos & 7);

	// Double the size if we run out of space
	if (offset >= m_data.size())
		m_data.resize(m_data.size() * 2);

	if (one)
		m_data[offset] |= bit_value;
	else
		m_data[offset] &= ~bit_value;

	m_bitsize = std::max(++m_bitpos, m_bitsize);
	m_onelast = one;
}
