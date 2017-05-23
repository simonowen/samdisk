// Buffer for assembling bitstream data (incomplete)

#include "SAMdisk.h"
#include "BitstreamTrackBuffer.h"

#define INIT_BITSIZE   5000000

BitstreamTrackBuffer::BitstreamTrackBuffer (DataRate datarate, Encoding encoding)
	: TrackBuffer(encoding != Encoding::FM), m_buffer(datarate, 1)
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

void BitstreamTrackBuffer::addBit (bool one)
{
	m_buffer.add(one);
}

BitBuffer &BitstreamTrackBuffer::buffer()
{
	return m_buffer;
}
