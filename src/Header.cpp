#include "SAMdisk.h"
#include "Header.h"

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
		case Encoding::MX:		return "MX";			break;
		case Encoding::Agat:	return "Agat";			break;
		case Encoding::Unknown:	break;
	}
	return "Unknown";
}

std::string short_name (const Encoding &encoding)
{
	switch (encoding)
	{
		case Encoding::MFM:		return "mfm";			break;
		case Encoding::FM:		return "fm";			break;
		case Encoding::GCR:		return "gcr";			break;
		case Encoding::Amiga:	return "ami";			break;
		case Encoding::Ace:		return "ace";			break;
		case Encoding::MX:		return "mx";			break;
		case Encoding::Agat:	return "agat";			break;
		case Encoding::Unknown:	break;
	}
	return "unk";
}

//////////////////////////////////////////////////////////////////////////////

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
