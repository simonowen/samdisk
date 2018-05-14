// Interchangeable Preservation Format from the Software Preservation Society:
//  http://www.softpres.org/

#include "SAMdisk.h"
#include "BitstreamDecoder.h"

// ToDo:
// - wrap CAPSImg library in class wrapper for thrown exceptions
// - add image meta data to disk container
// - remove relative path from CapsLibAll.h header
// - consider deferring track locking and processing?

typedef struct
{
	uint8_t abSignature[4];		// "CAPS" signature
} IPF_HEADER;


#ifdef HAVE_CAPSIMAGE

#define __cdecl
#include "caps/CapsLibAll.h"

#ifdef __APPLE__
extern "C" SDWORD __cdecl CAPSGetVersionInfo(PVOID pversioninfo, UDWORD flag) __attribute__((weak_import));
#endif

static std::string error_string (SDWORD ret_code)
{
	switch (ret_code)
	{
	case imgeOk:				return "imgeOk";
	case imgeUnsupported:		return "imgeUnsupported";
	case imgeGeneric:			return "imgeGeneric";
	case imgeOutOfRange:		return "imgeOutOfRange";
	case imgeReadOnly:			return "imgeReadOnly";
	case imgeOpen:				return "imgeOpen";
	case imgeType:				return "imgeType";
	case imgeShort:				return "imgeShort";
	case imgeTrackHeader:		return "imgeTrackHeader";
	case imgeTrackStream:		return "imgeTrackStream";
	case imgeTrackData:			return "imgeTrackData";
	case imgeDensityHeader:		return "imgeDensityHeader";
	case imgeDensityStream:		return "imgeDensityStream";
	case imgeDensityData:		return "imgeDensityData";
	case imgeIncompatible:		return "imgeIncompatible";
	case imgeUnsupportedType:	return "imgeUnsupportedType";
	case imgeBadBlockType:		return "imgeBadBlockType";
	case imgeBadBlockSize:		return "imgeBadBlockSize";
	case imgeBadDataStart:		return "imgeBadDataStart";
	case imgeBufferShort:		return "imgeBufferShort";
	}

	return util::fmt("%d", ret_code);
}

#endif // HAVE_CAPSIMAGE


