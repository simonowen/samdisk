// Buffer for assembling flux-level data (incomplete)

#include "SAMdisk.h"
#include "FluxTrackBuffer.h"

FluxTrackBuffer::FluxTrackBuffer (int datarate_kbps, int /*drive_rpm*/, int tick_ns, bool mfm)	// ToDo: consider drive RPM?
	: TrackBuffer(mfm)
{
//	auto track_bitcells = datarate_kbps * 2000 * 60 / drive_rpm;
	auto bitcell_ns = 1000000 / (datarate_kbps * 2);
	m_bitcell_ticks = static_cast<uint16_t>((bitcell_ns + (tick_ns / 2)) / tick_ns);
}

void FluxTrackBuffer::addBit (bool one)
{
	m_total_ticks += m_bitcell_ticks;
	if (one)
	{
		// Record the time since the last reversal, and reset it
		m_buf.push_back(util::htobe(static_cast<uint16_t>(m_total_ticks - m_reverse_ticks)));	// ToDo: remove cast once uint32_t
		m_reverse_ticks = m_total_ticks;
	}

	if (!m_mfm)
	{
		// FM bit cells are 4us, so double the width using a non-transition
		m_total_ticks += m_bitcell_ticks;
		one = false;
	}

	m_onelast = one;
}

