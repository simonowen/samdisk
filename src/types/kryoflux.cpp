// KryoFlux STREAM format:
//  http://www.softpres.org/kryoflux:stream

#include "SAMdisk.h"
#include "DemandDisk.h"
#include "BitstreamDecoder.h"

#define MASTER_CLOCK_FREQ	(((18432000 * 73) / 14) / 2)	// 48054857.14
#define SAMPLE_FREQ			(MASTER_CLOCK_FREQ / 2)		 	// 24027428.57
#define INDEX_FREQ			(MASTER_CLOCK_FREQ / 16)	 	//  3003428.57
#define PS_PER_TICK(sck)	(1000000000 / (sck / 1000)) 	//    41619.10

enum { OOB = 0x0d };


class STREAMDisk final : public DemandDisk
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


static std::vector<std::vector<uint32_t>> decode_stream (const CylHead &cylhead, const std::vector<uint8_t> &data, Disk &disk)
{
	std::vector<std::vector<uint32_t>> flux_revs;
	std::vector<uint32_t> flux_times;
	flux_times.reserve(data.size());

	uint32_t time = 0, flux_count = 0, index_time = 0;
	uint32_t ps_per_tick = PS_PER_TICK(SAMPLE_FREQ);

	auto itBegin = data.begin(), it = itBegin, itEnd = data.end();
	while (it != itEnd)
	{
		if (index_time && flux_count >= index_time)
		{
			flux_revs.emplace_back(std::move(flux_times));
			flux_times.clear();
			flux_times.reserve(data.size() / sizeof(uint32_t));
			index_time = 0;
		}

		auto type = *it++;
		switch (type)
		{
			case 0x0c: // Flux3
				type = *it++;
				flux_count++;
			case 0x00: case 0x01: case 0x02: case 0x03:	// Flux 2
			case 0x04: case 0x05: case 0x06: case 0x07:
				time += (static_cast<uint32_t>(type) << 8) | *it++;
				flux_times.push_back(time * ps_per_tick / 1000);
				flux_count += 2;
				time = 0;
				break;
			case 0xa:	// Nop3
				it++;
				flux_count++;
			case 0x9:	// Nop2
				it++;
				flux_count++;
			case 0x8:	// Nop1
				flux_count++;
				break;
			case 0xb:	// Ovl16
				time += 0x10000;
				flux_count++;
				break;

			case OOB:	// OOB
			{
				if (time)
					Message(msgWarning, "OOB received during extended flux time (%lu)", time);

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
						index_time = util::letoh(pdw[0]);
						assert(index_time >= flux_count);
						break;
					}

					case 0x03:	// StreamEnd
					{
						assert(size == 8);
#if 0
						auto pdw = reinterpret_cast<const uint32_t *>(&*it);
						auto eof_pos = util::letoh(pdw[0]);
						auto eof_ret = util::letoh(pdw[1]);
#endif
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
				flux_count++;
				time = 0;
				break;
		}
	}

	if (flux_revs.size() > 1)
	{
		// Remove the first partial revolution
		flux_revs.erase(flux_revs.begin());
	}
	else
	{
		Message(msgWarning, "insufficient flux data on %s", CH(cylhead.cyl, cylhead.head));
		flux_revs.clear();
	}

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

	auto stream_disk = std::make_shared<STREAMDisk>();

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
			stream_disk->add_track_data(cylhead, std::move(flux_revs));
		}
	});

	if (missing_total)
		Message(msgWarning, "%d missing or invalid stream track%s", missing_total, (missing_total == 1) ? "" : "s");

	stream_disk->strType = "STREAM";
	disk = stream_disk;

	return true;
}
