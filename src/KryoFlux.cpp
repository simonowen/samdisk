// KryoFlux device base class

#include "SAMdisk.h"
#include "KryoFlux.h"
#include "FluxTrackBuffer.h"

#ifdef HAVE_LIBUSB1
#include "KF_libusb.h"
#endif

#ifdef HAVE_WINUSB
#include "KF_WinUSB.h"
#endif

#define MASTER_CLOCK_FREQ		(((18432000 * 73) / 14) / 2)	// 48054857.14
#define SAMPLE_FREQ				(MASTER_CLOCK_FREQ / 2)		 	// 24027428.57
#define INDEX_FREQ				(MASTER_CLOCK_FREQ / 16)	 	//  3003428.57
#define PS_PER_TICK(sck)		(1000000000 / (sck / 1000)) 	//    41619.10

const char * KryoFlux::KF_FW_FILE = "firmware_kf_usb_rosalie.bin";


std::unique_ptr<KryoFlux> KryoFlux::Open ()
{
	std::unique_ptr<KryoFlux> p;

	for (auto i = 0; i < 2; ++i)
	{
#ifdef HAVE_LIBUSB1
		if (!p)
			p = KF_libusb::Open();
#endif
#ifdef HAVE_WINUSB
		if (!p)
			p = KF_WinUsb::Open();
#endif

		// Missing device or present with firmware loaded?
		if (!p || p->GetProductName() == "KryoFlux DiskSystem")
			break;

		p->UploadFirmware();
		p.reset();
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}

	return p;
}

void KryoFlux::UploadFirmware ()
{
	// Set interactive then non-interactive mode, to check for boot responses
	// https://sourceforge.net/p/lejos/wiki-nxt/SAM-BA%20Protocol/
	SamBaCommand("T#", ">");
	SamBaCommand("N#", "\n\r");

	auto fwpath = util::resource_dir() + KF_FW_FILE;
	std::ifstream fwfile(fwpath, std::ios::binary);
	if (!fwfile)
		throw posix_error(errno, fwpath.c_str());

	auto fwdata = std::vector<uint8_t>(std::istreambuf_iterator<char>(fwfile),
									   std::istreambuf_iterator<char>());
	auto fwsize = static_cast<int>(fwdata.size());

	SamBaCommand(util::fmt("S%08lx,%08lx#", KF_FW_LOAD_ADDR, fwsize));

	auto offset = 0;
	while (offset < fwsize)
		offset += Write(fwdata.data() + offset, fwsize - offset);

	SamBaCommand(util::fmt("R%08lx,%08lx#", KF_FW_LOAD_ADDR, fwsize));

	std::vector<uint8_t> fwverify(fwsize);
	for (offset = 0; offset < fwsize; )
		offset += Read(fwverify.data() + offset, fwsize - offset);

	if (fwdata != fwverify)
		throw util::exception("firmware verification failed");

	SamBaCommand(util::fmt("G%08lx#", KF_FW_EXEC_ADDR));
}

void KryoFlux::SamBaCommand (const std::string &cmd, const std::string &end)
{
	std::string s;
	uint8_t buf[512];

	try
	{
		Write(cmd.c_str(), static_cast<int>(cmd.length()));

		if (!end.empty())
		{
			for (;;)
			{
				auto len = Read(buf, sizeof(buf));

				s += std::string(reinterpret_cast<char *>(buf), len);

				if (s.length() >= end.length() &&
					s.compare(s.length() - end.length(), end.length(), end) == 0)
				{
					break;
				}
			}
		}
	}
	catch (...)
	{
		throw util::exception("firmware upload failed, try resetting device");
	}
}


/*static*/ int KryoFlux::ResponseCode (const std::string &str)
{
	auto pos = str.find_first_of('=');
	if (pos == std::string::npos)
		return 0;

	return static_cast<int>(std::atoi(str.c_str() + pos + 1));
}


int KryoFlux::Reset ()
{
	return ResponseCode(Control(REQ_RESET));
}

int KryoFlux::SelectDevice (int device)
{
	return ResponseCode(Control(REQ_DEVICE, device));
}

int KryoFlux::EnableMotor (int enable)
{
	return ResponseCode(Control(REQ_MOTOR, enable));
}

int KryoFlux::Seek (int cyl)
{
	return ResponseCode(Control(REQ_TRACK, cyl));
}

int KryoFlux::SelectDensity (bool density)
{
	return ResponseCode(Control(REQ_DENSITY, density));
}

int KryoFlux::SelectSide (int head)
{
	return ResponseCode(Control(REQ_SIDE, head));
}

int KryoFlux::SetMinTrack (int cyl)
{
	return ResponseCode(Control(REQ_MIN_TRACK, cyl));
}

int KryoFlux::SetMaxTrack (int cyl)
{
	return ResponseCode(Control(REQ_MAX_TRACK, cyl));
}


void KryoFlux::ReadFlux (int revs, FluxData &flux_revs, std::vector<std::string> &warnings)
{
	revs = std::max(1, std::min(revs, 20));

	Data track_data;
	track_data.reserve(1'000'000);

	// Start stream, for 1 more index hole than we require revolutions
	Control(REQ_STREAM, ((revs + 1) << 8) | 0x01);

	for (;;)
	{
		uint8_t buf[0x10000];
		auto len = Read(buf, sizeof(buf));
		track_data.insert(track_data.end(), buf, buf + len);

		// Check for end marker at end of current packet.
		// ToDo: parse this properly, due to small risk of false positives.
		if (len >= 7 && !memcmp(buf + len - 7, "\xd\xd\xd\xd\xd\xd\xd", 7))
		{
			// Stop streaming and finish
			Control(REQ_STREAM, 0);
			break;
		}
	}

	// Decode the track data to multiple flux revolutions, plus any warnings
	flux_revs = std::move(DecodeStream(track_data, warnings));
}


/*static*/ FluxData KryoFlux::DecodeStream (const Data &data, std::vector<std::string> &warnings)
{
	FluxData flux_revs;
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
		flux_counts[stream_pos] = static_cast<int>(flux_times.size());

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
						warnings.push_back("invalid OOB detected");
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
							warnings.push_back("stream end (buffering problem)");
						else if (eof_ret == 2)
							warnings.push_back("stream end (no index detected)");
						else if (eof_ret != 0)
							warnings.push_back(util::fmt("stream end problem (%u)", eof_ret));
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
									//									disk.metadata[name] = value;

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
						warnings.push_back(util::fmt("unexpected OOB sub-type (%X)", subtype));
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
		warnings.push_back("no flux data");

	return flux_revs;
}
