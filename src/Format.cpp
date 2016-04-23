#include "SAMdisk.h"
#include "Format.h"

Format::Format (RegularFormat reg_fmt)
	: Format(GetFormat(reg_fmt))
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
