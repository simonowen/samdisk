// CWTool = CatWeasel binary disk image created by CWTool:
//  http://www.discferret.com/wiki/CWTool_image_format

#include "SAMdisk.h"

typedef struct
{
	char signature[32];	// padded with nulls
} CWTOOL_FILE_HEADER;

typedef struct
{
	uint8_t magic;		// 0xCA
	uint8_t track;		// typically 0-165 for 80 cyls 2 heads
	uint8_t clock;		// 0=14MHz, 1=28, 2=56
	uint8_t flags;
	uint32_t size;		// little-endian
} CWTOOL_TRACK_HEADER;

enum : uint8_t
{
	FLAG_WRITEABLE = 1,
	FLAG_INDEX_STORED = 2,
	FLAG_INDEX_ALIGNED = 4,
	FLAG_NO_CORRECTION = 8
};

bool ReadCWTOOL (MemFile &file, std::shared_ptr<Disk> &disk)
{
	CWTOOL_FILE_HEADER fh;

	if (!file.rewind() || !file.read(&fh, sizeof(fh)))
		return false;
	else if (memcmp(fh.signature, "cwtool raw data", 15))
		return false;

	if (fh.signature[16] != '3')
		throw util::exception("only v3 files currently supported");

	std::vector<std::pair<CWTOOL_TRACK_HEADER, Data>> track_data;
	track_data.reserve(MAX_TRACKS * MAX_SIDES);

	CWTOOL_TRACK_HEADER th;
	auto num_tracks = 0;
	auto max_track = 0;
	auto clock_khz = -1;

	// First pass to determine disk geometry
	for (; file.read(&th, sizeof(th)); ++num_tracks)
	{
		if (th.magic != 0xca)
			throw util::exception("bad magic on track ", track_data.size());
		else if (th.clock >= 3)
			throw util::exception("invalid clock speed (", th.clock, ") on track ", th.track);
		else if (!file.seek(file.tell() + util::letoh(th.size)))
			throw util::exception("short file reading track ", th.track);
		else if (!(th.flags & FLAG_INDEX_ALIGNED))
			throw util::exception("only indexed-aligned images are currently supported");

		max_track = std::max(max_track, static_cast<int>(th.track));
	}

	file.seek(sizeof(fh));

	auto heads = (num_tracks > 85) ? 2 : 1;

	// Second pass to decode the data
	while (file.read(&th, sizeof(th)))
	{
		auto size = util::letoh(th.size);
		std::vector<uint8_t> data(size);
		file.read(data);

		CylHead cylhead(th.track / heads, th.track % heads);
		auto correction = (th.flags & FLAG_NO_CORRECTION) ? 0 : 1;

		clock_khz = 14161 << th.clock;
		auto ps_per_tick = 1'000'000'000 * 2 / clock_khz;

		FluxData flux_revs;
		std::vector<uint32_t> flux;
		flux.reserve(data.size());

		if (th.flags & FLAG_INDEX_STORED)	// index markers
		{
			bool last_index = false;

			for (auto b : data)
			{
				bool index = (b & 0x80) != 0;
				if (index && !last_index)
				{
					if (!flux.empty())
					{
						flux_revs.push_back(flux);	// copy
						flux.clear();
					}
				}
				last_index = index;

				auto time_ns = ((b & 0x7f) + correction) * ps_per_tick / 1000;
				flux.push_back(time_ns);
			}
		}
		else // index to index
		{
			for (int b : data)
			{
				auto time_ns = (b + correction) * ps_per_tick / 1000;
				flux.push_back(time_ns);
			}
		}

		if (!flux.empty())
			flux_revs.push_back(std::move(flux));

		disk->add(TrackData(cylhead, std::move(flux_revs)));
	}

	if (clock_khz > 0)
		disk->metadata["clock"] = util::fmt("%.3fMHz", static_cast<float>(clock_khz) / 1000.0);

	if (num_tracks != (max_track + 1))
		disk->metadata["revolutions"] = std::to_string(num_tracks / (max_track + 1));

	disk->strType = "CWTool";

	return true;
}
