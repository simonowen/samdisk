// Buffer for assembling flux-level data (incomplete)

#include "SAMdisk.h"
#include "FluxTrackBuffer.h"

FluxTrackBuffer::FluxTrackBuffer (const CylHead &cylhead, DataRate datarate, Encoding encoding)
	: TrackBuffer(encoding), m_cylhead(cylhead), m_bitcell_ns(bitcell_ns(datarate))
{
	switch (encoding)
	{
	case Encoding::MFM:
	case Encoding::FM:
	case Encoding::Amiga:
		break;
	default:
		throw util::exception("unsupported flux track encoding (", encoding, ")");
	}
}

void FluxTrackBuffer::addBit (bool next_bit)
{
	m_flux_time += (m_encoding == Encoding::FM) ? (m_bitcell_ns * 2) : m_bitcell_ns;

	if (m_curr_bit)
	{
		if (m_cylhead.cyl < 40)
		{
			m_flux_times.push_back(m_flux_time);
			m_flux_time = 0;
		}
		else
		{
			// Move adjacent transitions further apart, to account for attraction when written.
			auto pre_comp_ns{(m_last_bit == next_bit) ? 0 : (m_last_bit ? +PRECOMP_NS : -PRECOMP_NS)};

			m_flux_times.push_back(m_flux_time + pre_comp_ns);
			m_flux_time = 0 - pre_comp_ns;
		}
	}

	m_last_bit = m_curr_bit;
	m_curr_bit = next_bit;
}

void FluxTrackBuffer::addWeakBlock (int length)
{
	// Flush out previous constant block.
	addBit(1);
	addBit(1);

	// Approximately 11 ambigious reversals per weak byte.
	length = length * 21 / 2;

	while (length-- > 0)
		m_flux_times.push_back(m_bitcell_ns * 3 / 2);
}

std::vector<uint32_t> &FluxTrackBuffer::buffer ()
{
	// Flush any buffered time with a transition.
	if (m_flux_time)
	{
		m_flux_times.push_back(m_flux_time);
		m_flux_time = 0;
	}

	return m_flux_times;
}
