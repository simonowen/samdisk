#include "SAMdisk.h"
#include "Track.h"

#include "IBMPC.h"


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

int Track::data_extent_bits (const Sector &sector) const
{
	auto it = find(sector);
	assert(it != end());

	auto drive_speed = (sector.datarate == DataRate::_300K) ? RPM_TIME_360 : RPM_TIME_300;
	auto track_len = tracklen ? tracklen : GetTrackCapacity(drive_speed, sector.datarate, sector.encoding);

	// Approximate bit distance to next ID header.
	auto gap_bits = ((std::next(it) != end()) ? std::next(it)->offset : (track_len + begin()->offset)) - sector.offset;
	return gap_bits;
}

int Track::data_extent_bytes (const Sector &sector) const
{
	// We only support real data extent for MFM and FM sectors.
	if (sector.encoding != Encoding::MFM && sector.encoding != Encoding::FM)
		return sector.size();

	auto encoding_shift = (sector.encoding == Encoding::FM) ? 5 : 4;
	auto gap_bytes = data_extent_bits(sector) >> encoding_shift;
	auto overhead_bytes = GetSectorOverhead(sector.encoding) - GetSyncOverhead(sector.encoding);
	auto extent_bytes = (gap_bytes > overhead_bytes) ? gap_bytes - overhead_bytes : 0;
	return extent_bytes;
}

bool Track::data_overlap (const Sector &sector) const
{
	if (!sector.offset)
		return false;

	return data_extent_bytes(sector) < sector.size();
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
	auto count = 0;

	for (const auto &s : m_sectors)
	{
		// Check for data rate, encoding, and CHRN match
		if (s.datarate == sector.datarate &&
			s.encoding == sector.encoding &&
			s.header == sector.header)
		{
			// Stop if we see more than one match.
			if (++count > 1)
				return true;
		}
	}

	return false;
}

bool Track::has_data_error () const
{
	auto it = std::find_if(begin(), end(), [] (const Sector &sector) {
		if (sector.is_8k_sector() && sector.has_data())
		{
			const Data &data = sector.data_copy();
			if (!ChecksumMethods(data.data(), data.size()).empty())
				return false;
		}
		return !sector.has_data() || sector.has_baddatacrc();
	});

	return it != end();
}

void Track::clear ()
{
	*this = Track();
}

void Track::add (Track &&track)
{
	// Ignore if no sectors to add
	if (!track.sectors().size())
		return;

	// Use longest track length and time
	tracklen = std::max(tracklen, track.tracklen);
	tracktime = std::max(tracktime, track.tracktime);

	// Merge supplied sectors into existing track
	for (auto &s : track.sectors())
	{
		assert(s.offset != 0);
		add(std::move(s));
	}
}

Track::AddResult Track::add (Sector &&sector)
{
	// Check the new datarate against any existing sector.
	if (!m_sectors.empty() && m_sectors[0].datarate != sector.datarate)
		throw util::exception("can't mix datarates on a track");

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

			// Sector must be close enough and have the same header
			if (distance <= COMPARE_TOLERANCE_BITS && sector.header == s.header)
				return true;

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
			auto ret = it->merge(std::move(sector));
			if (ret == Sector::Merge::Unchanged)
				return AddResult::Unchanged;

			// Limit the number of data copies kept for each sector.
			if (data_overlap(*it) && !is_8k_sector())
				it->limit_copies(1);

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

void Track::insert(int index, Sector &&sector)
{
	assert(index <= static_cast<int>(m_sectors.size()));

	if (!m_sectors.empty() && m_sectors[0].datarate != sector.datarate)
		throw util::exception("can't mix datarates on a track");

	auto it = m_sectors.begin() + index;
	m_sectors.insert(it, std::move(sector));
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
		return header == s.header;
	});
}

std::vector<Sector>::iterator Track::find(const Header &header, const DataRate datarate, const Encoding encoding)
{
	return std::find_if(begin(), end(), [&](const Sector &s) {
		return header == s.header && datarate == s.datarate && encoding == s.encoding;
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
		return header == s.header;
	});
}

std::vector<Sector>::const_iterator Track::find(const Header &header, const DataRate datarate, const Encoding encoding) const
{
	return std::find_if(begin(), end(), [&](const Sector &s) {
		return header == s.header && datarate == s.datarate && encoding == s.encoding;
	});
}

const Sector &Track::get_sector (const Header &header) const
{
	auto it = find(header);
	if (it == end() || it->data_size() < header.sector_size())
		throw util::exception(CylHead(header.cyl, header.head), " sector ", header.sector, " not found");

	return *it;
}
