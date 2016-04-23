#include "SAMdisk.h"
#include "Header.h"

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
