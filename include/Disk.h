#ifndef DISK_H
#define DISK_H

const int MAX_DISK_CYLS = 128;
const int MAX_DISK_HEADS = 2;

#include "Track.h"
#include "Format.h"
#include "BitBuffer.h"


class Disk
{
public:
	Disk () = default;
	virtual ~Disk () = default;

	explicit Disk (Format &format);

	virtual void preload (const Range &/*range*/) {}
	virtual void unload (bool source_only=false);

	bool get_bitstream_source (const CylHead &cylhead, BitBuffer* &p);
	bool get_flux_source (const CylHead &cylhead, const std::vector<std::vector<uint32_t>>* &p);
	void set_source (const CylHead &cylhead, BitBuffer &&data);
	void set_source (const CylHead &cylhead, std::vector<std::vector<uint32_t>> &&data);

	virtual const Track &read_track (const CylHead &cylhead);
	virtual Track &write_track (const CylHead &cylhead, const Track &track);
	virtual Track &write_track (const CylHead &cylhead, Track &&track);
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
	std::map<CylHead, Track> m_tracks {};
	std::map<CylHead, std::vector<std::vector<uint32_t>>> m_fluxdata {};
	std::map<CylHead, BitBuffer> m_bitstreamdata {};
};

#endif // DISK_H
