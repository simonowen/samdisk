// David Keil's TRS-80 on-disk format:
//  http://www.classiccmp.org/cpmarchives/trs80/mirrors/trs-80.com/early/www.trs-80.com/trs80-dm.htm

#include "SAMdisk.h"
#include "IBMPC.h"
#include "BitstreamTrackBuffer.h"

const int DMK_MAX_TRACK_LENGTH = 0x3fff;	// most images use 0x2940, a few have 0x29a0
const int DMK_TRACK_INDEX_SIZE = 0x80;

typedef struct
{
	uint8_t protect;		// 0xff=write-protected, 0x00=read-write
	uint8_t cyls;			// cylinder count
	uint16_t tracklen;		// track data length, including 0x80 bytes for IDAM index
	uint8_t flags;			// b7=ignore density, b6=single byte single density, b4=single sided
	uint8_t reserved[7];	// reserved for future use
	uint32_t realsig;		// 0=disk image, 0x12345678=real floppy access
} DMK_HEADER;


bool ReadDMK (MemFile &file, std::shared_ptr<Disk> &disk)
{
	DMK_HEADER dh{};
	if (!file.rewind() || !file.read(&dh, sizeof(dh)))
		return false;
	else if ((dh.protect != 0x00 && dh.protect != 0xff))
		return false;

	bool ignore_density = (dh.flags & 0x80) != 0;
	bool single_density = (dh.flags & 0x40) != 0;
	bool single_sided = (dh.flags & 0x10) != 0;

	if (ignore_density)
		throw util::exception("DMK ignore density flag is not currently supported");
	else if (util::letoh(dh.realsig) == 0x12345678)
		throw util::exception("DMK real-disk-specification images contain no data");

	auto cyls = dh.cyls;
	auto heads = single_sided ? 1 : 2;
	int tracklen = util::letoh(dh.tracklen);

	auto total_size = static_cast<int>(sizeof(DMK_HEADER) + tracklen * cyls * heads);
	if (!tracklen || tracklen > DMK_MAX_TRACK_LENGTH || file.size() != total_size)
		return false;
	tracklen -= DMK_TRACK_INDEX_SIZE;

	for (auto cyl = 0; cyl < cyls; ++cyl)
	{
		for (auto head = 0; head < heads; ++head)
		{
			std::vector<uint16_t> index(64);
			std::vector<uint8_t> data(tracklen);

			if (!file.read(index) || !file.read(data))
				throw util::exception("short file reading ", CH(cyl, head));

			std::transform(index.begin(), index.end(), index.begin(),
				[](uint16_t w) { return util::letoh(w); });

			int idx_idam = 0;
			int pos = 0;
			int last_pos = pos;

			int current_idam_pos = 0;
			int next_idam_pos = (index[0] & 0x3fff) - DMK_TRACK_INDEX_SIZE;

			auto next_idam_encoding =
				(!index[0] || (index[0] & 0x8000)) ? Encoding::MFM : Encoding::FM;
			auto current_idam_encoding = next_idam_encoding;

			int fm_step = single_density ? 1 : 2;
			int step = (current_idam_encoding == Encoding::MFM) ? 1 : fm_step;

			bool found_iam = false;
			bool found_dam = false;

			BitstreamTrackBuffer bitbuf(DataRate::_250K, current_idam_encoding);
			if (opt.debug)
				util::cout << "DMK: " << CylHead(cyl, head) << "\n";

			while (pos < tracklen)
			{
				auto b = data[pos];
				bool is_am = false;

				if (next_idam_pos > 0 && pos >= next_idam_pos)
				{
					// Force sync in case of odd/even mismatch.
					pos = next_idam_pos;
					b = data[pos];

					if (opt.debug)
					{
						util::cout << next_idam_encoding << " IDAM (" <<
							data[pos + 3 * ((next_idam_encoding == Encoding::MFM) ? 1 : fm_step)] <<
							") at offset " << pos << "\n";
					}

					assert(b == IBM_IDAM);
					is_am = true;
				}
				else if (!found_iam && b == IBM_IAM && current_idam_pos == 0)
				{
					if (opt.debug)
						util::cout << next_idam_encoding << " IAM at offset " << pos << "\n";

					is_am = found_iam = true;
				}
				else if (!found_dam && b >= 0xf8 && b <= 0xfd)
				{
					auto min_distance = ((current_idam_encoding == Encoding::MFM) ? 14 : 7) * step;
					auto max_distance = min_distance + ((current_idam_encoding == Encoding::MFM) ? 43 : 30) * step;
					auto idam_distance = current_idam_pos ? (pos - current_idam_pos) : 0;

					if (idam_distance >= min_distance && idam_distance <= max_distance)
					{
						if (opt.debug)
							util::cout << current_idam_encoding << " DAM (" << b << ") at offset " << pos << "\n";

						is_am = found_dam = true;
					}
				}

				if (is_am)
				{
					auto am_encoding = (b == IBM_IDAM) ? next_idam_encoding : bitbuf.encoding();
					int rewind = (am_encoding == Encoding::MFM) ? (8 + 3) : (6 * 2);

					while (last_pos < (pos - rewind))
					{
						bitbuf.addByte(data[last_pos]);
						last_pos += step;
					}

					bitbuf.setEncoding(am_encoding);
					bitbuf.addBlock(0x00, (am_encoding == Encoding::MFM) ? 8 : 6);
					bitbuf.addAM(b);

					step = (am_encoding == Encoding::MFM) ? 1 : fm_step;
					pos += step;
					last_pos = pos;

					if (b == IBM_IDAM)
					{
						current_idam_pos = next_idam_pos;
						current_idam_encoding = next_idam_encoding;
						found_dam = false;

						auto idam_entry = index[++idx_idam];
						next_idam_pos = (idam_entry & 0x3fff) - 0x80;
						next_idam_encoding = (!idam_entry || (idam_entry & 0x8000)) ? Encoding::MFM : Encoding::FM;
					}
					else if (am_encoding == Encoding::FM && b == IBM_DAM_RX02)
					{
						bitbuf.setEncoding(Encoding::MFM);
						step = 1;
					}

					continue;
				}

				pos += step;
			}

			while (last_pos < pos)
			{
				bitbuf.addByte(data[last_pos]);
				last_pos += step;
			}

			disk->write(CylHead(cyl, head), std::move(bitbuf.buffer()));
		}
	}

	disk->metadata["protect"] = dh.protect ? "read-only" : "read-write";

	disk->strType = "DMK";
	return true;
}
