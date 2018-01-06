// Decode flux reversals and bitstreams to something recognisable

#include "SAMdisk.h"
#include "BitstreamDecoder.h"
#include "FluxDecoder.h"
#include "BitBuffer.h"
#include "TrackDataParser.h"
#include "IBMPC.h"
#include "JupiterAce.h"
#include "SpecialFormat.h"

// Scan track flux reversals for sectors. We default to the order MFM/FM,
// Amiga, then GCR. On subsequent calls the last successful encoding is
// checked first, as it's the most likely.

void scan_flux (TrackData &trackdata)
{
	static Encoding last_encoding = Encoding::MFM;

	// Return an empty track if we have no data
	if (trackdata.flux().empty())
		return;

	// Sum the flux times on the last revolution
	int64_t total_time = 0;
	for (const auto &time : trackdata.flux().back())
		total_time += time;

	// Convert from ns to us and save as track time
	Track track;
	track.tracktime = static_cast<int>(total_time / 1000);
	trackdata.add(std::move(track));

	// Jupiter Ace scanning must be explicitly requested, as the end of tracks
	// aren't wiped and often contain MFM sectors from previous disk use.
	if (opt.ace)
	{
		scan_flux_ace(trackdata);
		return;
	}

	if (opt.mx)
	{
		// Try 300K first (5.25" drive)
		scan_flux_mx(trackdata, DataRate::_300K);

		// If that fails, fall back on 250K (3.5" drive)
		if (trackdata.track().empty())
			scan_flux_mx(trackdata, DataRate::_250K);

		return;
	}

	if (opt.agat)
	{
		scan_flux_agat(trackdata);
		return;
	}

	// Set the encoding scanning order, with the last successful encoding first (and its duplicate removed)
	std::vector<Encoding> encodings = { last_encoding, Encoding::MFM, Encoding::Amiga/*, Encoding::GCR*/ };
	encodings.erase(std::next(std::find(encodings.rbegin(), encodings.rend(), last_encoding)).base());

	// MFM and FM use the same scanner, so remove the duplicate
	if (last_encoding == Encoding::FM)
		encodings.erase(std::find(encodings.rbegin(), encodings.rend(), Encoding::MFM).base());

	for (auto encoding : encodings)
	{
		switch (encoding)
		{
			case Encoding::MFM:
			case Encoding::FM:
			{
				static DataRate last_datarate = DataRate::_250K;

				scan_flux_mfm_fm(trackdata, last_datarate);

				// If we found something, remember the successful data rate
				if (!trackdata.track().empty())
					last_datarate = trackdata.bitstream().datarate;

				break;
			}

			case Encoding::Amiga:
			{
				scan_flux_amiga(trackdata);
				break;
			}

			case Encoding::GCR:
				scan_flux_gcr(trackdata);
				break;

			default:
				assert(false);
				break;
		}

		// Continue scanning formats in multi-format mode
		if (opt.multiformat)
			continue;

		// Something found?
		if (!trackdata.track().empty())
		{
			// Remember the encoding so we try it first next time
			last_encoding = encoding;
			break;
		}
	}
}


// Scan a track bitstream for sectors
void scan_bitstream (TrackData &trackdata)
{
	static Encoding last_encoding = Encoding::MFM;

	// Jupiter Ace scanning must be explicitly requested, as the end of tracks aren't wiped
	// and often contain MFM sectors from the previous disk use.
	if (opt.ace)
	{
		scan_bitstream_ace(trackdata);
		return;
	}

	if (opt.mx)
	{
		scan_bitstream_mx(trackdata);
		return;
	}

	if (opt.agat)
	{
		scan_bitstream_agat(trackdata);
		return;
	}

	// Set the encoding scanning order, with the last successful encoding first (and its duplicate removed)
	std::vector<Encoding> encodings = { last_encoding, Encoding::MFM, Encoding::Amiga/*, Encoding::GCR*/ };
	encodings.erase(std::next(std::find(encodings.rbegin(), encodings.rend(), last_encoding)).base());

	// MFM and FM use the same scanner, so remove the duplicate
	if (last_encoding == Encoding::FM)
		encodings.erase(std::find(encodings.rbegin(), encodings.rend(), Encoding::MFM).base());

	for (auto encoding : encodings)
	{
		switch (encoding)
		{
			case Encoding::MFM:
			case Encoding::FM:
			{
				scan_bitstream_mfm_fm(trackdata);
				break;
			}

			case Encoding::Amiga:
			{
				scan_bitstream_amiga(trackdata);
				break;
			}

			case Encoding::GCR:
				scan_bitstream_gcr(trackdata);
				break;

			default:
				assert(false);
				break;
		}

		// Something found?
		if (!trackdata.track().empty())
		{
			// Remember the encoding so we try it first next time
			last_encoding = encoding;
			break;
		}
	}
}


/*
GCR 5/3 encode/decode
0xab, 0xad, 0xae, 0xaf, 0xb5, 0xb6, 0xb7, 0xba,
0xbb, 0xbd, 0xbe, 0xbf, 0xd6, 0xd7, 0xda, 0xdb,
0xdd, 0xde, 0xdf, 0xea, 0xeb, 0xed, 0xee, 0xef,
0xf5, 0xf6, 0xf7, 0xfa, 0xfb, 0xfd, 0xfe, 0xff,

GCR 6/2 encode/decode
0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,

Physical to Apple DOS 3.3 logical sector mapping:
0x0, 0x7, 0xe, 0x6, 0xd, 0x5, 0xc, 0x4,
0xb, 0x3, 0xa, 0x2, 0x9, 0x1, 0x8, 0xf

http://www.scribd.com/doc/200679/Beneath-Apple-DOS-By-Don-Worth-and-Pieter-Lechner
*/

