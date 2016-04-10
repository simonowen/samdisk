#ifndef DISK_H
#define DISK_H

const int MAX_DISK_CYLS = 128;
const int MAX_DISK_HEADS = 2;

enum class DataRate : int { Unknown = 0, _250K = 250'000, _300K = 300'000, _500K = 500'000, _1M = 1'000'000 };
enum class Encoding { Unknown, MFM, FM, Amiga, GCR, Ace };

#include "BitBuffer.h"

std::string to_string (const DataRate &datarate);
std::string to_string (const Encoding &encoding);

inline int bitcell_ns (DataRate datarate)
{
	switch (datarate)
	{
		case DataRate::Unknown:	break;
		case DataRate::_250K:	return 2000;
		case DataRate::_300K:	return 1667;
		case DataRate::_500K:	return 1000;
		case DataRate::_1M:		return 500;
	}

	return 0;
}

inline int bits_per_second (DataRate datarate)
{
#if 1
	return static_cast<unsigned>(datarate);
#else
	switch (datarate)
	{
		case DataRate::Unknown:	break;
		case DataRate::_250K:	return 250000;
		case DataRate::_300K:	return 300000;
		case DataRate::_500K:	return 500000;
		case DataRate::_1M:		return 1000000;
	}

	return 0;
#endif
}

inline std::ostream & operator<<(std::ostream& os, const DataRate dr) { return os << to_string(dr); }
inline std::ostream & operator<<(std::ostream& os, const Encoding e) { return os << to_string(e); }

struct CylHead
{
	CylHead () = default;
	CylHead(int cyl_, int head_) : cyl(cyl_), head(head_)
	{
		assert(cyl >= 0 && cyl <= MAX_DISK_CYLS);
		assert(head >= 0 && head <= MAX_DISK_HEADS);
	}

	operator int() const { return (cyl * MAX_DISK_HEADS) + head; }
	std::string to_string () const
	{
#if 0	// ToDo
		if (opt.hex == 1)
			return util::format("cyl %02X head %u", cyl, head);
#endif

		return util::fmt("cyl %u head %u", cyl, head);
	}

	CylHead next_cyl ()
	{
		CylHead cylhead(*this);
		++cyl;
		assert(cyl < MAX_DISK_CYLS);
		return cylhead;
	}

	int cyl = -1, head = -1;
};

inline std::ostream & operator<<(std::ostream& os, const CylHead &cylhead) { return os << cylhead.to_string(); }

struct CylHeadSector
{
	CylHeadSector (uint16_t cyl_, uint8_t head_, uint8_t sector_)
		: cyl(cyl_), head(head_), sector(sector_)
	{
	}

	int cyl, head, sector;
};


class Range
{
public:
	Range () = default;
	Range (int cyls, int heads);
	Range (int cyl_begin_, int cyl_end_, int head_begin, int head_end);

	bool empty () const;
	int cyls () const;
	int heads () const;
	const CylHead &begin () const;
	const CylHead &end () const;
	bool contains (const CylHead &cylhead);
	void each (const std::function<void (const CylHead &cylhead)> &func, bool cyls_first = false) const;

	int cyl_begin = 0, cyl_end = 0;
	int head_begin = 0, head_end = 0;
};

std::string to_string (const Range &r);
inline std::ostream & operator<<(std::ostream& os, const Range &r) { return os << to_string(r); }

enum class FdcType { None, PC, WD, Amiga };
enum class RegularFormat { MGT, ProDos, PC320, PC360, PC640, PC720, PC1200, PC1232, PC1440, PC2880, D80, OPD, MBD820, MBD1804, TRDOS, D2M, D4M, D81, _2D, AmigaDOS, AmigaDOSHD, LIF, AtariST };

struct Format
{
	static const int DefaultTracks = 80;
	static const int DefaultSides = 2;

	Format () = default;
	Format (RegularFormat reg_fmt);

	int sector_size() const;
	int track_size() const;
	int side_size() const;
	int disk_size() const;
	int total_sectors() const;
	Range range () const;

	std::vector<int> get_ids (const CylHead &cylhead) const;
	static Format get (RegularFormat reg_format);

	int cyls = DefaultTracks;
	int heads = DefaultSides;