bool ReadIPF (MemFile &file, std::shared_ptr<Disk> &disk)
{
	IPF_HEADER ih;
	if (!file.rewind() || !file.read(&ih, sizeof(ih)) || memcmp(&ih.abSignature, "CAPS", 4))
		return false;

#ifdef HAVE_CAPSIMAGE
#ifdef __APPLE__
	if (CAPSGetVersionInfo == NULL)
		throw util::exception("CAPSImage.framework is required for this image");
#endif
	if (!CheckLibrary("capsimg", "CAPSGetVersionInfo"))
#endif
		throw util::exception("capsimg library is required for this image");

#ifdef HAVE_CAPSIMAGE
	CapsVersionInfo vi = {};

	// Require version 5.1 for CAPSSetRevolution() on CT Raw images
	if (CAPSGetVersionInfo(&vi, 0) != imgeOk || vi.release < 5 || (vi.release == 5 && vi.revision < 1))
		throw util::exception("capsimg 5.1 or newer is required");

	auto ret = CAPSInit();
	if (ret != imgeOk)
		throw util::exception("capsimg initialisation failed (", error_string(ret), ")");

	auto id = CAPSAddImage();
	auto pb = const_cast<PUBYTE>(file.data().data());

	// Load the image from memory
	ret = CAPSLockImageMemory(id, pb, static_cast<UDWORD>(file.size()), DI_LOCK_MEMREF);
	if (ret != imgeOk)
	{
		CAPSUnlockImage(id);
		CAPSRemImage(id);
		CAPSExit();

		if (ret == imgeIncompatible)
			throw util::exception("failed to lock image, please upgrade capsimg");
		else
			throw util::exception("failed to lock image file (", error_string(ret), ")");
	}

	auto image_type = CAPSGetImageTypeMemory(pb, static_cast<UDWORD>(file.size()));

	CapsImageInfo cii {};
	if (CAPSGetImageInfo(&cii, id) != imgeOk)
	{
		CAPSUnlockImage(id);
		CAPSRemImage(id);
		CAPSExit();

		throw util::exception("failed to read image information");
	}

	if (cii.type != ciitFDD)
		throw util::exception("non-floppy images are unsupported");

	// Request bit-resolution for sizes, with changing weak sector data
	auto lock_flags = vi.flag & (DI_LOCK_OVLBIT | DI_LOCK_TRKBIT | DI_LOCK_UPDATEFD | DI_LOCK_TYPE |
								 DI_LOCK_DENVAR | DI_LOCK_DENNOISE | DI_LOCK_NOISE);

	size_t unformatted1 = 0;

	for (auto cyl = cii.mincylinder; cyl <= cii.maxcylinder; ++cyl)
	{
		for (auto head = cii.minhead; head <= cii.maxhead; ++head)
		{
			// Start with space for 5 revolutions of 250Kbps
			BitBuffer bitbuf(DataRate::_250K, Encoding::Unknown, 5);

			// If timing data is available we'll generate flux data instead
			FluxData flux_revs;
			uint32_t total_time = 0;

			// Default to 5 revolutions for CT Raw and 2 for IPF in case of weak sectors
			auto max_revs = (image_type == citCTRaw) ? CAPS_MTRS : 2;

			for (auto rev = 0; rev < max_revs; ++rev)
			{
				CapsTrackInfoT1 cti {};
				cti.type = 1;

				// Set the revolution within CT Raw images
				ret = CAPSSetRevolution(id, rev);

				// Lock to receive track data, which includes updating any weak areas
				if (ret == imgeOk)
					ret = CAPSLockTrack(reinterpret_cast<CapsTrackInfo*>(&cti), id, cyl, head, lock_flags);

				if (ret != imgeOk)
				{
					CAPSUnlockImage(id);
					CAPSRemImage(id);
					CAPSExit();

					if (ret == imgeIncompatible)
						throw util::exception("failed to lock track, please upgrade capsimg");

					Message(msgWarning, "failed to lock %s (%s)", CH(cyl, head), error_string(ret).c_str());
					break;
				}

				// Unformatted track?
				if ((cti.type & CTIT_MASK_TYPE) == ctitNoise)
				{
					if (head == 1) unformatted1++;
					break;
				}

				size_t tracklen_bits = (lock_flags & DI_LOCK_TRKBIT) ? cti.tracklen : cti.tracklen * 8;
				bitbuf.datarate = (tracklen_bits / 16 >= 0x2000) ? DataRate::_500K : DataRate::_250K;
				auto ns_per_bitcell = bitcell_ns(bitbuf.datarate);

				// Do we have timing data that we're allowed to use?
				if (cti.timelen > 0 && !opt.noflux)
				{
					std::vector<uint32_t> flux_times;
					flux_times.reserve(tracklen_bits);

					// Generate a revolution of flux data
					for (size_t i = 0; i < tracklen_bits; ++i)
					{
						uint8_t bit = (cti.trackbuf[i / 8] >> (7 - (i & 7))) & 1;

						if ((i >> 3) < cti.timelen)
							total_time += ns_per_bitcell * cti.timebuf[i >> 3] / 1000;
						else
							total_time += ns_per_bitcell;

						if (bit)
						{
							flux_times.push_back(total_time);
							total_time = 0;
						}
					}

					// Add the new revolution, keeping any left over time for the next one
					flux_revs.push_back(std::move(flux_times));
				}
				else
				{
					// Generate a revolution of bitstream data
					for (size_t i = 0; i < tracklen_bits; ++i)
					{
						uint8_t bit = (cti.trackbuf[i / 8] >> (7 - (i & 7))) & 1;
						bitbuf.add(bit);
					}

					// Mark the index hole
					bitbuf.add_index();
				}

				// A single revolution is enough for IPF tracks without weak areas
				if (image_type == citIPF && !(cti.type & CTIT_FLAG_FLAKEY))
					max_revs = 1;
			}

			CAPSUnlockTrack(id, cyl, head);

			// Add flux or bitstream data, depending on which we generated
			if (!flux_revs.empty())
				disk->write(CylHead(cyl, head), std::move(flux_revs));
			else
				disk->write(CylHead(cyl, head), std::move(bitbuf));
		}
	}

	CAPSUnlockImage(id);
	CAPSRemImage(id);
	CAPSExit();

	disk->metadata["library"] = util::fmt("%lu.%lu", vi.release, vi.revision);

	if (cii.release != 0)
		disk->metadata["release"] = util::fmt("%lu", cii.release);

	if (cii.platform[0] != ciipNA)
	{
		std::string platforms;
		for (auto i = 0; i < CAPS_MAXPLATFORM; ++i)
		{
			if (cii.platform[i] != ciipNA)
			{
				if (!platforms.empty()) platforms += ", ";
				platforms += CAPSGetPlatformName(cii.platform[i]);
			}
		}
		disk->metadata["platform"] = platforms;
	}

	// Shrink to just head 0 if all tracks on head 1 are unformatted
	if (opt.fix != 0 && unformatted1 == (cii.maxcylinder - cii.mincylinder + 1))
		disk->resize(disk->cyls(), 1);

	switch (image_type)
	{
		case citIPF:		disk->strType = "IPF"; break;
		case citCTRaw:		disk->strType = "CTRaw"; break;
		case citKFStream:	disk->strType = "KFStream"; break;
		case citDraft:		disk->strType = "Draft"; break;
		default:			disk->strType = "CAPSImage"; break;
	}

	return true;
#else
	(void)disk; // unused
#endif // HAVE_CAPSIMAGE
}