void scan_bitstream_gcr (TrackData &trackdata)
{
	auto &bitbuf = trackdata.bitstream();
	bitbuf.seek(0);

	Track track;
	track.tracklen = bitbuf.track_bitsize();
	trackdata.add(std::move(track));

	uint32_t dword = 0;
	size_t marks = 0;

	// ToDo: finish GCR - for now we just count known marks
	while (!bitbuf.wrapped())
	{
		dword = (dword << 1) | bitbuf.read1();

		if ((dword & 0xffffff) == 0xd5aa96 ||	// address prologue			D5 AA 96 volume track sector checksum DE AA EB
			(dword & 0xffffff) == 0xd5aaad ||	// data prologue			D5 AA AD [342 bytes 6/2 encoded] checksum DE AA EB
			(dword & 0xffffff) == 0xdeaaeb)		// address/data epilogue
		{
			++marks;
		}
		else if ((dword & 0x3ff) == 0x3fb)		// sync
		{
		}
	}

	// Fail if we found enough marks to suggest GCR content
	if (marks > 10)
		throw std::logic_error("GCR format is not currently supported");
}


// ToDo: finish GCR support
void scan_flux_gcr (TrackData &trackdata)
{
	int bitcell_ns;

	// C64 GCR disks are zoned, with variable rate depending on cylinder
	// ToDo: make this optional, or passed in if needed
	if (trackdata.cylhead.cyl < 17)
		bitcell_ns = 3200;
	else if (trackdata.cylhead.cyl < 24)
		bitcell_ns = 3500;
	else if (trackdata.cylhead.cyl < 30)
		bitcell_ns = 3750;
	else
		bitcell_ns = 4000;

	FluxDecoder decoder(trackdata.flux(), bitcell_ns);
	BitBuffer bitbuf(DataRate::_250K, decoder);

	trackdata.add(std::move(bitbuf));
	scan_bitstream_gcr(trackdata);
}


void scan_bitstream_ace (TrackData &trackdata)
{
	auto &bitbuf = trackdata.bitstream();
	bitbuf.seek(0);

	Track track;
	track.tracklen = bitbuf.track_bitsize();

	uint32_t word = 0;

	enum State { stateWant255, stateWant42, stateData };
	State state = stateWant255;
	Data block;
	int idle = 0;
	bool dataerror = false;
	int bit;

	while (!bitbuf.wrapped())
	{
		// Read the next clock and data bits
		word = (bitbuf.read1() << 1) | bitbuf.read1();

		// If the clock is missing, attempt to re-sync by skipping a bit
		if (!(word & 2))
		{
			bitbuf.read1();
			continue;
		}

		// Outside a frame a 1 represents the idle state
		if (~word & 1)
		{
			// Stop if we've found an idle patch after valid data
			if (++idle > 64 && state == stateData)
				break;

			// Ignore idle otherwise
			continue;
		}

		// The transition to 0 represents a potential start bit, so reset the idle count
		idle = 0;

		bit = 0;
		uint8_t data = 0;	// no data yet
		int parity = 1;		// odd parity
		int clock = 2;		// valid clock

		// Read 8 data bits, 1 parity bit, 1 stop bit
		for (int i = 0; i < 10; ++i)
		{
			// Fetch clock and data bits
			word = (bitbuf.read1() << 1) | bitbuf.read1();

			// Extract bit, update parity and clock status
			bit = ~word & 1;
			parity ^= bit;
			clock &= word;

			// Add data bit (lsb to msb order)
			data |= (bit << i) & 0xff;

		}

		// Check for errors
		if (!clock || !bit || !parity)
		{
			if (state != stateData)
				continue;

			// Report only first error during the data block, unless verbose
			if (!dataerror || opt.verbose)
			{
				dataerror = true;

				if (!clock || !bit)
					Message(msgWarning, "framing error at offset %u on on %s", block.size(), CH(trackdata.cylhead.cyl, trackdata.cylhead.head));
				else if (!parity) // inverted due to inclusion of stop bit above
					Message(msgWarning, "parity error at offset %u on on %s", block.size(), CH(trackdata.cylhead.cyl, trackdata.cylhead.head));
			}
		}
		else
		{
			switch (state)
			{
				case stateWant255:
					if (data == 255)
						state = stateWant42;
					else
						block.clear();
					break;

				case stateWant42:
					if (data == 42)
						state = stateData;
					else if (data != 255)
					{
						state = stateWant255;
						block.clear();
					}
					break;

				default:
					break;
			}
		}

		// Save the byte
		block.push_back(data);
	}

	// If we found a block on the track, save it in a 4K sector with id=0.
	if (state == stateData)
	{
		Sector sector(DataRate::_250K, Encoding::Ace, Header(trackdata.cylhead, 0, SizeToCode(4096)));

		// Skip header bytes
		if (!IsValidDeepThoughtData(block))
		{
			Message(msgWarning, "block checksum error on %s", CH(trackdata.cylhead.cyl, trackdata.cylhead.head));
			dataerror = true;
		}

		sector.add(std::move(block), dataerror, 0x00);
		track.add(std::move(sector));
	}

	trackdata.add(std::move(track));
}

