// KryoFlux STREAM format:
//  http://www.softpres.org/kryoflux:stream

#include "SAMdisk.h"
#include "DemandDisk.h"

#define MASTER_CLOCK_FREQ	(((18432000 * 73) / 14) / 2)	// 48054857.14
#define SAMPLE_FREQ			(MASTER_CLOCK_FREQ / 2)		 	// 24027428.57
#define INDEX_FREQ			(MASTER_CLOCK_FREQ / 16)	 	//  3003428.57
#define PS_PER_TICK(sck)	(1000000000 / (sck / 1000)) 	//    41619.10

enum { OOB = 0x0d };


static std::vector<std::vector<uint32_t>> decode_stream (const CylHead &cylhead, const std::vector<uint8_t> &data, Disk &disk)
{
	std::vector<std::vector<uint32_t>> flux_revs;
	std::vector<uint32_t> flux_times, flux_counts;
	flux_times.reserve(data.size());
	flux_counts.resize(data.size());

	uint32_t time = 0, stream_pos = 0;
	uint32_t ps_per_tick = PS_PER_TICK(SAMPLE_FREQ);
	std::vector<uint32_t> index_offsets;

	auto itBegin = data.begin(), it = itBegin, itEnd = data.end();
	while (it != itEnd)
	{
		// Store current flux count at each stream position
		flux_counts[stream_pos] = flux_times.size();

		auto type = *it++;
		switch (type)
		{
			case 0x0c: // Flux3
				type = *it++;
				stream_pos++;
			case 0x00: case 0x01: case 0x02: case 0x03:	// Flux 2
			case 0x04: case 0x05: case 0x06: case 0x07:
				time += (static_cast<uint32_t>(type) << 8) | *it++;
				flux_times.push_back(time * ps_per_tick / 1000);
				stream_pos += 2;
				time = 0;
				break;
			case 0xa:	// Nop3
				it++;
				stream_pos++;
			case 0x9:	// Nop2
				it++;
				stream_pos++;
			case 0x8:	// Nop1
				stream_pos++;
				break;
			case 0xb:	// Ovl16
				time += 0x10000;
				stream_pos++;
				break;

			case OOB:	// OOB
			{
				auto subtype = *it++;
				uint16_t size = *it++;
				size |= (*it++ << 8);

				switch (subtype)
				{
					case 0x00:	// Invalid
						Message(msgWarning, "invalid OOB detected");
						it = itEnd;
						break;

					case 0x01:	// StreamInfo
						assert(size == 8);
						break;

					case 0x02:	// Index
					{
						assert(size == 12);

						auto pdw = reinterpret_cast<const uint32_t *>(&*it);
						index_offsets.push_back(util::letoh(pdw[0]));
						break;
					}

					case 0x03:	// StreamEnd
					{
						assert(size == 8);

						auto pdw = reinterpret_cast<const uint32_t *>(&*it);
//						auto eof_pos = util::letoh(pdw[0]);
						auto eof_ret = util::letoh(pdw[1]);

						if (eof_ret == 1)
							Message(msgWarning, "stream end (buffering problem) on %s", CH(cylhead.cyl, cylhead.head));
						else if (eof_ret == 2)
							Message(msgWarning, "stream end (no index detected) on %s", CH(cylhead.cyl, cylhead.head));
						else if (eof_ret != 0)
							Message(msgWarning, "stream end problem (%u) on %s", eof_ret, CH(cylhead.cyl, cylhead.head));
						break;
					}

					case 0x04:	// KFInfo
					{
						std::string info = reinterpret_cast<const char*>(&*it);
						for (auto &entry : util::split(info, ','))
						{
							auto pos = entry.find('=');
							if (pos != entry.npos)
							{
								auto name = util::trim(entry.substr(0, pos));
								auto value = util::trim(entry.substr(pos + 1));

								if (!name.empty() && !value.empty())
								{
									disk.metadata[name] = value;

									if (name == "sck")
										ps_per_tick = PS_PER_TICK(std::atoi(value.c_str()));
								}
							}
						}
						break;
					}

					case 0x0d:	// EOF
						assert(size == 0x0d0d);		// documented value
						size = 0;					// size is fake, so clear it
						it = itEnd;					// advance to end
						break;

					default:
						Message(msgWarning, "unexpected OOB sub-type (%X) on %s", CH(cylhead.cyl, cylhead.head));
						it = itEnd;
						break;
				}

				it += size;
				break;
			}

			default:	// Flux1
				time += type;
				flux_times.push_back(time * ps_per_tick / 1000);
				stream_pos++;
				time = 0;
				break;
		}
	}

	uint32_t last_pos = 0;
	for (auto index_offset : index_offsets)
	{
		// Ignore first partial track
		if (last_pos != 0)
		{
			assert(flux_counts[index_offset] != 0);

			// Extract flux segment for current revolution
			flux_revs.emplace_back(std::vector<uint32_t>(
				flux_times.begin() + last_pos,
				flux_times.begin() + flux_counts[index_offset]));
		}

		last_pos = flux_counts[index_offset];
	}

	if (flux_revs.size() == 0)
		Message(msgWarning, "no flux data on %s", CH(cylhead.cyl, cylhead.head));

	return flux_revs;
}

bool ReadSTREAM (MemFile &file, std::shared_ptr<Disk> &disk)
{
	uint8_t type;

	std::string path = file.path();
	if (!IsFileExt(path, "raw") || !file.rewind() || !file.read(&type, sizeof(type)) || type != OOB)
		return false;

	auto len = path.length();
	if (len < 8 || !std::isdigit(static_cast<uint8_t>(path[len - 8])) ||
		!std::isdigit(static_cast<uint8_t>(path[len - 7])) ||
		path[len - 6] != '.' ||
		!std::isdigit(static_cast<uint8_t>(path[len - 5])))
		return false;

	auto ext = path.substr(len - 3);
	path = path.substr(0, len - 8);

	auto stream_disk = std::make_shared<DemandDisk>();

	auto missing0 = 0, missing1 = 0, missing_total = 0;

	Range(MAX_TRACKS, MAX_SIDES).each([&] (const CylHead &cylhead) {
		auto track_path = util::fmt("%s%02u.%u.%s", path.c_str(), cylhead.cyl, cylhead.head, ext.c_str());

		MemFile f;
		if (!IsFile(track_path) || !f.open(track_path))
		{
			missing0 += (cylhead.head == 0);
			missing1 += (cylhead.head == 1);
		}
		else
		{
			// Track anything missing within the bounds of existing tracks
			if (cylhead.head == 0)
			{
				missing_total += missing0;
				missing0 = 0;
			}
			else
			{
				missing_total += missing1;
				missing1 = 0;
			}

			auto flux_revs = decode_stream(cylhead, f.data(), *disk);
			stream_disk->set_source(cylhead, std::move(flux_revs));
		}
	});

	if (missing_total)
		Message(msgWarning, "%d missing or invalid stream track%s", missing_total, (missing_total == 1) ? "" : "s");

	stream_disk->strType = "STREAM";
	disk = stream_disk;

	return true;
}
