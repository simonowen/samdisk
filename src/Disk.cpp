// Core disk class

#include "SAMdisk.h"
#include "Disk.h"
#include "IBMPC.h"

// ToDo: split classes into separate files

std::string to_string (const DataRate &datarate)
{
	switch (datarate)
	{
		case DataRate::_250K:	return "250Kbps";		break;
		case DataRate::_300K:	return "300Kbps";		break;
		case DataRate::_500K:	return "500Kbps";		break;
		case DataRate::_1M:		return "1Mbps";			break;
		case DataRate::Unknown:	break;
	}
	return "Unknown";
}

std::string to_string (const Encoding &encoding)
{
	switch (encoding)
	{
		case Encoding::MFM:		return "MFM";			break;
		case Encoding::FM:		return "FM";			break;
		case Encoding::GCR:		return "GCR";			break;
		case Encoding::Amiga:	return "Amiga";			break;
		case Encoding::Ace:		return "Ace";			break;
		case Encoding::Unknown:	break;
	}
	return "Unknown";
}

Header::Header (int cyl_, int head_, int sector_, int size_)
	: cyl(cyl_), head(head_), sector(sector_), size(size_)
{
}

Header::Header (const CylHead &cylhead, int sector_, int size_)
	: cyl(cylhead.cyl), head(cylhead.head), sector(sector_), size(size_)
{
}

bool Header::operator== (const Header &rhs) const
{
	return compare(rhs);
}

bool Header::operator!= (const Header &rhs) const
{
	return !compare(rhs);
}

Header::operator CylHead() const
{
	return CylHead(cyl, head);
}

bool Header::compare (const Header &rhs) const
{
	return cyl == rhs.cyl &&
		   //head == rhs.head &&		don't require a head match, like WD17xx
		sector == rhs.sector &&
		size == rhs.size;
}

int Header::sector_size () const
{
	return Sector::SizeCodeToLength(size);
}

//////////////////////////////////////////////////////////////////////////////

Sector::Sector (DataRate datarate_, Encoding encoding_, const Header &header_, int gap3_)
	: header(header_), datarate(datarate_), encoding(encoding_), gap3(gap3_)
{
}

bool Sector::operator== (const Sector &sector) const
{
	// Headers must match
	if (sector.header != header)
		return false;

	// If neither has data it's a match
	if (sector.m_data.size() == 0 && m_data.size() == 0)
		return true;

	// Both sectors must have some data
	if (sector.copies() == 0 || copies() == 0)
		return false;

	// Both first sectors must have at least the natural size to compare
	if (sector.data_size() < sector.size() || data_size() < size())
		return false;

	// The natural data contents must match
	return std::equal(data_copy().begin(), data_copy().begin() + size(), sector.data_copy().begin());
}

int Sector::size () const
{
	return header.sector_size();
}

int Sector::data_size () const
{
	return copies() ? static_cast<int>(m_data[0].size()) : 0;
}

const DataList &Sector::datas () const
{
	return m_data;
}

DataList &Sector::datas ()
{
	return m_data;
}

const Data &Sector::data_copy (int copy/*=0*/) const
{
	copy = std::max(std::min(copy, static_cast<int>(m_data.size()) - 1), 0);
	return m_data[copy];
}

Data &Sector::data_copy (int copy/*=0*/)
{
	assert(m_data.size() != 0);
	copy = std::max(std::min(copy, static_cast<int>(m_data.size()) - 1), 0);
	return m_data[copy];
}

int Sector::copies () const
{
	return static_cast<int>(m_data.size());
}