void scan_flux_ace (TrackData &trackdata)
{
	FluxDecoder decoder(trackdata.flux(), 4000);	// 125Kbps with 4us bitcell width
	BitBuffer bitbuf(DataRate::_250K, decoder);

	trackdata.add(std::move(bitbuf));
	scan_bitstream_ace(trackdata);
}


/*
 * DVK MX format.  DVK was a family of DEC LSI-11 compatible computers
 * produced by Soviet Union in 1980's, MX.SYS is the RT-11 driver name
 * for the controller.
 *
 * FM encoding. Track format is driver-dependent (except the sync word).
 * Hardware always reads or writes entire track, hence no sector headers.
 *
 * 1. gap1 (8 words)
 * 2. sync (1 word, 000363 octal, regular FM encoding)
 * 3. zero-based track number (1 word)
 * 4. 11 sectors:
 *   data (128 words)
 *   checksum (1 word)
 * 5. extra (1..4 words)
 * 6. gap4 (or unformatted)
 *
 * See also http://torlus.com/floppy/forum/viewtopic.php?f=19&t=1384
 */

void scan_bitstream_mx (TrackData &trackdata)
{
	Track track;
	Data block;
	uint64_t dword = 0;
	uint16_t stored_cksum = 0, cksum = 0;

	auto &bitbuf = trackdata.bitstream();
	bitbuf.seek(0);
	bitbuf.encoding = Encoding::FM;
	track.tracklen = bitbuf.track_bitsize();
	bool sync = false;

	while (!bitbuf.wrapped())
	{
		// Give up if no headers were found in the first revolution
		if (!track.size() && bitbuf.tell() > track.tracklen)
			break;

		// ignore sync sequences after first one
		if (sync)
			break;

		dword = (dword << 1) | bitbuf.read1();
		if (opt.debug)
		util::cout << util::fmt ("  s_b_mx %016lx c:h %d:%d\n", dword, trackdata.cylhead.cyl, trackdata.cylhead.head);

		switch (dword)
		{
			case 0x88888888aaaa88aa:	// FM-encoded 0x00f3 (000363 octal)
				sync = true;
				if (opt.debug) util::cout << "  s_b_mx found sync at " << bitbuf.tell() << "\n";
				break;

			default:
				continue;
		}

		// skip track number
		bitbuf.read_byte();
		bitbuf.read_byte();

		// read sectors
		for (auto s = 0; s < 11; s++) {
			Sector sector(bitbuf.datarate, Encoding::MX, Header(trackdata.cylhead, s, SizeToCode(256)));
			sector.offset = bitbuf.track_offset(bitbuf.tell());

			block.clear();
			cksum = 0;

			for (auto i = 0; i < 128; i++) {
				auto msb = bitbuf.read_byte();
				auto lsb = bitbuf.read_byte();
				cksum += (lsb | (msb << 8));
				block.push_back(lsb);
				block.push_back(msb);
			}

			stored_cksum  = bitbuf.read_byte() << 8;
			stored_cksum |= bitbuf.read_byte();

			if (opt.debug) util::cout << util::fmt ("cksum s %d disk:calc %06o:%06o (%04x:%04x)\n",
				s, stored_cksum, cksum, stored_cksum, cksum);

			sector.add(std::move(block), cksum != stored_cksum, 0);
			track.add(std::move(sector));
		}
	}

	trackdata.add(std::move(track));
}

void scan_flux_mx (TrackData &trackdata, DataRate datarate)
{
	FluxDecoder decoder(trackdata.flux(), ::bitcell_ns(datarate), opt.scale);
	BitBuffer bitbuf(datarate, decoder);

	trackdata.add(std::move(bitbuf));
	scan_bitstream_mx(trackdata);
}


#define MFM_MASK	0x55555555UL

static bool amiga_read_dwords (BitBuffer &bitbuf, uint32_t *pdw, size_t dwords, uint32_t &checksum)
{
	std::vector<uint32_t> evens;
	evens.reserve(dwords);
	size_t i;

	// First pass to gather even bits
	for (i = 0; i < dwords; ++i)
	{
		auto evenbits = bitbuf.read32();
		evens.push_back(evenbits);
		checksum ^= evenbits;
	}

	// Second pass to read odd bits, and combine to form the decoded data
	for (auto evenbits : evens)
	{
		auto oddbits = bitbuf.read32();
		checksum ^= oddbits;

		// Strip MFM clock bits, and merge to give 32-bit data value
		uint32_t value = ((evenbits & MFM_MASK) << 1) | (oddbits & MFM_MASK);
		*pdw++ = util::betoh(value);
	}

	return !bitbuf.wrapped() || !bitbuf.tell();
}

