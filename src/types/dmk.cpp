// David Keil's TRS-80 on-disk format:
//  http://fjkraan.home.xs4all.nl/comp/trs80-4p/dmkeilImages/trstech.htm

#include "SAMdisk.h"

const int DMK_MAX_TRACK_LENGTH = 0x2940;	// value from specification (no high-density support?)

typedef struct
{
	uint8_t bWriteProtect;		// 0xff if disk is write-protected, 0x00 otherwise
	uint8_t bTracks;			// Max number of cylinder, 0x4F for 80 cylinders on disk
	uint8_t abTrackLength[2];	// LSB/MSB of track length
	uint8_t bFlags;				// Flags
	uint8_t abReserved[7];		// Reserved
	uint8_t abRealSig[4];		// Zero for disk image, 0x12345678 if used to access real floppy
} DMK_HEADER;

typedef struct
{
	uint8_t abIDAM[128];		// 64 entries as LSB/MSB:  b15=double-density, b14=undefined, rest=IDAM-offset
} DMK_TRACK;

#if 0

static unsigned uPos, uTrackLen, uStep = 1;
static uint8_t *pbTrack;

static int ReadWord ()
{
	if (uPos >= uTrackLen)
		return -1;

	int nData = pbTrack[uPos];
	uPos += uStep;
	return nData;
}

static int ReadByte()
{
	int n = ReadWord();
	return (n < 0) ? n : (n & 0xff);
}

static int ReadBytes (uint8_t *pb_, int n_)
{
	for (int i = 0; i < n_; i++)
	{
		int nByte = ReadByte();
		if (nByte < 0)
			return i;

		pb_[i] = static_cast<uint8_t>(nByte);
	}

	return n_;
}

static void ReadSector (PTRACK pt_, int nSector_)
{
	PSECTOR ps = &pt_->sector[nSector_], pd = NULL;
	unsigned uData = SectorSize(ps->size);
	int iData;

	// The data step size depends on the data encoding
	uStep = ps->IsFM() ? 2 : 1;

	// We start looking for data after the header CRC, which is 7 bytes after the IDAM
	unsigned uHeaderPos = ps->offset + 7, uDataPos = 0;

	// The maximum offset from the ID header depends on the gap2 size
	unsigned uMaxOffset = 21 + pt_->GetGap2Size();

	for (iData = nSector_ + 1; iData != nSector_; iData++)
	{
		if (iData >= pt_->sectors) iData -= pt_->sectors;

		uDataPos = pt_->sector[iData].offset;
		unsigned uOffset = (uDataPos >= uHeaderPos) ? uDataPos - uHeaderPos : (pt_->tracklen + uDataPos) - uHeaderPos;
		// Note: FM gaps are half the length but each byte is twice the size, so the calculation still works

		// The controller misses anything too close to the end of header
		if (uOffset < 28)
			continue;

		// If what we've found is too far from the header or not a data field, give up
		if (uOffset > uMaxOffset || pt_->sector[iData].IsNoData())
			break;

		// Data field found for the header
		pd = &pt_->sector[iData];
		break;
	}

	// Is there a data field to read?
	if (!pd)
		return;

	// Sector now has data, data field has been used
	ps->flags &= ~SF_NODATA;
	pd->flags |= SF_INUSE;

	// Determine the indices of what should be the next IDAM and DAM
	int iNextId = (iData + 1) % pt_->sectors;
	int iNextData = (iNextId + 1) % pt_->sectors;

	// Determine the extent index and its track position, subtracting any MFM syncsync+AM size (MFM)
	int iExtent = opt.gap2 ? iNextData : iNextId;
	unsigned uExtentPos = pt_->sector[iExtent].offset - (ps->IsFM() ? (1 * uStep) : 4/*A1A1A1xx*/);

	// Calculate the different and convert from MFM bits to data bytes
	unsigned uExtent = ((uExtentPos > uDataPos) ? uExtentPos - uDataPos : (pt_->tracklen + uExtentPos) - uDataPos);

	// For single (id+header=2) oversized sectors, include the full track
	if (pt_->sectors == 2 && uData > pt_->tracklen)
		uExtent = pt_->tracklen;

	// Correct for the encoding size
	uExtent /= uStep;

	// In read-track mode, include the full track in the first sector, and no other gaps
	if (opt.gaps == GAPS_TRACK)
		uExtent = !nSector_ ? pt_->tracklen : 0;

	uPos = pd->offset;	// DAM offset

	int nAM = ReadByte();
	if (nAM < 0) return;
	uint8_t bAM = static_cast<uint8_t>(nAM);
	ps->SetDAM(bAM);

	unsigned uMaxData = std::max(uData + 2, uExtent);
	uint8_t *pb = ps->apbData[0] = new BYTE[uMaxData];
	memset(pb, 0, uMaxData);

	ps->uData = ReadBytes(pb, uMaxData);

	CRC16 crc;
	if (ps->IsMFM()) crc.add("\xa1\xa1\xa1", 3);
	crc.add(bAM);
	crc.add(pb, uData + 2);

	// Unless we're told not to, truncate to the data extent if we have too much data
	if (!opt.keepoverlap && ps->uData > uExtent)
		ps->uData = uExtent;

	// If the data CRC was bad, flag it
	if (crc != 0)
		ps->flags |= SF_DATACRC;
}