Sector::Merge Sector::add (Data &&data, bool bad_crc, uint8_t new_dam)
{
	Merge ret = Merge::NewData;
	assert(!copies() || dam == new_dam);

	// Limit the number of data copies kept (default is 3)
	if (copies() >= opt.maxcopies)
		return Merge::Unchanged;

	// If the sector has a bad header CRC, it can't have any data
	if (has_badidcrc())
		return Merge::Unchanged;

	// If there's enough data, check the CRC state
	if (static_cast<int>(data.size()) >= (size() + 2))
	{
		CRC16 crc;
		if (encoding == Encoding::MFM) crc.init(CRC16::A1A1A1);
		crc.add(new_dam);
		auto bad_data_crc = crc.add(data.data(), size() + 2) != 0;
		assert(bad_crc == bad_data_crc);
		(void)bad_data_crc;
	}

	// If the exising sector has good data, ignore supplied data if it's bad
	if (bad_crc && copies() && !has_baddatacrc())
		return Merge::Unchanged;

	// If the existing sector is bad, new good data will replace it all
	if (!bad_crc && has_baddatacrc())
	{
		remove_data();
		ret = Merge::Improved;
	}

	// 8K sectors always have a CRC error, but may include a secondary checksum
	if (is_8k_sector())
	{
		// Attempt to identify the 8K checksum method used by the new data
		auto chk8k_method = Get8KChecksumMethod(data.data(), data.size());

		// If it's recognised, replace any existing data with it
		if (chk8k_method >= CHK8K_FOUND)
		{
			remove_data();
			ret = Merge::Improved;
		}
		// Do we already have a copy?
		else if (copies() == 1)
		{
			// Can we identify the method used by the existing copy?
			chk8k_method = Get8KChecksumMethod(m_data[0].data(), m_data[0].size());
			if (chk8k_method >= CHK8K_FOUND)
			{
				// Keep the existing, ignoring the new data
				return Merge::Unchanged;
			}
		}
	}

	// Look for existing data that is a superset of what we're adding
	auto it = std::find_if(m_data.begin(), m_data.end(), [&] (const Data &d) {
		return d.size() >= data.size() && std::equal(data.begin(), data.end(), d.begin());
	});

	// Return if we already have a better copy
	if (it != m_data.end())
		return Merge::Unchanged;

	// Look for existing data that is a subset of what we're adding
	it = std::find_if(m_data.begin(), m_data.end(), [&] (const Data &d) {
		return d.size() <= data.size() && std::equal(d.begin(), d.end(), data.begin());
	});

	// Remove the inferior copy
	if (it != m_data.end())
	{
		m_data.erase(it);
		ret = Merge::Improved;
	}

	// DD 8K sectors are considered complete at 6K, everything else at natural size
	auto complete_size = is_8k_sector() ? 0x1800 : data.size();

	// Is the supplied data enough for a complete sector?
	if (data.size() >= complete_size)
	{
		// Look for existing data that contains the same normal sector data
		it = std::find_if(m_data.begin(), m_data.end(), [&] (const Data &d) {
			return d.size() >= complete_size && std::equal(d.begin(), d.begin() + complete_size, data.begin());
		});

		// Found a match?
		if (it != m_data.end())
		{
			// Return if the new one isn't larger
			if (data.size() <= it->size())
				return Merge::Unchanged;

			// Remove the existing smaller copy
			m_data.erase(it);
		}

		// Will we now have multiple copies?
		if (m_data.size() > 0)
		{
			// Keep multiple copies the same size, whichever is shortest
			auto new_size = std::min(data.size(), m_data[0].size());
			data.resize(new_size);

			// Resize any existing copies to match
			for (auto &d : m_data)
				d.resize(new_size);
		}
	}

	// Insert the new data copy
	m_data.emplace_back(std::move(data));

	// Update the data CRC state and DAM
	m_bad_data_crc = bad_crc;
	dam = new_dam;

	return ret;
}

Sector::Merge Sector::merge (Sector &&sector)
{
	Merge ret = Merge::Unchanged;

	// If the new header CRC is bad there's nothing we can use
	if (sector.has_badidcrc())
		return Merge::Unchanged;

	// Something is wrong if the new details don't match the existing one
	assert(sector.header == header);
	assert(sector.datarate == datarate);
	assert(sector.encoding == encoding);

	// If the existing header is bad, repair it
	if (has_badidcrc())
	{
		header = sector.header;
		set_badidcrc(false);
		ret = Merge::Improved;
	}

	// We can't repair good data with bad
	if (!has_baddatacrc() && sector.has_baddatacrc())
		return ret;

	// Add the new data snapshots
	for (Data &data : sector.m_data)
	{
		// Move the data into place, passing on the existing data CRC status and DAM
		if (add(std::move(data), sector.has_baddatacrc(), sector.dam) != Merge::Unchanged)
			ret = Merge::Improved;	// ToDo: detect NewData return?
	}
	sector.m_data.clear();

	return ret;
}