void scan_bitstream_amiga (TrackData &trackdata)
{
	auto &bitbuf = trackdata.bitstream();
	bitbuf.seek(0);

	Track track;
	track.tracklen = bitbuf.track_bitsize();

	CRC16 crc;
	uint32_t dword = 0;

	while (!bitbuf.wrapped())
	{
		// Give up if no headers were found in the first revolution
		if (!track.size() && bitbuf.tell() > track.tracklen)
			break;

		dword = (dword << 1) | bitbuf.read1();

		// Check for A1A1 MFM sync markers
		if (dword != 0x44894489)
			continue;

		auto sector_offset = bitbuf.tell();

		// Decode the info block from the odd and even MFM components
		uint32_t info = 0, calcsum = 0;
		if (!amiga_read_dwords(bitbuf, &info, 1, calcsum))
			continue;

		uint8_t type = info & 0xff;
		uint8_t track_nr = (info >> 8) & 0xff;
		uint8_t sector_nr = (info >> 16) & 0xff;
		uint8_t eot = (info >> 24) & 0xff;

		// Check for AmigaDOS (0xff), sector / sector end within normal range, and track number matching physical location
		auto max_sectors{(bitbuf.datarate == DataRate::_500K) ? 22 : 11};
		if (type != 0xff || sector_nr >= max_sectors || !eot || eot > max_sectors ||
			track_nr != static_cast<uint8_t>((trackdata.cylhead.cyl << 1) + trackdata.cylhead.head))
			continue;

		std::vector<uint32_t> label(4);
		if (!amiga_read_dwords(bitbuf, label.data(), label.size(), calcsum))
			continue;

		// Warn if the label field isn't empty
		if (*std::max_element(label.begin(), label.end()) != 0)
			Message(msgWarning, "%s label field is not empty", CHS(trackdata.cylhead.cyl, trackdata.cylhead.head, (info >> 8) & 0xff));

		// Read the header checksum, and combine with checksum so far
		uint32_t disksum;
		if (!amiga_read_dwords(bitbuf, &disksum, 1, calcsum))
			continue;

		// Mask the checksum to include only the data bits
		calcsum &= MFM_MASK;
		if (calcsum != 0 && !opt.idcrc)
			continue;

		Sector sector(bitbuf.datarate, Encoding::Amiga, Header(trackdata.cylhead, sector_nr, 2));
		sector.offset = bitbuf.track_offset(sector_offset);

		// Read the data checksum
		if (!amiga_read_dwords(bitbuf, &disksum, 1, calcsum))
			continue;

		// Read the data field
		Data data(sector.size());
		if (!amiga_read_dwords(bitbuf, reinterpret_cast<uint32_t*>(data.data()), data.size() / sizeof(uint32_t), calcsum))
			continue;

		if (opt.debug) util::cout << "* AmigaDOS (id=" << sector_nr << ") at offset " << sector_offset << " (" << bitbuf.track_offset(sector_offset) << ")\n";

		bool bad_data = (calcsum & MFM_MASK) != 0;
		sector.add(std::move(data), bad_data, 0x00);

		track.add(std::move(sector));
	}

	trackdata.add(std::move(track));
}

void scan_flux_amiga (TrackData &trackdata)
{
	// Scale the flux values to simulate motor speed variation
	for (auto flux_scale : { 100, 95, 105 })
	{
		FluxDecoder decoder(trackdata.flux(), ::bitcell_ns(DataRate::_250K), flux_scale);
		BitBuffer bitbuf(DataRate::_250K, decoder);

		trackdata.add(std::move(bitbuf));
		scan_bitstream_amiga(trackdata);
		auto &track = trackdata.track();

		// Stop if there's nothing to fix or motor wobble is disabled
		if (!track.has_data_error() || opt.nowobble)
			break;
	}
}

