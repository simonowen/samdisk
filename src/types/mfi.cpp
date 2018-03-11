// MAME/MESS floppy image
//
// https://github.com/mamedev/mame/blob/master/src/lib/formats/mfi_dsk.cpp

#include "SAMdisk.h"
#include "DemandDisk.h"
#include "BitstreamDecoder.h"

#ifdef HAVE_ZLIB
#include "zlib.h"
#endif

// Note: all values are little-endian
typedef struct
{
	char signature[16];		// "MESSFLOPPYIMAGE"
	uint32_t cyl_count, head_count;
	uint32_t form_factor, variant;
} MFI_FILE_HEADER;

typedef struct
{
	uint32_t offset, compressed_size, uncompressed_size, write_splice;
} MFI_TRACK_HEADER;

//! Floppy format data
enum {
	TIME_MASK = 0x0fffffff,
	MG_MASK   = 0xf0000000,
	MG_SHIFT  = 28, //!< Bitshift constant for magnetic orientation data
	MG_A      = (0 << MG_SHIFT),    //!< - 0, MG_A -> Magnetic orientation A
	MG_B      = (1 << MG_SHIFT),    //!< - 1, MG_B -> Magnetic orientation B
	MG_N      = (2 << MG_SHIFT),    //!< - 2, MG_N -> Non-magnetized zone (neutral)
	MG_D      = (3 << MG_SHIFT),    //!< - 3, MG_D -> Damaged zone, reads as neutral but cannot be changed by writing
	RESOLUTION_SHIFT = 30,
	CYLINDER_MASK = 0x3fffffff
};

//! Form factors
enum {
	FF_UNKNOWN  = 0x00000000, //!< Unknown, useful when converting
	FF_3        = 0x20202033, //!< "3   " 3 inch disk
	FF_35       = 0x20203533, //!< "35  " 3.5 inch disk
	FF_525      = 0x20353235, //!< "525 " 5.25 inch disk
	FF_8        = 0x20202038  //!< "8   " 8 inch disk
};

//! Variants
enum {
	SSSD  = 0x44535353, //!< "SSSD", Single-sided single-density
	SSDD  = 0x44445353, //!< "SSDD", Single-sided double-density
	SSQD  = 0x44515353, //!< "SSQD", Single-sided quad-density
	DSSD  = 0x44535344, //!< "DSSD", Double-sided single-density
	DSDD  = 0x44445344, //!< "DSDD", Double-sided double-density (720K in 3.5, 360K in 5.25)
	DSQD  = 0x44515344, //!< "DSQD", Double-sided quad-density (720K in 5.25, means DD+80 tracks)
	DSHD  = 0x44485344, //!< "DSHD", Double-sided high-density (1440K)
	DSED  = 0x44455344  //!< "DSED", Double-sided extra-density (2880K)
};


class MFIDisk final : public DemandDisk
{
public:
	void add_track_data (const CylHead &cylhead, std::vector<uint32_t> &&trackdata)
	{
		m_data[cylhead] = std::move(trackdata);
		extend(cylhead);
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

		uint32_t total_time = 0;
		for (auto time : data)
		{
			flux_times.push_back(time);
			total_time += time;
		}

		if (total_time != 200000000)
			throw util::exception("wrong total_time for ", cylhead, ": ", total_time);

		if (!flux_times.empty())
			flux_revs.push_back(std::move(flux_times));

// causes random crashes
//		m_data.erase(cylhead);

		return TrackData(cylhead, std::move(flux_revs));
	}

private:
	std::map<CylHead, std::vector<uint32_t>> m_data {};
	uint32_t m_tick_ns = 0;

};


bool ReadMFI (MemFile &file, std::shared_ptr<Disk> &disk)
{
	MFI_FILE_HEADER fh {};

	if (!file.rewind() || !file.read(&fh, sizeof(fh)))
		return false;

	if (memcmp(fh.signature, "MESSFLOPPYIMAGE", sizeof(fh.signature)))
		return false;

#ifndef HAVE_ZLIB
	throw util::exception("MFI disk images are not supported without ZLIB");
#else
	if ((fh.cyl_count >> RESOLUTION_SHIFT) > 0)
		throw util::exception("half- and quarter-track MFI images are not supported");

	if ((fh.cyl_count & CYLINDER_MASK) > 84 || fh.head_count > 2)
		return false;

	auto mfi_disk = std::make_shared<MFIDisk>();

	fh.cyl_count &= CYLINDER_MASK;

	for (unsigned int cyl=0; cyl < fh.cyl_count; cyl++)
	{
		for (unsigned int head=0; head != fh.head_count; head++)
		{
			MFI_TRACK_HEADER th;
			if (!file.read(&th, sizeof(th)))
				break;

			CylHead cylhead(cyl, head);

			Data compressed_data(util::letoh(th.compressed_size));
			std::vector<uint32_t> track_data(util::letoh(th.uncompressed_size) >> 2);

			auto o = file.tell();
			file.seek(util::letoh(th.offset));
			if (!file.read(compressed_data))
				throw util::exception("short file reading ", cylhead, " data");

			file.seek(o);

			uLongf size = util::letoh(th.uncompressed_size);
			int rc = uncompress((Bytef *)&track_data[0], &size, &compressed_data[0], util::letoh(th.compressed_size));
			if (rc != Z_OK)
			{
				util::cout << "decompress of " << cylhead << " failed, rc " << rc << "\n";
				util::cout << "sizes " << util::letoh(th.compressed_size) << " to " << util::letoh(th.uncompressed_size) << "\n";
				return false;
			}

			std::transform(track_data.begin(), track_data.end(), track_data.begin(),
				[](uint32_t c) -> uint32_t { return (util::letoh(c) & TIME_MASK); });

			mfi_disk->add_track_data(cylhead, std::move(track_data));
		}
	}

	mfi_disk->metadata["form_factor"] = util::fmt("%08X", fh.form_factor);
	mfi_disk->metadata["variant"] = util::fmt("%08X", fh.variant);

	mfi_disk->strType = "MFI";
	disk = mfi_disk;

	return true;
#endif
}
