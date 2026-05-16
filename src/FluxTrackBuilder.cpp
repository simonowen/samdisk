// Buffer for assembling flux-level data (incomplete)

#include "SAMdisk.h"
#include "FluxTrackBuilder.h"

FluxTrackBuilder::FluxTrackBuilder(const CylHead& cylhead, DataRate datarate, Encoding encoding)
    : TrackBuilder(datarate, encoding),
    m_cylhead(cylhead), m_bitcell_ns(bitcell_ns(datarate)),
    m_flux_time(0U - m_bitcell_ns)
{
    // We start with a negative cell time to absorb the first zero m_cur_bit.
    // This ensures the first reversal exactly matches the added data.
}

void FluxTrackBuilder::addRawBit(bool next_bit)
{
    m_flux_time += m_bitcell_ns;

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
            auto pre_comp_ns{ (m_last_bit == next_bit) ? 0 : (m_last_bit ? +PRECOMP_NS : -PRECOMP_NS) };

            m_flux_times.push_back(m_flux_time + pre_comp_ns);
            m_flux_time = 0 - pre_comp_ns;
        }
    }

    m_last_bit = m_curr_bit;
    m_curr_bit = next_bit;
}

void FluxTrackBuilder::addWeakBlock(int length)
{
    // Flush out previous constant block.
    addRawBit(1);
    addRawBit(1);

    // Approximately 11 ambiguous reversals per weak byte.
    length = length * 21 / 2;

    while (length-- > 0)
        m_flux_times.push_back(m_bitcell_ns * 3 / 2);
}

std::vector<uint32_t>& FluxTrackBuilder::buffer()
{
    // Flush any buffered time with a transition.
    if (m_flux_time)
    {
        m_flux_times.push_back(m_flux_time);
        m_flux_time = 0;
    }

    return m_flux_times;
}