void scan_bitstream_mfm_fm (TrackData &trackdata)
{
	Track track;

	auto &bitbuf = trackdata.bitstream();
	bitbuf.seek(0);
	track.tracklen = bitbuf.track_bitsize();

	CRC16 crc;
	std::vector<std::pair<int, Encoding>> data_fields;

	uint32_t dword = 0;
	bool seen_fm_sector = false;

	while (!bitbuf.wrapped())
	{
		// Give up if no headers were found in the first revolution
		if (!track.size() && bitbuf.tell() > track.tracklen)
			break;

		dword = (dword << 1) | bitbuf.read1();

		if (dword == 0x44894489)
		{
			if (bitbuf.read16() != 0x4489) continue;

			bitbuf.encoding = Encoding::MFM;
			crc.init(CRC16::A1A1A1);
		}
		else if (opt.nofm)
			continue;
		else
		{
			// Check for known FM address marks
			switch (dword)
			{
				case 0xaa222888:	// F8/C7 DDAM
				case 0xaa22288a:	// F9/C7 Alt-DDAM
				case 0xaa2228a8:	// FA/C7 Alt-DAM
				case 0xaa2228aa:	// FB/C7 DAM
				case 0xaa222a88:	// FC/C7 IAM
				case 0xaa222a8a:	// FD/C7 RX02 DAM
				case 0xaa222aa8:	// FE/C7 IDAM
					break;

				// Not a recognised FM mark, so keep searching
				default:
					continue;
			}

			// With FM the address mark is also the sync, so step back to read it again
			bitbuf.seek(bitbuf.tell() - 32);

			bitbuf.encoding = Encoding::FM;
			crc.init();
		}

		auto am_offset = bitbuf.tell();
		auto am = bitbuf.read_byte();
		crc.add(am);

		switch (am)
		{
			case 0xfe:	// IDAM
			{
				std::array<uint8_t, 6> id;	// CHRN + 16-bit CRC
				bitbuf.read(id);

				// Check header CRC, skipping if it's bad, unless the user wants it.
				// Don't allow FM sectors with ID CRC errors, due to the false-positive risk.
				crc.add(id.data(), id.size());
				if (!crc || (opt.idcrc == 1 && bitbuf.encoding != Encoding::FM))
				{
					Header header(id[0], id[1], id[2], id[3]);
					Sector s(bitbuf.datarate, bitbuf.encoding, header);
					s.set_badidcrc(crc != 0);
					s.offset = bitbuf.track_offset(am_offset);

					if (opt.debug) util::cout << "* IDAM (id=" << header.sector << ") at offset " << am_offset << " (" << s.offset << ")\n";
					track.add(std::move(s));

					// Track good FM sector headers
					seen_fm_sector |= (bitbuf.encoding == Encoding::FM && !crc);
				}
				break;
			}

			case 0xfb: case 0xfa:	// normal data (+alt)
			case 0xf8: case 0xf9:	// deleted data (+alt)
			case 0xfd:				// RX02
			{
				// FM address marks are short, so false positives are likely. There's no way to validate
				// DAMs without knowing the size, so we ignore them if we've not seen a valid FM header.
				if (bitbuf.encoding == Encoding::FM && !seen_fm_sector)
					break;

				if (opt.debug) util::cout << "* DAM (am=" << am << ") at offset " << am_offset << " (" << bitbuf.track_offset(am_offset) << ")\n";
				data_fields.push_back(std::make_pair(am_offset, bitbuf.encoding));
				break;
			}

			case 0xfc:	// IAM
				if (opt.debug) util::cout << "* IAM at offset " << am_offset << " (" << bitbuf.track_offset(am_offset) << ")\n";
				break;

			// This isn't a valid AM type, but Skate or Die [+3] and Les Dieux
			// du Ciel [CPC] use this on unused 8K sectors (script bug?), so
			// we now silently ignore it.
			case 0xe5:
				break;

			// Another invalid AM type to ignore, as found on cyls 40+41 of
			// International 3D Tennis [+3].
			case 0x80:
				break;

			default:
				Message(msgWarning, "unknown %s address mark (%02X) at offset %u on %s", to_string(bitbuf.encoding).c_str(), am, am_offset, CH(trackdata.cylhead.cyl, trackdata.cylhead.head));
				break;
		}
	}

	// Process each sector header to look for an associated data field
	for (auto it = track.begin(); it != track.end(); ++it)
	{
		auto &sector = *it;
		auto final_sector = std::next(it) == track.end();

		auto shift = (sector.encoding == Encoding::FM) ? 5 : 4;
		auto gap2_size = (sector.datarate == DataRate::_1M) ? GAP2_MFM_ED : GAP2_MFM_DDHD;	// gap2 size in MFM bytes (FM is half size but double encoding overhead)
		auto min_distance = ((1 + 6) << shift) + (gap2_size << 4);			// AM, ID, gap2 (fixed shift as FM is half size)
		auto max_distance = ((1 + 6) << shift) + ((23 + gap2_size) << 4);		// 1=AM, 6=ID, 21+gap2=max WD177x offset (gap2 may be longer when formatted by different type of controller)

		// If the header has a CRC error, the data can't be reached
		if (sector.has_badidcrc())
			continue;

		if (opt.debug) util::cout << "Finding " << trackdata.cylhead << " sector " << sector.header.sector << ":\n";

		for (auto itData = data_fields.begin(); itData != data_fields.end(); ++itData)
		{
			const auto &dam_offset = itData->first;
			const Encoding &data_encoding = itData->second;
			auto itDataNext = (std::next(itData) == data_fields.end()) ? data_fields.begin() : std::next(itData);

			// Data field must be the same encoding type
			if (data_encoding != sector.encoding)
				continue;

			// Determine distance from header to data field, taking care of track wrapping
			auto dam_track_offset = bitbuf.track_offset(dam_offset);
			auto distance = ((dam_track_offset < sector.offset) ? track.tracklen : 0) + dam_track_offset - sector.offset;

			// Reject if the data field is too close or too far away
			if (distance < min_distance || distance > max_distance)
				continue;

			// If there's a splice between IDAM and DAM the track was probably modified
#if 0 // disabled until header/data matching enhancements are complete
			if (bitbuf.sync_lost(sector.offset, dam_offset))
				track.modified = true;
#endif
			bitbuf.seek(dam_offset);
			bitbuf.encoding = data_encoding;

			auto dam = bitbuf.read_byte();
			crc.init((data_encoding == Encoding::MFM) ? CRC16::A1A1A1 : CRC16::INIT_CRC);
			crc.add(dam);

			// Determine the offset and distance to the next IDAM, taking care of track wrap if it's the final sector
			auto next_idam_offset = final_sector ? track.begin()->offset : std::next(it)->offset;
			auto next_idam_distance = ((next_idam_offset < dam_track_offset) ? track.tracklen : 0) + next_idam_offset - dam_track_offset;
			auto next_idam_bytes = (next_idam_distance >> shift) - 1;	// -1 due to DAM being read above
			auto next_idam_align = next_idam_distance & ((1 << shift) - 1);

			// Determine the bit offset and distance to the next DAM
			auto next_dam_offset = itDataNext->first;
			auto next_dam_distance = ((next_dam_offset < dam_offset) ? bitbuf.size() : 0) + next_dam_offset - dam_offset;
			auto next_dam_bytes = (next_dam_distance >> shift) - 1;		// -1 due to DAM being read above

			// Attempt to read gap2 from non-final sectors, unless we're asked not to
			auto read_gap2 = !final_sector && (opt.gap2 != 0);

			// Calculate the extent of the current data field, up to the next header or data field (depending if gap2 is required)
			auto extent_bytes = read_gap2 ? next_dam_bytes : next_idam_bytes;
			if (extent_bytes >= 3 && sector.encoding == Encoding::MFM) extent_bytes -= 3;	// remove A1A1A1

			auto normal_bytes = sector.size() + 2;					// data size + CRC bytes
			auto data_bytes = std::max(normal_bytes, extent_bytes);	// data size needed to check CRC

			// Calculate bytes remaining in the data in current data encoding
			auto avail_bytes = bitbuf.remaining() >> shift;

			// Ignore truncated copies, unless it's the only copy we have
			if (avail_bytes < normal_bytes)
			{
				// If we've already got a copy, ignore the truncated version
				if (sector.copies() && (!sector.is_8k_sector() || avail_bytes < 0x1802))	// ToDo: fix nasty check
				{
					if (opt.debug) util::cout << "ignoring truncated sector copy\n";
					continue;
				}

				if (opt.debug) util::cout << "using truncated sector data as only copy\n";
			}

			// Read the full data field and check its CRC
			Data data(data_bytes);
			bitbuf.read(data);
			bool bad_crc = crc.add(data.data(), normal_bytes) != 0;

			// Truncate at the extent size, unless we're asked to keep overlapping sectors
			if (!opt.keepoverlap && extent_bytes < sector.size())
				data.resize(extent_bytes);
			else if (data.size() > sector.size() && (opt.gaps == GAPS_NONE || (opt.gap4b == 0 && final_sector)))
				data.resize(sector.size());

			auto gap2_offset = next_idam_bytes + 1 + 4 + 2;
			auto has_gap2 = data.size() >= gap2_offset;
			auto has_gap3_4b = data.size() >= normal_bytes;
			auto remove_gap2 = false;
			auto remove_gap3_4b = false;

			// Check IDAM bit alignment and value, as AnglaisCollege\track00.0.raw has rogue FE junk on cyls 22+26
			if (has_gap2)
				remove_gap2 = next_idam_align != 0 || data[next_idam_bytes] != 0xfe || test_remove_gap2(data, gap2_offset);

			if (has_gap3_4b)
			{
				if (final_sector)
					remove_gap3_4b = test_remove_gap4b(data, normal_bytes);
				else
					remove_gap3_4b = test_remove_gap3(data, normal_bytes, sector.gap3);
			}

			if (opt.gaps != GAPS_ALL)
			{
				if (has_gap2 && remove_gap2)
				{
					if (opt.debug) util::cout << "removing gap2 data\n";
					data.resize(next_idam_bytes - ((sector.encoding == Encoding::MFM) ? 3 : 0));
				}
				else if (has_gap2)
				{
					if (opt.debug) util::cout << "skipping gap2 removal\n";
				}

				if (has_gap3_4b && remove_gap3_4b && (!has_gap2 || remove_gap2))
				{
					if (!final_sector)
					{
						if (opt.debug) util::cout << "removing gap3 data\n";
						data.resize(sector.size());
					}
					else
					{
						if (opt.debug) util::cout << "removing gap4b data\n";
						data.resize(sector.size());
					}
				}
			}

			// If it's an 8K sector, attempt to validate any embedded checksum
			auto chk8k_method = CHK8K_UNKNOWN;
			if (sector.is_8k_sector())
			{
				chk8k_method = Get8KChecksumMethod(data.data(), data.size());

				int chk_len = 0;
				if (opt.debug) util::cout << "chk8k_method = " << Get8KChecksumMethodName(chk8k_method, chk_len) << '\n';
			}

			sector.add(std::move(data), bad_crc, dam);

			// If the data is good there's no need to search for more data fields
			if (!bad_crc || chk8k_method >= CHK8K_FOUND)
				break;
		}
	}

	trackdata.add(std::move(track));
}