	FdcType fdc = FdcType::PC;				// FDC type for head matching rules
	DataRate datarate = DataRate::Unknown;	// Data rate within encoding
	Encoding encoding = Encoding::Unknown;	// Data encoding scheme
	int sectors = 0, size = 2;				// sectors/track, sector size code
	int base = 1, offset = 0;				// base sector number, offset into cyl 0 head 0
	int interleave = 1, skew = 0;			// sector interleave, track skew
	int head0 = 0, head1 = 1;				// head 0 value, head 1 value
	int gap3 = 0;							// inter-sector gap
	uint8_t fill = 0x00;					// Fill byte
	bool cyls_first = false;				// True if media order is cyls on head 0 before head 1
};


class Header
{
public:
	Header () = default;
	Header (int cyl, int head, int sector, int size);
	Header (const CylHead &cylhead, int sector, int size);

	bool operator== (const Header &rhs) const;
	bool operator!= (const Header &rhs) const;
	operator CylHead() const;

	int sector_size () const;
	bool compare (const Header &rhs) const;

	int cyl = 0, head = 0, sector = 0, size = 0;
};

//using Data = std::vector<uint8_t>;
class Data : public std::vector<uint8_t>
{
public:
	using std::vector<uint8_t>::vector;
	int size () const { return static_cast<int>(std::vector<uint8_t>::size()); }
};

using DataList = std::vector<Data>;

class Sector
{
public:
	enum class Merge { Unchanged, Improved, NewData };

public:
	Sector (DataRate datarate, Encoding encoding, const Header &header = Header(), int gap3 = 0);
	bool operator==(const Sector &sector) const;

	Merge merge (Sector &&sector);

	bool has_data () const;
	bool has_gapdata () const;
	bool has_shortdata () const;
	bool has_badidcrc () const;
	bool has_baddatacrc () const;
	bool is_deleted () const;
	bool is_altdam () const;
	bool is_rx02dam () const;
	bool is_8k_sector () const;

	void set_badidcrc (bool bad = true);
	void set_baddatacrc (bool bad = true);
	void remove_data ();
	void remove_gapdata ();

	int size () const;
	int data_size () const;

	const DataList &datas () const;
	DataList &datas ();
	const Data &data_copy (int copy = 0) const;
	Data &data_copy (int copy = 0);

	Merge add (Data &&data, bool bad_crc = false, uint8_t dam = 0xfb);
	int copies() const;

	static int SizeCodeToRealSizeCode (int size);
	static int SizeCodeToLength (int size);

public:
	Header header { 0,0,0,0 };				// cyl, head, sector, size
	DataRate datarate = DataRate::Unknown;	// 250Kbps
	Encoding encoding = Encoding::Unknown;	// MFM
	int offset = 0;							// bitstream offset from index, in bits
	int gap3 = 0;							// inter-sector gap size
	uint8_t dam = 0xfb;						// data address mark

private:
	bool m_bad_id_crc = false;
	bool m_bad_data_crc = false;
	std::vector<Data> m_data {};			// copies of sector data
};

class Track
{
public:
	enum class AddResult { Unchanged, Append, Insert, Merge };

public:
	explicit Track (int sectors = 0);	// sectors to reserve

	bool empty() const;
	int size() const;
	const std::vector<Sector> &sectors () const;
	std::vector<Sector> &sectors ();
	const Sector &operator [] (int index) const;
	Sector &operator [] (int index);
	int index_of (const Sector &sector) const;

	int data_extent (const Sector &sector) const;
	bool is_mixed_encoding () const;
	bool is_8k_sector () const;
	bool is_repeated (const Sector &sector) const;

	void clear ();
	void add (Track &&track);
	AddResult add (Sector &&sector);
	Sector remove (int index);
	const Sector &get_sector (const Header &header) const;

	Track &format (const CylHead &cylhead, const Format &format);
	Data::const_iterator populate (Data::const_iterator it, Data::const_iterator itEnd);

	std::vector<Sector>::iterator begin () { return m_sectors.begin(); }
	std::vector<Sector>::iterator end () { return m_sectors.end(); }
	std::vector<Sector>::iterator find (const Sector &sector);
	std::vector<Sector>::iterator find (const Header &header);

	std::vector<Sector>::const_iterator begin () const { return m_sectors.begin(); }
	std::vector<Sector>::const_iterator end () const { return m_sectors.end(); }
	std::vector<Sector>::const_iterator find (const Sector &sector) const;
	std::vector<Sector>::const_iterator find (const Header &header) const;

	int tracklen = 0;	// track length in MFM bits
	int tracktime = 0;	// track time in us
	bool modified = false;	// signs of data splices?

private:
	std::vector<Sector> m_sectors {};

	// Max bitstream position difference for sectors to be considerd the same.
	// Used to match sectors between revolutions, and needs to cope with the
	// larger sync differences after weak sectors. We still require the header
	// to match, so only close repeated headers should be a problem.
	static const int COMPARE_TOLERANCE_BITS = 64 * 16;
};

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