static void ReadTrack (PTRACK pt_, DMK_HEADER *pdh_, DMK_TRACK *pdt_)
{
	uPos = 0;
	int nMFM = 0;

	for (int i = 0; i < sizeof(*pdt_) / 2; i++)
	{
		WORD wOffset = (pdt_->abIDAM[i * 2 + 1] << 8) | pdt_->abIDAM[i * 2];
		if (!wOffset)
			break;

		// Determine whether the sector is MFM or FM, the data stepping, and the track offset
		bool fMFM = !!(wOffset & 0x8000);
		uStep = (fMFM || (pdh_->bFlags & 0x40)) ? 1 : 2;

		// Position at and read the IDAM
		uPos = (wOffset & 0x3fff) - sizeof(*pdt_);
		if (uPos > uTrackLen)
		{
			Message(msgWarning, "invalid IDAM offset (%s) for %s", NumStr(wOffset & 0x3fff), CHS(pt_->cyl, pt_->head, i));
			break;
		}

		int nAM = ReadByte();
		if (nAM < 0) break;
		uint8_t bAM = static_cast<uint8_t>(nAM);

		// Reject anything other than the expected IDAM
		if (bAM != 0xfe)
		{
			Message(msgWarning, "invalid IDAM (%02X) for %s", bAM, CHS(pt_->cyl, pt_->head, i));
			continue;
		}

		// Add new sector for header field, and store the IDAM position
		PSECTOR ps = &pt_->sector[pt_->sectors++];
		ps->flags = fMFM ? 0 : SF_FM;
		ps->offset = uPos - uStep;
		nMFM += !!fMFM;

		// Initialise CRC (include A1A1A1 for MFM)
		CRC16 crc;
		if (ps->IsMFM()) crc.add("\xa1\xa1\xa1", 3);
		crc.add(bAM);

		crc.add(ps->cyl = static_cast<uint8_t>(ReadByte()));
		crc.add(ps->head = static_cast<uint8_t>(ReadByte()));
		crc.add(ps->sector = static_cast<uint8_t>(ReadByte()));
		crc.add(ps->size = static_cast<uint8_t>(ReadByte()));
		crc.add(static_cast<uint8_t>(ReadByte()));
		crc.add(static_cast<uint8_t>(ReadByte()));

		// ID CRC error?
		if (crc != 0)
		{
			// Ignore the header
			pt_->sectors--;
			break;
		}

		// No associated data field yet
		ps->flags |= SF_NODATA;

		// The controller misses anything too close to the end of header
		uPos += 28;

		// The maximum offset from the ID header depends on the gap2 size
		int nMaxOffset = 21 + pt_->GetGap2Size();

		DWORD dwAM = 0x00000000;

		// Search for a data field
		for (int j = 0; j < nMaxOffset; j++)
		{
			int n = ReadByte();
			if (n < 0) break;
			BYTE b = static_cast<BYTE>(n);
			dwAM = (dwAM << 8) | b;

			// MFM requires A1 A1 A1 before the DAM
			if (fMFM && (dwAM & 0xffffff00) != 0xa1a1a100)
				continue;

			// Found a data address mark?
			if ((b >= 0xf8 && b <= 0xfb) || b == 0xfd)
			{
				// Add new sector for the data field, and store the DAM position
				PSECTOR pd = &pt_->sector[pt_->sectors++];
				pd->offset = uPos - uStep;
				pd->flags = SF_NOID | (nMFM ? 0 : SF_FM);
				break;
			}

			// If there was no DAM match in MFM the data field is missing
			if (fMFM)
				break;
		}
	}

	// Attempt to read the data field from each header
	for (int i = 0; i < pt_->sectors; i++)
	{
		// If this is a header, attempt to read the data field
		if (pt_->sector[i].IsNoData())
			ReadSector(pt_, i);
	}

	// Remove any placeholder data fields
	for (int i = 0; i < pt_->sectors; i++)
	{
		if (pt_->sector[i].IsNoID())
			pt_->DeleteSector(i--);
	}

	// Finally, clean gaps unless we're required to keep them all
	if (opt.gaps != GAPS_ALL)
		pt_->CleanGaps();
}
#endif


bool ReadDMK (MemFile &file, std::shared_ptr<Disk> &disk)
{
	DMK_HEADER dh;
	if (!file.rewind() || !file.read(&dh, sizeof(dh)))
		return false;
	else if ((dh.bWriteProtect != 0x00 && dh.bWriteProtect != 0xff))
		return false;

//	bool fReadOnly = !!dh.bWriteProtect;
//	bool fIgnoreDensity = !!(dh.bFlags & 0x80);
//	bool fSingleDensity = !!(dh.bFlags & 0x40);
	bool fSingleSided = !!(dh.bFlags & 0x10);

	auto cyls = dh.bTracks;
	auto heads = fSingleSided ? 1 : 2;

	auto uTrackLen = (dh.abTrackLength[1] << 8) | dh.abTrackLength[0];

	auto uSize = static_cast<int>(sizeof(DMK_HEADER) + uTrackLen * cyls * heads);
	if (!uTrackLen || uTrackLen > DMK_MAX_TRACK_LENGTH || file.size() != uSize)
		return false;

	if (cyls > MAX_TRACKS)
	{
		Message(msgWarning, "ignoring tracks >= %u", MAX_TRACKS);
		cyls = MAX_TRACKS;
	}

	uTrackLen -= sizeof(DMK_TRACK);
	MEMORY mem(uTrackLen);
//	pbTrack = mem;

	for (auto cyl = 0; cyl < cyls; ++cyl)
	{
		for (auto head = 0; head < heads; ++head)
		{
			DMK_TRACK dt;
			if (!file.read(&dt, sizeof(dt)) || !file.read(mem, uTrackLen))
				throw util::exception("short file reading ", CH(cyl, head));

//			pt->tracklen = uTrackLen;
//			ReadTrack(pt, &dh, &dt);
		}
	}

	disk->strType = "DMK";
	return true;
}
