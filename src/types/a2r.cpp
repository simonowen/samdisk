// A2R - Apple II file format:
//  https://applesaucefdc.com/a2r/

#include "SAMdisk.h"
#include "DemandDisk.h"

#define A2R_SIGNATURE	"A2R2"

typedef struct
{
	char sig[4];			// A2R2
	uint8_t ff;				// 0xff (8-bit test)
	uint8_t lfcrlf[3];		// \n\r\n (text conversion test)
} A2R_HEADER;

typedef struct
{
	uint8_t type[4];		// chunk id
	uint8_t size[4];		// size of data following
} A2R_CHUNK;

typedef struct
{
	uint8_t version;		// currently 1
	char creator[32];		// Creator application
	uint8_t disk_type;		// 1=5.25", 2=3.5"
	uint8_t write_protect;	// 1=write-protected
	uint8_t synchronised;	// 1=cross-track synchronised
} INFO_CHUNK;

typedef struct
{
	uint8_t location;		// track/side
	uint8_t capture_type;	// 1=timing, 2=bits, 3=xtiming
	uint8_t data_length[4];	// captured data length
	uint8_t loop_point[4];	// duration until sync sensor was triggered at loop point
} STRM_CHUNK;

constexpr uint32_t id_value (const char *str)
{
	return (static_cast<uint32_t>(str[0]) << 24) |
		(static_cast<uint32_t>(str[1]) << 16) |
		(static_cast<uint32_t>(str[2]) << 8) |
		str[3];
}

//

class A2RDisk final : public DemandDisk
{
public:
	void add_track_data (const CylHead &cylhead, std::vector<uint8_t> &&trackdata, uint32_t loop_point)
	{
		m_data[cylhead] = std::move(trackdata);
		m_loop_point[cylhead] = std::move(loop_point);
		extend(cylhead);
	}

	bool has_track_data (const CylHead &cylhead)
	{
		return !m_data[cylhead].empty();
	}

protected:
	TrackData load (const CylHead &cylhead, bool /*first_read*/) override
	{
		const auto &data = m_data[cylhead];
		if (data.empty())
			return TrackData(cylhead);

		FluxData flux_revs;

		std::vector<uint32_t> flux_times;
		flux_times.reserve(data.size());

		uint32_t total_time = 0, ticks = 0;
		for (auto time : data)
		{
			total_time += time;
			if (time < 255)
			{
				flux_times.push_back(total_time * 125);	// 125ns sampling time
				ticks += total_time;
				total_time = 0;

				if (ticks >= m_loop_point[cylhead])
				{
					flux_revs.push_back(std::move(flux_times));
					flux_times.clear();
					ticks -= m_loop_point[cylhead];
				}
			}
		}

		flux_revs.push_back(std::move(flux_times));

		m_data[cylhead].clear();

		return TrackData(cylhead, std::move(flux_revs));
	}

private:
	std::map<CylHead, std::vector<uint8_t>> m_data {};
	std::map<CylHead, uint32_t> m_loop_point {};
};


bool ReadA2R (MemFile &file, std::shared_ptr<Disk> &disk)
{
	A2R_HEADER wh;
	if (!file.rewind() || !file.read(&wh, sizeof(wh)) || memcmp(&wh.sig, A2R_SIGNATURE, sizeof(wh.sig)))
		return false;
	else if (wh.ff != 0xff || memcmp(&wh.lfcrlf, "\n\r\n", 3))
		return false;

	INFO_CHUNK info{};
	A2R_CHUNK wc{};
	auto a2r_disk = std::make_shared<A2RDisk>();

	while (file.read(&wc, sizeof(wc)))
	{
		auto chunk_id = util::be_value(wc.type);	// big-endian for text
		auto chunk_size = util::le_value(wc.size);
		auto next_pos = file.tell() + chunk_size;

		switch (chunk_id)
		{
		case id_value("INFO"):
		{
			if (!file.read(&info, sizeof(info)))
				throw util::exception("short file reading info");

			a2r_disk->metadata["disk_type"] = (info.disk_type == 1) ? "5.25\"" :
				(info.disk_type == 2) ? "3.5\"" : std::to_string(info.disk_type);
			a2r_disk->metadata["read_only"] = info.write_protect ? "yes" : "no";
			a2r_disk->metadata["synchronised"] = info.synchronised ? "yes" : "no";
			a2r_disk->metadata["creator"] =
				util::trim(std::string(info.creator, sizeof(info.creator)));
			break;
		}

		case id_value("STRM"):
		{
			std::vector<uint8_t> strm(chunk_size);
			if (!file.read(strm))
				throw util::exception("short file reading strm chunk");

			STRM_CHUNK sc{};
			for (size_t pos = 0; pos < strm.size() && strm[pos] != 0xff;)
			{
				memcpy(&sc, &strm[pos], sizeof(sc));
				auto data_length = util::le_value(sc.data_length);
				if (opt.debug) util::cout << util::fmt ("pos %u: loc %d, type %d, len %u, loop %u\n", 
					pos, sc.location, sc.capture_type, data_length, util::le_value(sc.loop_point));

				if ((sc.location & 3) == 0 && sc.capture_type != 2)
				{
					CylHead cylhead(sc.location >> 2, 0);
					if (!a2r_disk->has_track_data(cylhead))
					{
						a2r_disk->add_track_data(cylhead,
							{ strm.begin() + pos, strm.begin() + pos + data_length },
							util::le_value(sc.loop_point));
					}
				}

				pos += data_length + sizeof(sc);
			}

			break;
		}

		case id_value("META"):
		{
			std::vector<char> meta(chunk_size);
			if (!file.read(meta))
				throw util::exception("short file reading meta chunk");

			auto str_meta = std::string(meta.data(), meta.size());
			for (auto &row : util::split(str_meta, '\n'))
			{
				auto columns = util::split(row, '\t');
				if (columns.size() == 2)
					a2r_disk->metadata[columns[0]] = columns[1];
			}
			break;
		}

		default:
			Message(msgWarning, "unknown A2R chunk: %.*s [%02X %02X %02X %02X] with size %u",
				sizeof(wc.type), reinterpret_cast<const char *>(wc.type),
				wc.type[0], wc.type[1], wc.type[2], wc.type[3], chunk_size);

			file.seek(file.tell() + chunk_size);
			break;
		}

		file.seek(next_pos);
	}

	a2r_disk->strType = "A2R";
	disk = a2r_disk;

	return true;
}
