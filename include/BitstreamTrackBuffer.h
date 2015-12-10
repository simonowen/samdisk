#ifndef BITSTREAMTRACKBUFFER_H
#define BITSTREAMTRACKBUFFER_H

#include "TrackBuffer.h"

class BitstreamTrackBuffer final : public TrackBuffer
{
public:
	explicit BitstreamTrackBuffer (bool mfm = true);

	void addBit (bool one) override;

private:
	std::vector<uint8_t> m_data {};
	std::vector<size_t> m_indexes {};
	size_t m_bitsize = 0;
	size_t m_bitpos = 0;
};

#endif // BITSTREAMTRACKBUFFER_H
