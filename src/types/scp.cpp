// SuperCard Pro flux file format:
//  http://www.cbmstuff.com/downloads/scp/scp_image_specs.txt

#include "SAMdisk.h"
#include "DemandDisk.h"

enum
{
	FLAG_INDEX = 1<<0,	// set for index-synchronised, clear if not
	FLAG_TPI = 1<<1,	// set for 96tpi, clear for 48tpi
	FLAG_RPM = 1<<2,	// set for 360rpm, clear for 300rpm
	FLAG_TYPE = 1<<3,	// set for normalised flux, clear for preservation quality
	FLAG_MODE = 1<<4,	// set for read/write image, clear for read-only
	FLAG_FOOTER = 1<<5	// set if footer block present, clear if absent
};

typedef struct
{
	char signature[3];		// SCP
	uint8_t revision;		// (version << 4) | revision, 0x39 = v3.9
	uint8_t disk_type;
	uint8_t revolutions;	// number of stored revolutions
	uint8_t start_track;
	uint8_t end_track;
	uint8_t flags;
	uint8_t bitcell_width;	// 0 = 16 bits, non-zero = number of bits
	uint8_t heads;			// 0 = both heads, 1 = head 0 only, 2 = head 1 only
	uint8_t reserved;		// should be zero?
	uint32_t checksum;		// 32-bit checksum from after header to EOF (unless FLAG_MODE is set)
} SCP_FILE_HEADER;

typedef struct
{
	uint32_t manufacturer_offset;
	uint32_t model_offset;
	uint32_t serial_offset;
	uint32_t creator_offset;
	uint32_t application_offset;
	uint32_t comments_offset;
	uint64_t creation_time;
	uint64_t modification_time;
	uint8_t application_version;
	uint8_t hardware_version;
	uint8_t firmware_version;
	uint8_t format_revision;
	char sig[4];
} SCP_FILE_FOOTER;

typedef struct
{
	char signature[3];		// TRK
	uint8_t tracknr;
} TRACK_DATA_HEADER;

std::string FooterString(MemFile &file, uint32_t offset)
{
	uint16_t len = 0;
	if (!offset || !file.seek(static_cast<int>(offset)) || !file.read(&len, sizeof(len)))
		return "";

	len = util::letoh(len);
	if (static_cast<int>(offset + len) >= file.size())
		return "";

	std::vector<char> str;
	str.resize(len);

	if (!file.read(str.data(), str.size()))
		return "";

	return std::string(str.data(), str.size());
}

std::string FooterVersion(uint32_t version)
{
	std::stringstream ss;
	if (version)
		ss << (version >> 4) << "." << (version & 0xf);
	return ss.str();
}

std::string FooterTime(uint64_t unix_time)
{
	auto t = static_cast<std::time_t>(unix_time);
	auto tm = *std::gmtime(&t);

	std::stringstream ss;
	if (unix_time)
		ss << std::put_time(&tm, "%Y-%m-%d %T");
	return ss.str();
}


class SCPDisk final : public DemandDisk
{
public:
	SCPDisk (bool normalised) : m_normalised(normalised) {}

	void add_track_data (const CylHead &cylhead, std::vector<std::vector<uint16_t>> &&trackdata)
	{
		m_data[cylhead] = std::move(trackdata);
		extend(cylhead);
	}

protected:
	TrackData load (const CylHead &cylhead, bool /*first_read*/) override
	{
		FluxData flux_revs;

		auto it = m_data.find(cylhead);
		if (it == m_data.end())
			return TrackData(cylhead);

		for (auto &rev_times : it->second)
		{
			std::vector<uint32_t> flux_times;
			flux_times.reserve(rev_times.size());

			uint32_t total_time = 0;
			for (auto time : rev_times)	// note: big endian times!
			{
				if (!time)
					total_time += 0x10000;
				else
				{
					total_time += util::betoh(time);
					flux_times.push_back(total_time * 25);	// 25ns sampling time
					total_time = 0;
				}
			}

			flux_revs.push_back(std::move(flux_times));
		}

		return TrackData(cylhead, std::move(flux_revs), m_normalised);
	}

private:
	std::map<CylHead, std::vector<std::vector<uint16_t>>> m_data {};
	bool m_normalised = false;
};