void scan_flux_mfm_fm (TrackData &trackdata, DataRate last_datarate)
{
	// Set the datarate scanning order, with the last successful rate first (and its duplicate removed)
	std::vector<DataRate> datarates = { last_datarate, DataRate::_250K, DataRate::_500K, DataRate::_300K, DataRate::_1M };
	datarates.erase(std::next(std::find(datarates.rbegin(), datarates.rend(), last_datarate)).base());

	// Loop through datarates until we find one that yields sectors
	for (auto datarate : datarates)
	{
		// Scale the flux values to simulate motor speed variation
		for (auto flux_scale : { 100, 95, 105 })
		{
			FluxDecoder decoder(trackdata.flux(), ::bitcell_ns(datarate), flux_scale);
			BitBuffer bitbuf(datarate, decoder);

			trackdata.add(std::move(bitbuf));
			scan_bitstream_mfm_fm(trackdata);
			auto &track = trackdata.track();

			// Stop if there's nothing to fix or motor wobble is disabled
			if (!track.has_data_error() || opt.nowobble)
				break;
		}

		// If we found something there's no need to check other densities
		if (!trackdata.track().empty())
			break;
	}
}

bool generate_special (TrackData &trackdata)
{
	auto track{ trackdata.track() };
	int weak_offset{ 0 }, weak_size{ 0 };

	// Special formats have special conversions
	if (IsEmptyTrack(track))
		trackdata.add(GenerateEmptyTrack(trackdata.cylhead, track));
	else if (IsKBI19Track(track))
		trackdata.add(GenerateKBI19Track(trackdata.cylhead, track));
	else if (IsSystem24Track(track))
		trackdata.add(GenerateSystem24Track(trackdata.cylhead, track));
	else if (IsSpectrumSpeedlockTrack(track, weak_offset, weak_size))
		trackdata.add(GenerateSpectrumSpeedlockTrack(trackdata.cylhead, track, weak_offset, weak_size));
	else if (IsCpcSpeedlockTrack(track, weak_offset, weak_size))
		trackdata.add(GenerateCpcSpeedlockTrack(trackdata.cylhead, track, weak_offset, weak_size));
	else if (IsRainbowArtsTrack(track, weak_offset, weak_size))
		trackdata.add(GenerateRainbowArtsTrack(trackdata.cylhead, track, weak_offset, weak_size));
	else if (IsKBI10Track(track, weak_offset, weak_size))
		trackdata.add(GenerateKBI10Track(trackdata.cylhead, track, weak_offset, weak_size));
	else if (IsLogoProfTrack(track))
		trackdata.add(GenerateLogoProfTrack(trackdata.cylhead, track));
	else if (IsOperaSoftTrack(track))
		trackdata.add(GenerateOperaSoftTrack(trackdata.cylhead, track));
	else
		return false;

	return true;
}