bool Sector::has_data () const
{
	return copies() != 0;
}

bool Sector::has_gapdata () const
{
	return data_size() > size();
}

bool Sector::has_shortdata () const
{
	return data_size() < size();
}

bool Sector::has_badidcrc () const
{
	return m_bad_id_crc;
}

bool Sector::has_baddatacrc () const
{
	return m_bad_data_crc;
}

bool Sector::is_deleted () const
{
	return dam == 0xf8 || dam == 0xf9;
}

bool Sector::is_altdam () const
{
	return dam == 0xfa;
}

bool Sector::is_rx02dam () const
{
	return dam == 0xfd;
}

bool Sector::is_8k_sector () const
{
	// +3 and CPC disks treat this as a virtual complete sector
	return datarate == DataRate::_250K && encoding == Encoding::MFM && header.size == 6;
}

void Sector::set_badidcrc (bool bad)
{
	m_bad_id_crc = bad;

	if (bad)
		remove_data();
}

void Sector::remove_data ()
{
	m_data.clear();
	m_bad_data_crc = false;
	dam = 0xfb;
}

void Sector::remove_gapdata ()
{
	if (!has_gapdata())
		return;

	for (auto &data : m_data)
		data.resize(size());
}

// Map a size code to how it's treated by the uPD765 FDC on the PC
int Sector::SizeCodeToRealSizeCode (int size)
{
	// Sizes above 8 are treated as 8 (32K)
	return (size <= 7) ? size : 8;
}

// Return the sector length for a given sector size code
int Sector::SizeCodeToLength (int size)
{
	// 2 ^ (7 + size)
	return 128 << SizeCodeToRealSizeCode(size);
}

//////////////////////////////////////////////////////////////////////////////

Track::Track (int num_sectors/*=0*/)
{
	m_sectors.reserve(num_sectors);
}

bool Track::empty() const
{
	return m_sectors.empty();
}

int Track::size() const
{
	return static_cast<int>(m_sectors.size());
}
/*
EncRate Track::encrate(EncRate preferred) const
{
	std::map<EncRate, int> freq;

	for (auto sector : m_sectors)
		++freq[sector.encrate];

	auto it = std::max_element(freq.begin(), freq.end(), [] (const std::pair<EncRate, int> &a, const std::pair<EncRate, int> &b) {
		return a.second < b.second;
	});

	if (it == freq.end() || it->second == freq[preferred])
		return preferred;

	return it->first;
}
*/

const std::vector<Sector> &Track::sectors() const
{
	return m_sectors;
}

std::vector<Sector> &Track::sectors()
{
	return m_sectors;
}

const Sector &Track::operator [] (int index) const
{
	assert(index < static_cast<int>(m_sectors.size()));
	return m_sectors[index];
}

Sector &Track::operator [] (int index)
{
	assert(index < static_cast<int>(m_sectors.size()));
	return m_sectors[index];
}

int Track::index_of (const Sector &sector) const
{
	auto it = std::find_if(begin(), end(), [&] (const Sector &s) {
		return &s == &sector;
	});

	return (it == end()) ? -1 : static_cast<int>(std::distance(begin(), it));
}

int Track::data_extent (const Sector &sector) const
{
	auto it = find(sector);
	assert(it != end());

	auto drive_speed = (sector.datarate == DataRate::_300K) ? RPM_TIME_360 : RPM_TIME_300;
	auto track_len = tracklen ? tracklen : GetTrackCapacity(drive_speed, sector.datarate, sector.encoding);

	// Approximate distance to next ID header
	auto gap = (std::next(it) != end()) ? std::next(it)->offset : track_len - sector.offset;
	auto overhead = (std::next(it) != end()) ? GetSectorOverhead(sector.encoding) - GetSyncOverhead(sector.encoding) : 0;
	auto extent = (gap > overhead) ? gap - overhead : 0;

	return extent;
}

