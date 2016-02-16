// Interchangeable Preservation Format from the Software Preservation Society:
//  http://www.softpres.org/

#include "SAMdisk.h"
#include "DemandDisk.h"
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


class IPFDisk final : public DemandDisk
{
public:
	void add_track_data (const CylHead &cylhead, BitBuffer &&bitbuf)
	{
		m_trackdata.emplace(std::make_pair(cylhead, std::move(bitbuf)));
		extend(cylhead);
	}

protected:
	Track load (const CylHead &cylhead) override
	{
		auto ch = CylHead(cylhead.cyl * opt.step, cylhead.head);
		auto it = m_trackdata.find(ch);
		if (it != m_trackdata.end())
			return scan_bitstream(ch, it->second);

		// No data to decode!
		return Track();
	}

private:
	std::map<CylHead, BitBuffer> m_trackdata {};
};


#ifdef HAVE_CAPSIMAGE

#define __cdecl
#include "caps/CapsLibAll.h"

#ifdef _WIN32
#pragma comment(lib, "CAPSImg.lib")
#endif

#ifdef __APPLE__
extern "C" CapsLong CAPSGetVersionInfo(void *pversioninfo, CapsULong flag) __attribute__((weak_import));
#endif

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
		throw util::exception("capsimg initialisation failed (", ret, ")");

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
			throw util::exception("failed to lock image file (", ret, ")");
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
	auto lock_flags = vi.flag & (DI_LOCK_OVLBIT | DI_LOCK_TRKBIT | DI_LOCK_UPDATEFD | DI_LOCK_TYPE);

	auto ipf_disk = std::make_shared<IPFDisk>();
	size_t unformatted1 = 0;

	for (auto cyl = cii.mincylinder; cyl <= cii.maxcylinder; ++cyl)
	{
		for (auto head = cii.minhead; head <= cii.maxhead; ++head)
		{
			// Start with space for 5 revolutions of 250Kbps
			BitBuffer bitbuf(DataRate::_250K, 5);

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

					throw util::exception(util::fmt("failed to lock %s (%d)", CH(cyl, head), ret));
				}

				// Unformatted track?
				if ((cti.type & CTIT_MASK_TYPE) == ctitNoise)
				{
					if (head == 1) unformatted1++;
					break;
				}

				// Add the new revolution to the bit buffer, taking care of the reversed bit order
				size_t tracklen_bits = (lock_flags & DI_LOCK_TRKBIT) ? cti.tracklen : cti.tracklen * 8;
				for (size_t i = 0; i < tracklen_bits; ++i)
					bitbuf.add((cti.trackbuf[i / 8] >> (7 - (i & 7))) & 1);

				// Mark the index hole, and guess the data rate from the bit count
				bitbuf.index();
				bitbuf.datarate = (tracklen_bits / 16 >= 0x2000) ? DataRate::_500K : DataRate::_250K;

				// A single revolution is enough for IPF tracks without weak areas
				if (image_type == citIPF && !(cti.type & CTIT_FLAG_FLAKEY))
					max_revs = 1;
			}

			CAPSUnlockTrack(id, cyl, head);

			ipf_disk->add_track_data(CylHead(cyl, head), std::move(bitbuf));
		}
	}

	CAPSUnlockImage(id);
	CAPSRemImage(id);
	CAPSExit();

	if (cii.release != 0)
		ipf_disk->metadata["release"] = util::fmt("%lu", cii.release);

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
		ipf_disk->metadata["platform"] = platforms;
	}

	// Shrink to just head 0 if all tracks on head 1 are unformatted
	if (opt.fix != 0 && unformatted1 == (cii.maxcylinder - cii.mincylinder + 1))
		ipf_disk->resize(ipf_disk->cyls(), 1);

	switch (image_type)
	{
		case citIPF:		ipf_disk->strType = "IPF"; break;
		case citCTRaw:		ipf_disk->strType = "CTRaw"; break;
		case citKFStream:	ipf_disk->strType = "KFStream"; break;
		case citDraft:		ipf_disk->strType = "Draft"; break;
		default:			ipf_disk->strType = "CAPSImage"; break;
	}

	disk = ipf_disk;
	return true;
#else
	(void)disk; // unused
#endif // HAVE_CAPSIMAGE
}
