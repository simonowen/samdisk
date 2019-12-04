// FDC-like flux reversal decoding
//
// PLL code from Keir Frasier's Disk-Utilities/libdisk

#include "SAMdisk.h"
#include "FluxDecoder.h"

FluxDecoder::FluxDecoder(const FluxData& flux_revs, int bitcell_ns, int flux_scale_percent, int pll_adjust)
    : m_flux_revs(flux_revs), m_clock(bitcell_ns), m_clock_centre(bitcell_ns),
    m_clock_min(bitcell_ns* (100 - pll_adjust) / 100),
    m_clock_max(bitcell_ns* (100 + pll_adjust) / 100),
    m_flux_scale_percent(flux_scale_percent),
    m_pll_adjust(pll_adjust)
{
    assert(flux_revs.size());

    m_rev_it = m_flux_revs.cbegin();
    m_flux_it = (*m_rev_it).cbegin();
}

int FluxDecoder::flux_revs() const
{
    return static_cast<int>(m_flux_revs.size());
}

int FluxDecoder::flux_count() const
{
    auto count = 0;

    for (const auto& vec : m_flux_revs)
        count += static_cast<int>(vec.size());

    return count;
}

bool FluxDecoder::index()
{
    auto ret = m_index;
    m_index = false;
    return ret;
}

bool FluxDecoder::sync_lost()
{
    auto ret = m_sync_lost;
    m_sync_lost = false;
    return ret;
}

int FluxDecoder::next_bit()
{
    int new_flux;

    while (m_flux < m_clock / 2)
    {
        if ((new_flux = next_flux()) == -1)
            return -1;

        if (m_flux_scale_percent != 100)
            new_flux = new_flux * m_flux_scale_percent / 100;

        m_flux += new_flux;
        m_clocked_zeros = 0;
    }

    m_flux -= m_clock;

    if (m_flux >= m_clock / 2)
    {
        ++m_clocked_zeros;
        ++m_goodbits;
        return 0;
    }

    // PLL: Adjust clock frequency according to phase mismatch
    if (m_clocked_zeros <= 3)
    {
        // In sync: adjust base clock by percentage of phase mismatch
        m_clock += m_flux * m_pll_adjust / 100;
    }
    else
    {
        // Out of sync: adjust base clock towards centre
        m_clock += (m_clock_centre - m_clock) * m_pll_adjust / 100;

        // Require 256 good bits before reporting another loss of sync
        if (m_goodbits >= 256)
            m_sync_lost = true;

        m_goodbits = 0;
    }

    // Clamp the clock's adjustment range
    m_clock = std::min(std::max(m_clock_min, m_clock), m_clock_max);

    // Authentic PLL: Do not snap the timing window to each flux transition
    new_flux = m_flux * (100 - opt.pllphase) / 100;
    m_flux = new_flux;

    ++m_goodbits;
    return 1;
}

int FluxDecoder::next_flux()
{
    if (m_flux_it == (*m_rev_it).cend())
    {
        if (++m_rev_it == m_flux_revs.cend())
            return -1;

        m_index = true;
        m_flux_it = (*m_rev_it).cbegin();
        if (m_flux_it == (*m_rev_it).cend())
            return -1;
    }

    auto time_ns = *m_flux_it++;
    return time_ns;
}