bool Track::is_mixed_encoding () const
{
	if (empty())
		return false;

	auto first_encoding = m_sectors[0].encoding;

	auto it = std::find_if(begin() + 1, end(), [&] (const Sector &s) {
		return s.encoding != first_encoding;
	});

	return it != end();
}

bool Track::is_8k_sector () const
{
	return size() == 1 && m_sectors[0].is_8k_sector();
}

bool Track::is_repeated (const Sector &sector) const
{
	for (const auto &s : sectors())
	{
		// Ignore ourselves in the list
		if (&s == &sector)
			continue;

		// Stop if we find match for data rate, encoding, and CHRN
		if (s.datarate == sector.datarate &&
			s.encoding == sector.encoding &&
			s.header == sector.header)
			return true;
	}

	return false;
}

void Track::clear ()
{
	m_sectors.clear();
	tracklen = 0;
}

void Track::add (Track &&track)
{
	// Use longest track length
	tracklen = std::max(tracklen, track.tracklen);

	// Merge supplied sectors into existing track
	for (auto &s : track.sectors())
	{
		assert(s.offset != 0);
		add(std::move(s));
	}
}

Track::AddResult Track::add (Sector &&sector)
{
	// If there's no positional information, simply append
	if (sector.offset == 0)
	{
		m_sectors.emplace_back(std::move(sector));
		return AddResult::Append;
	}
	else
	{
		// Find a sector close enough to the new offset to be the same one
		auto it = std::find_if(begin(), end(), [&] (const Sector &s) {
			auto offset_min = std::min(sector.offset, s.offset);
			auto offset_max = std::max(sector.offset, s.offset);
			auto distance = std::min(offset_max - offset_min, tracklen + offset_min - offset_max);

			// Compare bitstream distance, which isn't affected by motor speed but may be thrown off by PLL sync
			if (distance <= COMPARE_TOLERANCE_BITS)
			{
				assert(sector.header == s.header);
				return true;
			}

			return false;
		});

		// If that failed, we have a new sector with an offset
		if (it == end())
		{
			// Find the insertion point to keep the sectors in order
			it = std::find_if(begin(), end(), [&] (const Sector &s) {
				return sector.offset < s.offset;
			});
			m_sectors.emplace(it, std::move(sector));
			return AddResult::Insert;
		}
		else
		{
			// Merge details with the existing sector
			it->merge(std::move(sector));
			return AddResult::Merge;
		}
	}
}

Track &Track::format (const CylHead &cylhead, const Format &fmt)
{
	assert(fmt.sectors != 0);

	m_sectors.clear();
	m_sectors.reserve(fmt.sectors);

	for (auto id : fmt.get_ids(cylhead))
	{
		Header header(cylhead.cyl, cylhead.head ? fmt.head1 : fmt.head0, id, fmt.size);
		Sector sector(fmt.datarate, fmt.encoding, header, fmt.gap3);
		Data data(fmt.sector_size(), fmt.fill);

		sector.add(std::move(data));
		add(std::move(sector));
	}

	return *this;
}

Data::const_iterator Track::populate (Data::const_iterator it, Data::const_iterator itEnd)
{
	assert(std::distance(it, itEnd) >= 0);

	// Populate in sector number order, which requires sorting the track
	std::vector<Sector *> ptrs(m_sectors.size());
	std::transform(m_sectors.begin(), m_sectors.end(), ptrs.begin(), [] (Sector &s) { return &s; });
	std::sort(ptrs.begin(), ptrs.end(), [] (Sector *a, Sector *b) { return a->header.sector < b->header.sector; });

	for (auto sector : ptrs)
	{
		assert(sector->copies() == 1);
		auto bytes = std::min(sector->size(), static_cast<int>(std::distance(it, itEnd)));
		std::copy_n(it, bytes, sector->data_copy(0).begin());
		it += bytes;
	}

	return it;
}

Sector Track::remove (int index)
{
	assert(index < static_cast<int>(m_sectors.size()));

	auto it = m_sectors.begin() + index;
	auto sector = std::move(*it);
	m_sectors.erase(it);
	return sector;
}

std::vector<Sector>::iterator Track::find (const Sector &sector)
{
	return std::find_if(begin(), end(), [&] (const Sector &s) {
		return &s == &sector;
	});
}

