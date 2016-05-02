#ifndef FLUXDECODER_H
#define FLUXDECODER_H

class FluxDecoder
{
	// This was originally 10%, but increasing it makes the PLL more responsive
	// to changes in bitcell width caused by damage. So far no side-effects have
	// been seen, and we only claim to support relatively fixed cell widths.
	static const int CLOCK_MAX_ADJUST = 50;	// +/- % PLL adjustment

public:
	FluxDecoder (const FluxData &flux_revs, int bitcell_ns, int flux_scale_percent = 100);

	bool index ();
	bool sync_lost ();
	int flux_revs () const;
	int flux_count () const;

	int next_bit ();
	int next_flux ();

protected:
	const FluxData &m_flux_revs;
	FluxData::const_iterator m_rev_it {};
	std::vector<uint32_t>::const_iterator m_flux_it {};

	int m_clock = 0, m_clock_centre, m_clock_min, m_clock_max;
	int m_flux = 0;
	int m_clocked_zeros = 0;
	int m_flux_scale_percent = 100;
	int m_goodbits = 0;
	bool m_index = false;
	bool m_sync_lost = false;
};

#endif // FLUXDECODER_H
