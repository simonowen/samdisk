#ifndef DISK_H
#define DISK_H

const int MAX_DISK_CYLS = 128;
const int MAX_DISK_HEADS = 2;

using FluxData = std::vector<std::vector<uint32_t>>;

#include "TrackData.h"
#include "Format.h"

class Disk
{
public:
	Disk () = default;
	virtual ~Disk () = default;

	explicit Disk (Format &format);

	virtual bool preload (const Range &range, int cyl_step);
	virtual void unload ();

	virtual const TrackData &read (const CylHead &cylhead);
	const Track &read_track (const CylHead &cylhead);
	const BitBuffer &read_bitstream (const CylHead &cylhead);
	const FluxData &read_flux (const CylHead &cylhead);

	virtual const TrackData &write (TrackData &&trackdata);
	const Track &write (const CylHead &cylhead, Track &&track);
	const BitBuffer &write (const CylHead &cylhead, BitBuffer &&bitbuf);
	const FluxData &write (const CylHead &cylhead, FluxData &&flux_revs);

	void each (const std::function<void (const CylHead &cylhead, const Track &track)> &func, bool cyls_first = false);

	void format (const RegularFormat &reg_fmt, const Data &data = Data(), bool cyls_first = false);
	void format (const Format &fmt, const Data &data = Data(), bool cyls_first = false);
	void flip_sides ();
	void resize (int cyls, int heads);

	bool find (const Header &header, const Sector *&found_sector);
	const Sector &get_sector (const Header &header);

	Range range () const;
	int cyls () const;
	int heads () const;

	Format fmt {};
//	CHK8K_METHOD chk8k_method = CHK8K_UNKNOWN;
	std::map<std::string, std::string> metadata {};
	std::string strType = "<unknown>";

protected:
	std::map<CylHead, TrackData> m_trackdata {};
	std::mutex m_trackdata_mutex {};
};

#endif // DISK_H
