// WOZ - Apple II file format:
//  http://www.evolutioninteractive.com/applesauce/woz_reference.pdf

#include "SAMdisk.h"

#define WOZ_SIGNATURE	"WOZ1"

typedef struct
{
	char sig[4];			// WOZ1
	uint8_t ff;				// 0xff (8-bit test)
	uint8_t lfcrlf[3];		// \n\r\n (text conversion test)
	uint8_t crc32[4];		// CRC32 of remaining file dara
} WOZ_HEADER;

typedef struct
{
	uint8_t type[4];		// chunk id
	uint8_t size[4];		// size of data following
} WOZ_CHUNK;

typedef struct
{
	uint8_t version;		// currently 1
	uint8_t disk_type;		// 1=5.25", 2=3.5"
	uint8_t write_protect;	// 1=write-protected
	uint8_t synchronised;	// 1=cross-track synchronised
	uint8_t cleaned;		// 1=MC3470 fake bits removed
	char creator[32];		// Creator application
} INFO_CHUNK;

typedef struct
{
	std::array<uint8_t, 160> map;	// track map
} TMAP_CHUNK;

typedef struct
{
	uint8_t data[6646];
	uint8_t used_bytes[2];	// bytes used in bitstream
	uint8_t used_bits[2];	// bits in bitstream
	uint8_t splice_pos[2];	// index of bit after trace splice
	uint8_t splice_nibble;	// nibble value for splice (write hint)
	uint8_t splice_bits;	// bit count of splice nibble (write hint)
	uint8_t reserved[2];
} TRKS_CHUNK;

constexpr uint32_t id_value (const char *str)
{
	return (static_cast<uint32_t>(str[0]) << 24) |
		(static_cast<uint32_t>(str[1]) << 16) |
		(static_cast<uint32_t>(str[2]) << 8) |
		str[3];
}

static uint32_t crc32 (const uint8_t *buf, size_t len)
{
	uint32_t crc = ~0U;

	while (len-- > 0)
	{
		crc ^= *buf++;
		for (int j = 0 ; j < 8; ++j)
			crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
	}

	return ~crc;
}

bool ReadWOZ (MemFile &file, std::shared_ptr<Disk> &disk)
{
	WOZ_HEADER wh;
	if (!file.rewind() || !file.read(&wh, sizeof(wh)) || memcmp(&wh.sig, WOZ_SIGNATURE, sizeof(wh.sig)))
		return false;
	else if (wh.ff != 0xff || memcmp(&wh.lfcrlf, "\n\r\n", 3))
		return false;

	auto crc = util::le_value(wh.crc32);
	if (crc && crc32(file.ptr<uint8_t>(), file.size() - file.tell()) != crc)
		Message(msgWarning, "file checksum is incorrect!");

	INFO_CHUNK info{};
	TMAP_CHUNK tmap{};
	WOZ_CHUNK wc{};

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

			disk->metadata["disk_type"] = (info.disk_type == 1) ? "5.25\"" :
				(info.disk_type == 2) ? "3.5\"" : std::to_string(info.disk_type);
			disk->metadata["read_only"] = info.write_protect ? "yes" : "no";
			disk->metadata["synchronised"] = info.synchronised ? "yes" : "no";
			disk->metadata["cleaned"] = info.cleaned ? "yes" : "no";
			disk->metadata["creator"] =
				util::trim(std::string(info.creator, sizeof(info.creator)));
			break;
		}

		case id_value("TMAP"):
			if (!file.read(&tmap, sizeof(tmap)))
				throw util::exception("short file reading track map");
			break;

		case id_value("TRKS"):
		{
			if (chunk_size % sizeof(TRKS_CHUNK))
				throw util::exception("TRKS chunk size is mis-aligned");

			auto tracks = chunk_size / sizeof(TRKS_CHUNK);
			std::vector<TRKS_CHUNK> trks(tracks);

			for (size_t i = 0; i < tracks; ++i)
			{
				if (!file.read(&trks[i], sizeof(trks[i])))
					throw util::exception("short file reading track data");
				else if (util::le_value(trks[i].used_bytes) !=
						(util::le_value(trks[i].used_bits) + 7) / 8)
					throw util::exception("bit/byte counts inconsistent on track ", i);

				util::bit_reverse(trks[i].data, util::le_value(trks[i].used_bytes));
			}

			int cyl_step = (info.disk_type == 1) ? 4 : 1;
			for (size_t i = 0; i < tmap.map.size(); i += cyl_step)
			{
				if (tmap.map[i] < trks.size())
				{
					auto &trk = trks[tmap.map[i]];
					BitBuffer bitbuf(DataRate::_250K, trk.data, util::le_value(trk.used_bits));

					auto splicepos = util::le_value(trk.splice_pos);
					if (splicepos != 0xffff)
						bitbuf.splicepos(splicepos);

					auto cylhead = CylHead(tmap.map[i], static_cast<int>((i / cyl_step) / trks.size()));
					disk->write(cylhead, std::move(bitbuf));
				}
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
					disk->metadata[columns[0]] = columns[1];
			}
			break;
		}

		default:
			Message(msgWarning, "unknown WOZ chunk: %.*s [%02X %02X %02X %02X] with size %u",
				sizeof(wc.type), reinterpret_cast<const char *>(wc.type),
				wc.type[0], wc.type[1], wc.type[2], wc.type[3], chunk_size);

			file.seek(file.tell() + chunk_size);
			break;
		}

		file.seek(next_pos);
	}

	disk->strType = "WOZ";
	return true;
}
