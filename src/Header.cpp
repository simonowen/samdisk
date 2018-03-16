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
		case Encoding::RX02:	return "RX02";			break;
		case Encoding::Amiga:	return "Amiga";			break;
		case Encoding::GCR:		return "GCR";			break;
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
		case Encoding::RX02:	return "rx";			break;
		case Encoding::Amiga:	return "ami";			break;
		case Encoding::GCR:		return "gcr";			break;
		case Encoding::Ace:		return "ace";			break;
		case Encoding::MX:		return "mx";			break;
		case Encoding::Agat:	return "agat";			break;
		case Encoding::Unknown:	break;
	}
	return "unk";
}


DataRate datarate_from_string(std::string str)
{
	str = util::lowercase(str);
	auto len = str.size();

	if (str == std::string("250kbps").substr(0, len)) return DataRate::_250K;
	if (str == std::string("300kbps").substr(0, len)) return DataRate::_300K;
	if (str == std::string("500kbps").substr(0, len)) return DataRate::_500K;
	if (str == std::string("1mbps").substr(0, len)) return DataRate::_1M;
	return DataRate::Unknown;
}

Encoding encoding_from_string (std::string str)
{
	str = util::lowercase(str);

	if (str == "mfm") return Encoding::MFM;
	if (str == "fm") return Encoding::FM;
	if (str == "gcr") return Encoding::GCR;
	if (str == "amiga") return Encoding::Amiga;
	if (str == "ace") return Encoding::Ace;
	if (str == "mx") return Encoding::MX;
	if (str == "agat") return Encoding::Agat;
	return Encoding::Unknown;
}

//////////////////////////////////////////////////////////////////////////////

CylHead::operator int() const
{
	return (cyl * MAX_DISK_HEADS) + head;
}

CylHead operator * (const CylHead &cylhead, int cyl_step)
{
	return CylHead(cylhead.cyl * cyl_step, cylhead.head);
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
	return compare_crn(rhs);	// ToDo: use compare_chrn?
}

bool Header::operator!= (const Header &rhs) const
{
	return !compare_crn(rhs);
}

Header::operator CylHead() const
{
	return CylHead(cyl, head);
}

bool Header::compare_chrn (const Header &rhs) const
{
	return cyl == rhs.cyl &&
		head == rhs.head &&
		sector == rhs.sector &&
		size == rhs.size;
}

bool Header::compare_crn (const Header &rhs) const
{
	// Compare without head match, like WD17xx
	return cyl == rhs.cyl &&
		sector == rhs.sector &&
		size == rhs.size;
}

int Header::sector_size () const
{
	return Sector::SizeCodeToLength(size);
}