std::vector<Sector>::iterator Track::find (const Header &header)
{
	return std::find_if(begin(), end(), [&] (const Sector &s) {
		return header.compare(s.header);
	});
}

std::vector<Sector>::const_iterator Track::find (const Sector &sector) const
{
	return std::find_if(begin(), end(), [&] (const Sector &s) {
		return &s == &sector;
	});
}

std::vector<Sector>::const_iterator Track::find (const Header &header) const
{
	return std::find_if(begin(), end(), [&] (const Sector &s) {
		return header.compare(s.header);
	});
}

const Sector &Track::get_sector (const Header &header) const
{
	auto it = find(header);
	if (it == end() || it->data_size() < header.sector_size())
		throw util::exception(CylHead(header.cyl, header.head), " sector ", header.sector, " not found");

	return *it;
}

//////////////////////////////////////////////////////////////////////////////

Disk::Disk (Format &fmt_)
	: fmt(fmt_)
{
	format(fmt);
}

Range Disk::range () const
{
	return Range(cyls(), heads());
}

int Disk::cyls () const
{
	return m_tracks.empty() ? 0 : (m_tracks.rbegin()->first.cyl + 1);
}

int Disk::heads () const
{
	if (m_tracks.empty())
		return 0;

	auto it = std::find_if(m_tracks.begin(), m_tracks.end(), [] (const std::pair<const CylHead, const Track> &p) {
		return p.first.head != 0;
	});

	return (it != m_tracks.end()) ? 2 : 1;
}


void Disk::unload (bool source_only)
{
	if (!source_only)
		m_tracks.clear();

	m_bitstreamdata.clear();
	m_fluxdata.clear();
}


bool Disk::get_bitstream_source (const CylHead &cylhead, BitBuffer* &p)
{
	auto it = m_bitstreamdata.find(cylhead);
	if (it != m_bitstreamdata.end())
	{
		p = &it->second;
		return true;
	}

	return false;
}

bool Disk::get_flux_source (const CylHead &cylhead, const std::vector<std::vector<uint32_t>>* &p)
{
	auto it = m_fluxdata.find(cylhead);
	if (it != m_fluxdata.end())
	{
		p = &it->second;
		return true;
	}

	return false;
}

void Disk::set_source (const CylHead &cylhead, BitBuffer &&bitbuf)
{
	m_bitstreamdata.emplace(std::make_pair(cylhead, std::move(bitbuf)));
	m_tracks[cylhead]; // extend
}

void Disk::set_source (const CylHead &cylhead, std::vector<std::vector<uint32_t>> &&data)
{
	m_fluxdata[cylhead] = std::move(data);
	m_tracks[cylhead]; // extend
}


const Track &Disk::read_track (const CylHead &cylhead)
{
	return m_tracks[cylhead];
}

Track &Disk::write_track (const CylHead &cylhead, const Track &track)
{
	// Move a temporary copy of the const source track
	return write_track(cylhead, Track(track));
}

Track &Disk::write_track (const CylHead &cylhead, Track &&track)
{
	// Invalidate stored format, since we can no longer guarantee a match
	fmt.sectors = 0;

	// Move supplied track into disk
	return m_tracks[cylhead] = std::move(track);
}

void Disk::each (const std::function<void (const CylHead &cylhead, const Track &track)> &func, bool cyls_first)
{
	if (!m_tracks.empty())
	{
		range().each([&] (const CylHead &cylhead) {
			func(cylhead, read_track(cylhead));
		}, cyls_first);
	}
}

void Disk::format (const RegularFormat &reg_fmt, const Data &data, bool cyls_first)
{
	format(Format(reg_fmt), data, cyls_first);
}

void Disk::format (const Format &new_fmt, const Data &data, bool cyls_first)
{
	auto it = data.begin(), itEnd = data.end();

	new_fmt.range().each([&] (const CylHead &cylhead) {
		Track track;
		track.format(cylhead, new_fmt);
		it = track.populate(it, itEnd);
		write_track(cylhead, std::move(track));
	}, cyls_first);

	// Assign format after formatting as it's cleared by formatting
	fmt = new_fmt;
}

