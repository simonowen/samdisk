#ifndef TRACKDATA_H
#define TRACKDATA_H

#include "Track.h"
#include "BitBuffer.h"

class TrackData
{
public:
	enum class TrackDataType { None, Track, BitStream, Flux };
	enum TrackDataFlags { TD_NONE = 0, TD_TRACK = 1, TD_BITSTREAM = 2, TD_FLUX = 4 };

	TrackData () = default;
	TrackData (const CylHead &cylhead_);
	TrackData (const CylHead &cylhead_, Track &&track);
	TrackData (const CylHead &cylhead_, BitBuffer &&bitstream);
	TrackData (const CylHead &cylhead_, FluxData &&flux, bool normalised=false);

	auto type () const { return m_type; }
	bool has_track () const;
	bool has_bitstream () const;
	bool has_flux () const;
	bool has_normalised_flux () const;

	const Track &track ();
	/*const*/ BitBuffer &bitstream ();
	const FluxData &flux ();

	void add (TrackData &&trackdata);
	void add (Track &&track);
	void add (BitBuffer &&bitstream);
	void add (FluxData &&flux, bool normalised=false);

	CylHead cylhead {};

private:
	TrackDataType m_type{ TrackDataType::None };
	int m_flags{ TD_NONE };

	Track m_track {};
	BitBuffer m_bitstream {};
	FluxData m_flux {};
	bool m_normalised_flux = false;
};

#endif // TRACKDATA_H