void generate_bitstream (TrackData &trackdata)
{
	assert(trackdata.has_track());

	// Special formats have special conversions
	if (generate_special(trackdata))
	{
		// Fail if we've encountered a flux-only special format, as converting
		// it to bitstream is unlikely to give a working track.
		if (!trackdata.has_bitstream())
			throw util::exception(trackdata.cylhead, " has no suitable bitstream representation");
	}
	else
	{
		throw util::exception("track to bitstream conversion not yet implemented for ", trackdata.cylhead);
	}
}

void generate_flux (TrackData &trackdata)
{
	uint8_t last_bit{0}, curr_bit{0};
	auto &bitbuf = trackdata.bitstream();
	auto ns_per_bitcell = bitcell_ns(bitbuf.datarate);
	bitbuf.seek(0);

	uint32_t flux_time{0};
	std::vector<uint32_t> flux_times{};
	flux_times.reserve(bitbuf.size());

	while (!bitbuf.wrapped())
	{
		auto next_bit{bitbuf.read1()};

		flux_time += ns_per_bitcell;
		if (curr_bit)
		{
			if (trackdata.cylhead.cyl < 40)
			{
				flux_times.push_back(flux_time);
				flux_time = 0;
			}
			else
			{
				auto pre_comp_ns{(last_bit == next_bit) ? 0 : (last_bit ? +240 : -240)};
				flux_times.push_back(flux_time + pre_comp_ns);
				flux_time = 0 - pre_comp_ns;
			}
		}

		last_bit = curr_bit;
		curr_bit = next_bit;
	}

	trackdata.add(FluxData({flux_times}));
}

/*
 * Agat 840K MFM format.  Agat was a family of Apple II workalikes
 * produced by Soviet Union in 1980's, this format is unique to them.
 *
 * Via https://github.com/sintech/AGAT/blob/master/docs/agat-840k-format.txt
 * and http://www.torlus.com/floppy/forum/viewtopic.php?f=19&t=1385
 */