void Disk::flip_sides ()
{
	decltype(m_tracks) tracks;

	for (auto pair : m_tracks)
	{
		CylHead cylhead = pair.first;
		cylhead.head ^= 1;

		// Move tracks to the new head position
		tracks[cylhead] = std::move(pair.second);
	}

	// Finally, swap the gutted container with the new one
	std::swap(tracks, m_tracks);
}

void Disk::resize (int new_cyls, int new_heads)
{
	if (!new_cyls && !new_heads)
	{
		m_tracks.clear();
		return;
	}

	// Remove tracks beyond the new extent
	for (auto it = m_tracks.begin(); it != m_tracks.end(); )
	{
		if (it->first.cyl >= new_cyls || it->first.head >= new_heads)
			it = m_tracks.erase(it);
		else
			++it;
	}

	// If the disk is too small, insert a blank track to extend it
	if (cyls() < new_cyls || heads() < new_heads)
		m_tracks[CylHead(new_cyls - 1, new_heads - 1)];
}

const Sector &Disk::get_sector (const Header &header)
{
	return read_track(header).get_sector(header);
}

bool Disk::find (const Header &header, const Sector *&found_sector)
{
	auto &track = read_track(header);
	auto it = track.find(header);
	if (it != track.end())
	{
		found_sector = &*it;
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////////

Range::Range (int num_cyls, int num_heads)
	: Range(0, num_cyls, 0, num_heads)
{
}

Range::Range (int cyl_begin_, int cyl_end_, int head_begin_, int head_end_)
	: cyl_begin(cyl_begin_), cyl_end(cyl_end_), head_begin(head_begin_), head_end(head_end_)
{
	assert(cyl_begin >= 0 && cyl_begin <= cyl_end);
	assert(head_begin >= 0 && head_begin <= head_end);
}

bool Range::empty () const
{
	return cyls() <= 0 || heads() <= 0;
}

int Range::cyls () const
{
	return cyl_end - cyl_begin;
}

int Range::heads () const
{
	return head_end - head_begin;
}

bool Range::contains (const CylHead &cylhead)
{
	return cylhead.cyl >= cyl_begin  && cylhead.cyl < cyl_end &&
		cylhead.head >= head_begin && cylhead.head < head_end;
}

void Range::each (const std::function<void (const CylHead &cylhead)> &func, bool cyls_first/*=false*/) const
{
	if (cyls_first && heads() > 1)
	{
		for (auto head = head_begin; head < head_end; ++head)
			for (auto cyl = cyl_begin; cyl < cyl_end; ++cyl)
				func(CylHead(cyl, head));
	}
	else
	{
		for (auto cyl = cyl_begin; cyl < cyl_end; ++cyl)
			for (auto head = head_begin; head < head_end; ++head)
				func(CylHead(cyl, head));
	}
}

//////////////////////////////////////////////////////////////////////////////

Format::Format (RegularFormat reg_fmt)
	: Format(Format::get(reg_fmt))
{
}

int Format::sector_size() const
{
	return Sector::SizeCodeToLength(size);
}

int Format::track_size() const
{
	assert(sectors && size <= 7);
	return sector_size() * sectors;
}

int Format::side_size() const
{
	assert(cyls);
	return track_size() * cyls;
}

int Format::disk_size() const
{
	assert(heads);
	return side_size() * heads;
}

int Format::total_sectors() const
{
	assert(cyls && heads && sectors);
	return cyls * heads * sectors;
}

Range Format::range () const
{
	return Range(cyls, heads);
}

std::vector<int> Format::get_ids (const CylHead &cylhead) const
{
	std::vector<bool> used(sectors);
	std::vector<int> ids(sectors);

	auto base_id = base;

	for (auto s = 0; s < sectors; ++s)
	{
		// Calculate the expected sector index using the interleave and skew
		auto index = (offset + s*interleave + skew*(cylhead.cyl)) % sectors;

		// Find a free slot starting from the expected position
		for (; used[index]; index = (index + 1) % sectors);
		used[index] = 1;

		// Assign the sector number, with offset adjustments
		ids[index] = base_id + s;
	}

	return ids;
}

/*static*/ Format Format::get (RegularFormat reg_fmt)
{
	Format fmt;

	switch (reg_fmt)
	{
		case RegularFormat::MGT:	// 800K
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 10;
			fmt.skew = 1;
			fmt.gap3 = 24;
			break;

		case RegularFormat::ProDos:	// 720K
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 9;
			fmt.interleave = 2;
			fmt.skew = 2;
			fmt.gap3 = 0x50;
			fmt.fill = 0xe5;
			break;

		case RegularFormat::PC720:	 // 720K
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 9;
			fmt.interleave = 1;
			fmt.skew = 1;
			fmt.gap3 = 0x50;
			fmt.fill = 0xe5;
			break;

		case RegularFormat::PC1440:	// 1.44M
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 18;
			fmt.interleave = 1;
			fmt.skew = 1;
			fmt.gap3 = 0x65;
			fmt.fill = 0xe5;
			break;

		case RegularFormat::PC2880:	// 2.88M
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 36;
			fmt.interleave = 1;
			fmt.skew = 1;
			fmt.gap3 = 0x53;
			fmt.fill = 0xe5;
			break;

		case RegularFormat::D80:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 9;
			fmt.skew = 5;
			fmt.fill = 0xe5;
			break;

		case RegularFormat::OPD:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 18;
			fmt.size = 1;
			fmt.fill = 0xe5;
			fmt.base = 0;
			fmt.offset = 17;
			fmt.interleave = 13;
			fmt.skew = 13;
			break;

		case RegularFormat::MBD820:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.cyls = 82;
			fmt.sectors = 5;
			fmt.size = 3;
			fmt.skew = 1;
			fmt.gap3 = 44;
			break;

		case RegularFormat::MBD1804:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_500K;
			fmt.encoding = Encoding::MFM;
			fmt.cyls = 82;
			fmt.sectors = 11;
			fmt.size = 3;
			fmt.skew = 1;
			break;

		case RegularFormat::TRDOS:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.cyls = 80;
			fmt.heads = 2;
			fmt.sectors = 16;
			fmt.size = 1;
			fmt.interleave = 2;
			fmt.head1 = 0;
			break;

		case RegularFormat::D2M:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_500K;
			fmt.encoding = Encoding::MFM;
			fmt.cyls = 81;
			fmt.sectors = 10;
			fmt.size = 3;
			fmt.fill = 0xe5;
			fmt.gap3 = 0x64;
			fmt.head0 = 1;
			fmt.head1 = 0;
			break;

		case RegularFormat::D4M:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_1M;
			fmt.encoding = Encoding::MFM;
			fmt.cyls = 81;
			fmt.sectors = 20;
			fmt.size = 3;
			fmt.fill = 0xe5;
			fmt.gap3 = 0x64;
			fmt.head0 = 1;
			fmt.head1 = 0;
			break;

		case RegularFormat::D81:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 10;
			fmt.gap3 = 0x26;
			fmt.head0 = 1;
			fmt.head1 = 0;
			break;

		case RegularFormat::_2D:
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.cyls = 40;
			fmt.sectors = 16;
			fmt.size = 1;
			break;

		case RegularFormat::AmigaDOS:
			fmt.fdc = FdcType::Amiga;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::Amiga;
			fmt.cyls = 80;
			fmt.sectors = 11;
			fmt.size = 2;
			fmt.base = 0;
			break;

		case RegularFormat::AmigaDOSHD:
			fmt.fdc = FdcType::Amiga;
			fmt.datarate = DataRate::_500K;
			fmt.encoding = Encoding::Amiga;
			fmt.sectors = 22;
			fmt.size = 2;
			fmt.base = 0;
			break;

		case RegularFormat::LIF:
			fmt.cyls = 77;
			fmt.heads = 2;
			fmt.fdc = FdcType::PC;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 16;
			fmt.size = 1;
			break;

		case RegularFormat::AtariST:
			fmt.fdc = FdcType::WD;
			fmt.datarate = DataRate::_250K;
			fmt.encoding = Encoding::MFM;
			fmt.sectors = 9;
			fmt.gap3 = 40;
			fmt.fill = 0x00;
			break;
	}

	return fmt;
}
