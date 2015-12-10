// SuperCard Pro flux file format:
//  http://www.cbmstuff.com/downloads/scp/scp_image_specs.txt

#include "SAMdisk.h"
#include "DemandDisk.h"
#include "BitstreamDecoder.h"

enum
{
	FLAG_INDEX = 1,
	FLAG_TPI = 2,
	FLAG_RPM = 4,
	FLAG_TYPE = 8,
	FLAG_MODE = 16
};

typedef struct
{
	char signature[3];		// SCP
	uint8_t revision;		// (version << 4) | revision, 0x39 = v3.9
	uint8_t disk_type;
	uint8_t revolutions;	// number of stored revolutions
	uint8_t start_track;	// 0-165
	uint8_t end_track;
	uint8_t flags;
	uint8_t bitcell_width;	// 0 = 16 bits, non-zero = number of bits
	uint8_t heads;			// 0 = both header, 1 = head 0 only, 2 = head 1 only
	uint8_t reserved;		// should be zero?
	uint8_t checksum[4];	// 32-bit checksum from after header to EOF (unless SCP_FLAG_MODE is set)
} SCP_FILE_HEADER;

typedef struct
{
	char signature[3];		// TRK
	uint8_t tracknr;
} TRACK_DATA_HEADER;


class SCPDisk : public DemandDisk
{
public:
	void add_track_data (const CylHead &cylhead, std::vector<std::vector<uint32_t>> &&data)
	{
		m_trackdata[cylhead] = std::move(data);
		extend(cylhead);
	}

protected:
	Track load (const CylHead &cylhead) override
	{
		auto ch = CylHead(cylhead.cyl * opt.step, cylhead.head);
		auto it = m_trackdata.find(ch);
		if (it != m_trackdata.end())
			return scan_flux(ch, it->second);

		// No data to decode!
		return Track();
	}

private:
	std::map<CylHead, std::vector<std::vector<uint32_t>>> m_trackdata {};
};


bool ReadSCP (MemFile &file, std::shared_ptr<Disk> &disk)
{
	SCP_FILE_HEADER fh {};

	if (!file.rewind() || !file.read(&fh, sizeof(fh)) || std::string(fh.signature, 3) != "SCP")
		return false;

	/*if (!(fh.flags & FLAG_INDEX))
		throw util::exception("not an index-synchronised image");
	else*/ if (fh.revolutions == 0 || fh.revolutions > 10)
		throw util::exception("invalid revolution count (", fh.revolutions, ")");
	else if (fh.bitcell_width != 0 && fh.bitcell_width != 16)
		throw util::exception("unsupported bit cell width (", fh.bitcell_width, ")");
	else if (fh.start_track > fh.end_track)
		throw util::exception("start track (", fh.start_track, ") > end track (", fh.end_track, ")");

	std::vector<uint32_t> tdh_offsets(83 * 2);	// ToDo: use constants
	if (!file.read(tdh_offsets))
		throw util::exception("short file reading track offset index");

	for (auto &offset : tdh_offsets)
		offset = util::letoh<uint32_t>(offset);

	auto scp_disk = std::make_shared<SCPDisk>();

	for (auto tracknr = fh.start_track; tracknr <= fh.end_track; ++tracknr)
	{
		TRACK_DATA_HEADER tdh;
		auto cyl = tracknr >> 1;
		auto head = tracknr & 1;
		CylHead cylhead(cyl, head);

		if (!file.seek(tdh_offsets[tracknr]) || !file.read(&tdh, sizeof(tdh)))
			throw util::exception("short file reading ", cylhead, " track header");
		else if (std::string(tdh.signature, 3) != "TRK")
			throw util::exception("invalid track signature on ", cylhead);
		else if (tdh.tracknr != tracknr)
			throw util::exception("track number mismatch (", tdh.tracknr, " != ", tracknr, ") in ", cylhead, " header");

		std::vector<std::vector<uint32_t>> flux_revs;
		std::vector<uint32_t> rev_index(fh.revolutions * 3);

		if (!file.read(rev_index))
			throw util::exception("short file reading ", cylhead, " track index");

		for (uint8_t rev = 0; rev < fh.revolutions; ++rev)
		{
//			auto index_time  = util::letoh<uint32_t>(rev_index[rev*3 + 0]);
			auto flux_count = util::letoh<uint32_t>(rev_index[rev * 3 + 1]);
			auto data_offset = util::letoh<uint32_t>(rev_index[rev * 3 + 2]);

			std::vector<uint16_t> flux_data(flux_count);	// NB: time values are big-endian
			std::vector<uint32_t> flux_times;
			flux_times.reserve(flux_count);

			if (!file.seek(tdh_offsets[tracknr] + data_offset) || !file.read(flux_data))
				throw util::exception("short error reading ", cylhead, " data");

			uint32_t total_time = 0;
			for (auto time : flux_data)
			{
				if (!time)
					total_time += 0x10000;
				else
				{
					total_time += util::betoh<uint16_t>(time);
					flux_times.push_back(total_time * 25);	// 25ns sampling time
					total_time = 0;
				}
			}

			flux_revs.push_back(std::move(flux_times));
		}

		scp_disk->add_track_data(CylHead(cyl, head), std::move(flux_revs));
	}

	scp_disk->strType = "SCP";
	disk = scp_disk;

	return true;
}