void scan_bitstream_agat (TrackData &trackdata)
{
	Track track;
	Data block;
	uint64_t dword = 0;
	uint16_t stored_cksum = 0, cksum = 0;
	std::vector<std::pair<int, Encoding>> data_fields;

	auto &bitbuf = trackdata.bitstream();
	bitbuf.seek(0);
	bitbuf.encoding = Encoding::MFM;
	track.tracklen = bitbuf.track_bitsize();

	while (!bitbuf.wrapped())
	{
		// Give up if no headers were found in the first revolution
		if (!track.size() && bitbuf.tell() > track.tracklen)
			break;

		dword = (dword << 1) | bitbuf.read1();
		if (opt.debug && 1)
		util::cout << util::fmt ("  s_b_agat %016lx c:h %d:%d at %d\n",
			dword, trackdata.cylhead.cyl, trackdata.cylhead.head, bitbuf.tell());

		// MFM encoded address field prologue = 0x49111444; data field prologue = 0x14444911
		switch (dword & 0x1ffffffff)
		{
			case 0x89245555:	// 0100010010010010 0 0101010101010101 = MFM-encoded 0xa4, 2 us gap, 0xff
			case 0x44922d55:	// 0100010010010010 0 0101 10101010101 (variant)
			case 0x44905555:	// 01000100100100 0 0 0101010101010101 produced by agath-aim-to-hfe.pl
				if (opt.debug && 1) util::cout << "  s_b_agat found sync at " << bitbuf.tell() << "\n";
				break;

			default:
				continue;
		}

		auto am = bitbuf.read_byte() << 8;
		auto am_offset = bitbuf.tell();

		am |= bitbuf.read_byte();

		switch (am)
		{
			case 0x956a:	// IDAM
			{
				// volume, track, sector, epilogue (= 0x5a)
				std::array<uint8_t, 4> id;
				bitbuf.read(id);

				if (id[3] == 0x5a)
				{
					Sector s(bitbuf.datarate, bitbuf.encoding, Header(trackdata.cylhead, id[2], SizeToCode(256)));
					s.offset = bitbuf.track_offset(am_offset);

					if (opt.debug) util::cout << "* IDAM (id=" << id[2] << ") at offset " << am_offset << " (" << s.offset << ")\n";
					track.add(std::move(s));
				}
				else
				{
					Message(msgWarning, "unknown %s address mark epilogue (%02X) at offset %u on %s", to_string(bitbuf.encoding).c_str(), id[3], am_offset, CH(trackdata.cylhead.cyl, trackdata.cylhead.head));
				}
				break;
			}

			case 0x6a95:
			{
				if (opt.debug) util::cout << "* DAM (am=" << am << ") at offset " << am_offset << " (" << bitbuf.track_offset(am_offset) << ")\n";
				data_fields.push_back(std::make_pair(am_offset, bitbuf.encoding));
				break;
			}

			default:
				Message(msgWarning, "unknown %s address mark (%04X) at offset %u on %s", to_string(bitbuf.encoding).c_str(), am, am_offset, CH(trackdata.cylhead.cyl, trackdata.cylhead.head));
				break;
		}
	}

	// Process each sector header to look for an associated data field
	for (auto it = track.begin(); it != track.end(); ++it)
	{
		auto &sector = *it;
		auto final_sector = std::next(it) == track.end();

		auto shift = 4;
		auto gap2_size = 5;	// gap2 size in MFM bytes
		auto min_distance = ((1 + 4) << shift) + (gap2_size << 4);			// AM, ID, gap2
		auto max_distance = ((1 + 4) << shift) + ((40 + gap2_size) << 4);	// 1=AM, 4=ID, 21+gap2=max WD177x offset

		if (opt.debug) util::cout << "Finding " << trackdata.cylhead << " sector " << sector.header.sector << ":\n";

		for (auto itData = data_fields.begin(); itData != data_fields.end(); ++itData)
		{
			const auto &dam_offset = itData->first;
			auto itDataNext = (std::next(itData) == data_fields.end()) ? data_fields.begin() : std::next(itData);

			// Determine distance from header to data field, taking care of track wrapping
			auto dam_track_offset = bitbuf.track_offset(dam_offset);
			auto distance = ((dam_track_offset < sector.offset) ? track.tracklen : 0) + dam_track_offset - sector.offset;

			// Reject if the data field is too close or too far away
			if (distance < min_distance || distance > max_distance)
				continue;

			// If there's a splice between IDAM and DAM the track was probably modified
#if 0 // disabled until header/data matching enhancements are complete
			if (bitbuf.sync_lost(sector.offset, dam_offset))
				track.modified = true;
#endif
			bitbuf.seek(dam_offset);
			bitbuf.encoding = Encoding::MFM;

			auto dam = bitbuf.read_byte();

			// Determine the offset and distance to the next IDAM, taking care of track wrap if it's the final sector
			auto next_idam_offset = final_sector ? track.begin()->offset : std::next(it)->offset;
			auto next_idam_distance = ((next_idam_offset < dam_track_offset) ? track.tracklen : 0) + next_idam_offset - dam_track_offset;
			auto next_idam_bytes = (next_idam_distance >> shift) - 1;	// -1 due to DAM being read above

			// Determine the bit offset and distance to the next DAM
			auto next_dam_offset = itDataNext->first;
			auto next_dam_distance = ((next_dam_offset < dam_offset) ? bitbuf.size() : 0) + next_dam_offset - dam_offset;
			auto next_dam_bytes = (next_dam_distance >> shift) - 1;		// -1 due to DAM being read above

			// Attempt to read gap2 from non-final sectors, unless we're asked not to
			auto read_gap2 = (opt.gap2 != 0);

			// Calculate the extent of the current data field, up to the next header or data field (depending if gap2 is required)
			auto extent_bytes = read_gap2 ? next_dam_bytes : next_idam_bytes;
			if (extent_bytes >= 22) extent_bytes -= 22;	// remove AAAA.. XXX

			auto normal_bytes = sector.size() + 2;					// data size + CRC bytes
			auto data_bytes = std::min(normal_bytes, extent_bytes);	// data size needed to check CRC

			// Calculate bytes remaining in the data in current data encoding
			auto avail_bytes = bitbuf.remaining() >> shift;

			// Ignore truncated copies, unless it's the only copy we have
			if (avail_bytes < normal_bytes)
			{
				// If we've already got a copy, ignore the truncated version
				if (sector.copies() && (!sector.is_8k_sector() || avail_bytes < 0x1802))	// ToDo: fix nasty check
				{
					if (opt.debug) util::cout << "ignoring truncated sector copy\n";
					continue;
				}

				if (opt.debug) util::cout << "using truncated sector data as only copy\n";
			}

			// Read the full data field and check its CRC
			Data data(data_bytes);
			bitbuf.read(data);
			cksum = 0;
			stored_cksum = data[256];

			// Truncate at the extent size, unless we're asked to keep overlapping sectors
			if (!opt.keepoverlap && extent_bytes < sector.size())
				data.resize(extent_bytes);
			else if (data.size() > sector.size())
				data.resize(sector.size());

			for (auto byte = 0; byte < 256; byte++) {
				if (cksum > 255) { cksum++; cksum &= 255; }
				cksum += data[byte];
			}
			cksum &= 255;

			if (opt.debug) util::cout << util::fmt ("cksum s %d disk:calc %02x:%02x distance %d (min %d max %d)\n",
				sector.header.sector, stored_cksum, cksum, distance, min_distance, max_distance);
			bool bad_crc = (stored_cksum != cksum);

			sector.add(std::move(data), bad_crc, dam);

			// If the data is good there's no need to search for more data fields
			if (!bad_crc)
				break;
		}
	}

	trackdata.add(std::move(track));
}

void scan_flux_agat (TrackData &trackdata)
{
	FluxDecoder decoder(trackdata.flux(), ::bitcell_ns(DataRate::_250K), opt.scale);
	BitBuffer bitbuf(DataRate::_250K, decoder);

	trackdata.add(std::move(bitbuf));
}