bool ReadSCP (MemFile &file, std::shared_ptr<Disk> &disk)
{
	SCP_FILE_HEADER fh {};

	if (!file.rewind() || !file.read(&fh, sizeof(fh)) || std::string(fh.signature, 3) != "SCP")
		return false;

	if (!(fh.flags & FLAG_MODE) && fh.checksum)
	{
		auto checksum = std::accumulate(file.data().begin() + 0x10, file.data().end(), uint32_t(0));
		if (checksum != util::letoh(fh.checksum))
			Message(msgWarning, "file checksum is incorrect!");
	}

	/*if (!(fh.flags & FLAG_INDEX))
		throw util::exception("not an index-synchronised image");
	else*/ if (fh.revolutions == 0 || fh.revolutions > 10)
		throw util::exception("invalid revolution count (", fh.revolutions, ")");
	else if (fh.bitcell_width != 0 && fh.bitcell_width != 16)
		throw util::exception("unsupported bit cell width (", fh.bitcell_width, ")");
	else if (fh.start_track > fh.end_track)
		throw util::exception("start track (", fh.start_track, ") > end track (", fh.end_track, ")");

	std::vector<uint32_t> tdh_offsets(fh.end_track + 1);
	if (!file.read(tdh_offsets))
		throw util::exception("short file reading track offset index");

	auto scp_disk = std::make_shared<SCPDisk>((fh.flags & FLAG_TYPE) != 0);

	for (auto tracknr = fh.start_track; tracknr <= fh.end_track; ++tracknr)
	{
		TRACK_DATA_HEADER tdh;
		auto cyl = fh.heads == 0 ? (tracknr >> 1) : tracknr;
		auto head = fh.heads == 0 ? (tracknr & 1) : (fh.heads - 1);
		CylHead cylhead(cyl, head);

		if (!tdh_offsets[tracknr])
			continue;

		if (!file.seek(tdh_offsets[tracknr]) || !file.read(&tdh, sizeof(tdh)))
			throw util::exception("short file reading ", cylhead, " track header");
		else if (std::string(tdh.signature, 3) != "TRK")
			throw util::exception("invalid track signature on ", cylhead);
		else if (tdh.tracknr != tracknr)
			throw util::exception("track number mismatch (", tdh.tracknr, " != ", tracknr, ") in ", cylhead, " header");

		std::vector<uint32_t> rev_index(fh.revolutions * 3);
		if (!file.read(rev_index))
			throw util::exception("short file reading ", cylhead, " track index");

		std::vector<std::vector<uint16_t>> revs_data;
		revs_data.reserve(fh.revolutions);

		for (uint8_t rev = 0; rev < fh.revolutions; ++rev)
		{
//			auto index_time  = util::letoh<uint32_t>(rev_index[rev * 3 + 0]);
			auto flux_count = util::letoh<uint32_t>(rev_index[rev * 3 + 1]);
			auto data_offset = util::letoh<uint32_t>(rev_index[rev * 3 + 2]);

			std::vector<uint16_t> flux_data(flux_count);
			if (!file.seek(tdh_offsets[tracknr] + data_offset) || !file.read(flux_data))
				throw util::exception("short error reading ", cylhead, " data");

			revs_data.push_back(std::move(flux_data));
		}

		scp_disk->add_track_data(cylhead, std::move(revs_data));
	}

	auto footer_offset = file.size() - static_cast<int>(sizeof(SCP_FILE_FOOTER));
	if ((fh.flags & FLAG_FOOTER) && footer_offset >= file.tell())
	{
		SCP_FILE_FOOTER ff{};

		if (file.seek(footer_offset) && file.read(&ff, sizeof(ff)) &&
			std::string(ff.sig, sizeof(ff.sig)) == "FPCS")
		{
			scp_disk->metadata["manufacturer"] = FooterString(file, ff.manufacturer_offset);
			scp_disk->metadata["model"] = FooterString(file, ff.model_offset);
			scp_disk->metadata["serial"] = FooterString(file, ff.serial_offset);
			scp_disk->metadata["creator"] = FooterString(file, ff.creator_offset);
			scp_disk->metadata["application"] = FooterString(file, ff.application_offset);
			scp_disk->metadata["comment"] = FooterString(file, ff.comments_offset);

			scp_disk->metadata["app_version"] = FooterVersion(ff.application_version);
			scp_disk->metadata["hw_version"] = FooterVersion(ff.hardware_version);
			scp_disk->metadata["fw_version"] = FooterVersion(ff.firmware_version);
			scp_disk->metadata["scp_version"] = FooterVersion(ff.format_revision);

			scp_disk->metadata["created"] = FooterTime(ff.creation_time);
			if (ff.modification_time != ff.creation_time)
				scp_disk->metadata["modified"] = FooterTime(ff.modification_time);
		}
	}
	else
	{
		scp_disk->metadata["app_version"] = FooterVersion(fh.revision);
		scp_disk->metadata["application"] = "SuperCard Pro software";

		std::stringstream ss;
		for (uint8_t b; file.read(&b, sizeof(b)) && std::isprint(b); )
			ss << static_cast<char>(b);
		scp_disk->metadata["created"] = ss.str();
	}

	scp_disk->metadata["index"] = (fh.flags & FLAG_INDEX) ? "synchronised" : "unsynchronised";
	scp_disk->metadata["tpi"] = (fh.flags & FLAG_TPI) ? "96 tpi" : "48 tpi";
	scp_disk->metadata["rpm"] = (fh.flags & FLAG_RPM) ? "360 rpm" : "300 rpm";
	scp_disk->metadata["quality"] = (fh.flags & FLAG_TYPE) ? "normalised" : "preservation";
	scp_disk->metadata["mode"] = (fh.flags & FLAG_MODE) ? "read/write" : "read-only";

	scp_disk->strType = "SCP";
	disk = scp_disk;

	return true;
}
