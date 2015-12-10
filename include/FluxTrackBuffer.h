#ifndef FLUXTRACKBUFFER_H
#define FLUXTRACKBUFFER_H

#include "TrackBuffer.h"

class FluxTrackBuffer final : public TrackBuffer
{
public:
	FluxTrackBuffer (int datarate_kbps, int trackms, int nspertick, bool mfm = true);

	operator const uint16_t* () const { return &m_buf[0]; }
	operator const std::vector<uint16_t>& () const { return m_buf; }

	void addBit (bool one) override;

private:
	std::vector<uint16_t> m_buf {};	// ToDo: make this uint32_t to be general?
	uint16_t m_bitcell_ticks = 0;
	uint32_t m_total_ticks = 0;
	uint32_t m_reverse_ticks = 0;
};

#endif // FLUXTRACKBUFFER_H
