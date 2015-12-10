// UDI = Ultra Disk Image for Spectrum:
//  http://scratchpad.wikia.com/wiki/Spectrum_emulator_file_format:_udi

#include "SAMdisk.h"

#define UDI_SIGNATURE			"UDI!"
//const int MAX_UDI_TRACK_SIZE = 8192;

typedef struct
{
	uint8_t abSignature[4];		// Signature "UDI!"
	uint8_t abSize[4];			// FileSize-4 (last 4 bytes is checksum)
	uint8_t bVersion;			// Version (current version is 00)
	uint8_t bMaxCyl;			// Max number of cylinder, 0x4F for 80 cylinders on disk
	uint8_t bMaxHead;			// Max number of header (side), 0x00 SingleSided  0x01 DoubleSided  0x02..0xFF reserved
	uint8_t bUnused;			// Unused (should be 00) (see note)
	uint8_t abExtHdl[4];		// EXTHDL - Extended header size (should be 00)
} UDI_HEADER;

typedef struct
{
	uint8_t bFormat;				// 0x00 - MFM only  (native TR-DOS format), other values are forbidden
	uint8_t abLength[2];			// tlen - raw track size in bytes (usually 6250 bytes), (raw size is size of data, GAPs, index data, spaces ...)
} UDI_TRACK;


bool ReadUDI (MemFile &/*file*/, std::shared_ptr<Disk> &/*disk*/)
{
	throw std::logic_error("not implemented");

#if 0
	UDI_HEADER uh;
	if (!file.rewind() || !file.read(&uh, sizeof(uh)))
		return false;
	else if (memcmp(&uh.abSignature, UDI_SIGNATURE, 3))	// "UDI"
		return false;

	// The documented formats have '!' as the 4th character in the signature
	if (uh.abSignature[3] != '!')
		throw util::exception("unknown old-format UDI image");

	uint8_t abCRC[4];
	auto uSize = (uh.abSize[3] << 24) | (uh.abSize[2] << 16) | (uh.abSize[1] << 8) | uh.abSize[0];

	if (file.size() != uSize + 4)
		Message(msgWarning, "file size (%u) doesn't match header size field (%u)", file.size(), uSize + 4);
	else if (file.seek(uSize) && file.read(abCRC, sizeof(abCRC), 1))
	{
		uint32_t dwCrcFile = (abCRC[3] << 24) | (abCRC[2] << 16) | (abCRC[1] << 8) | abCRC[0];
		uint32_t dwCrc = crc32(crc32(0L, Z_NULL, 0), file.data().data(), file.size() - 4);

		if (dwCrc != dwCrcFile)
			Message(msgWarning, "invalid file CRC");

		// Seek back to after header
		file.seek(sizeof(uh));
	}

	int cyls = uh.bMaxCyl + 1;
	int heads = uh.bMaxHead + 1;

	ValidateGeometry(cyls, heads, 1);

	MEMORY mem(MAX_UDI_TRACK_SIZE), memClock((MAX_UDI_TRACK_SIZE + 7) >> 3);

	for (uint8_t cyl = 0; cyl < cyls; ++cyl)
	{
		for (uint8_t head = 0; head < heads; ++head)
		{
			CylHead cylhead(cyl, head);

			UDI_TRACK ut;
			if (!file.read(&ut, sizeof(ut)))
				throw util::exception("short file reading header on ", cylhead);

			unsigned tlen = (ut.abLength[1] << 8) | ut.abLength[0];
			if (tlen > MAX_UDI_TRACK_SIZE || !file.read(mem, tlen))
				throw util::exception("track size (", tlen, ") too big on ", cylhead);

			unsigned ctlen = (tlen + 7) >> 3;
			if (!file.read(memClock, ctlen))
				throw util::exception("short file reading clock bits from ", cylhead);

			// Accept only MFM (0x00) and FM (0x01) tracks until 1.1 has been finalised
			if (ut.bFormat >= 0x02)
				throw util::exception("unknown track type (", ut.bFormat, ") on ", cylhead);

			PTRACK pt = pd_->GetTrack(cyl, head);
			pt->encrate = ((ut.bFormat == 0x01) ? FD_OPTION_FM : FD_OPTION_MFM) |
				((tlen > 6400) ? FD_RATE_500K : FD_RATE_250K);
			pt->tracklen = tlen;
			pt->sectors = 0;

			// Skip blank tracks
			if (!tlen)
				continue;

			// Walk the track array
			for (unsigned u = 0, v; u < tlen - 1; u++)
			{
				// Skip unless we've got an ID address mark with missing clock bit
				if (mem[u] != 0xa1 || mem[u + 1] != 0xfe || !(memClock[u >> 3] & (1 << (u & 7))))
					continue;

				// Check there's enough ID data, and that the CRC is valid
				if (tlen - u < 8 || CRC16(mem + u - 2, 10) != 0)
					continue;

				// Add the new sector to the track, and set the ID header with what we've found
				PSECTOR ps = &pt->sector[pt->sectors++];
				ps->cyl = mem[u + 2];
				ps->head = mem[u + 3];
				ps->sector = mem[u + 4];
				ps->size = mem[u + 5];
				ps->flags = SF_NODATA;	// assume we won't find a data field
				ps->offset = u + 1;		// store IDAM offset

				// Calculate the expected data size, and assume we'll have it all
				unsigned uRealSize = SectorSize(ps->size);
				unsigned uDataSize = uRealSize;

				// Ignore 28 bytes from the end of the CRC before looking for data
				// The DAM must be found within 43 bytes of the CRC
//				for (v = u+8+30 ; v < u+8+60 ; v++)
				for (v = u + 8 + 28; v < u + 8 + 43; v++)
				{
					// As before, check for a data address mark (normal or deleted) with missing clock bit
					if (mem[v] != 0xa1 || (mem[v + 1] != 0xfb && mem[v + 1] != 0xf8) || !(memClock[v >> 3] & (1 << (v & 7))))
						continue;

					// Clear the no-data flag, and indicate if this is a deleted data address mark
					ps->flags &= ~SF_NODATA;
					if (mem[v + 1] == 0xf8) ps->flags |= SF_DELETED;

					break;
				}

				// If data was found, extract it
				if (!ps->IsNoData())
				{
					// Check if the block is short or there's a CRC error
					if (v + 2 + uDataSize + 2 >= tlen || CRC16(mem + v - 2, 3 + 1 + uDataSize + 2) != 0)
					{
						// Flag a CRC error, and calculate the available data size
						ps->flags |= SF_DATACRC;
						uDataSize = std::min(uDataSize, tlen - 2 - v - 1);
					}

					// Extract the data field
					ps->apbData[0] = new uint8_t[uRealSize];
					memcpy(ps->apbData[0], mem + v + 2, uDataSize);
					memset(ps->apbData[0] + uDataSize, 0, uRealSize - uDataSize);
				}
			}
		}
	}

	pd_->strType = "UDI";
	return true;
#endif
}
