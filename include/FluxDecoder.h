#pragma once

#define DEFAULT_PLL_ADJUST  4
#define DEFAULT_PLL_PHASE   60
#define MAX_PLL_ADJUST      50
#define MAX_PLL_PHASE       90

class FluxDecoder
{
public:
    FluxDecoder(const FluxData& flux_revs, int bitcell_ns,
        int flux_scale_percent = 100, int pll_adjust = DEFAULT_PLL_ADJUST);

    bool index();
    bool sync_lost();
    int flux_revs() const;
    int flux_count() const;

    int next_bit();
    int next_flux();

protected:
    const FluxData& m_flux_revs;
    FluxData::const_iterator m_rev_it{};
    std::vector<uint32_t>::const_iterator m_flux_it{};

    int m_clock = 0, m_clock_centre, m_clock_min, m_clock_max;
    int m_flux = 0;
    int m_clocked_zeros = 0;
    int m_flux_scale_percent = 100;
    int m_pll_adjust = 0;
    int m_goodbits = 0;
    bool m_index = false;
    bool m_sync_lost = false;
};
