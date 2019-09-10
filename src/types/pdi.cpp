// PDI - RLE compressed DOS disk images.

#include "SAMdisk.h"
#include "types.h"

struct PDI_HEADER
{
	char sig[9];		// "PDITYPEx\0"
	char idata[5];		// "IDATA"
	uint8_t unknown[4];	// checksum perhaps?
	uint8_t unknown2;	// zero
	char maindata[8];	// "MAINDATA"
};

bool ReadPDI (MemFile &file, std::shared_ptr<Disk> &disk)
{
	PDI_HEADER ph{};
	if (!file.rewind() || !file.read(&ph, sizeof(ph)))
		return false;
	else if (std::string(ph.sig, 7) != "PDITYPE")
		return false;

	if (ph.sig[7] != '1')
		throw util::exception("PDI version ", ph.sig[7], " files are not supported");
	else if (std::string(ph.maindata, 8) != "MAINDATA")
		throw util::exception("missing main data header");

	Data data;
	data.reserve(Format(RegularFormat::PC1440).disk_size());

	int last_b = -1, last_last_b = -1;
	uint8_t b;

	while (file.read(b))
	{
		data.push_back(b);

		if (b == last_b && last_b == last_last_b)
		{
			uint8_t count;
			if (!file.read(count))
				throw util::exception("EOF in byte repeat count");

			for (int i = 0 ; i < count; ++i)
				data.push_back(b);

			last_b = last_last_b = -1;
			continue;
		}

		last_last_b = last_b;
		last_b = b;
	}

	Data end_marker{ 0, 'E', 'N', 'D', 0 };
	if (data.size() >= end_marker.size() && Data(data.end() - end_marker.size(), data.end()) == end_marker)
		data.resize(data.size() - end_marker.size());

	Format fmt{};
	if (!Format::FromSize(data.size(), fmt))
		throw util::exception("unrecognised uncompressed size (", data.size(), ")");

	disk->format(fmt, data);
	disk->strType = "PDI";

	return true;
}
