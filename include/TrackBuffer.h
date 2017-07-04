#ifndef TRACKBUFFER_H
#define TRACKBUFFER_H

#include "CRC16.h"

class TrackBuffer
{
	static const int GAP_FILL_BYTE = 0x4e;	// IBM System 34 gap filler

public:
	explicit TrackBuffer (Encoding encoding = Encoding::MFM);
	virtual ~TrackBuffer () = default;

	void setFM () { m_encoding = Encoding::FM; }
	void setMFM () { m_encoding = Encoding::MFM; }

	virtual void addBit (bool one) = 0;

	void addDataBit (bool one);
	void addByte (int byte);
	void addByteBits (int byte, int num_bits);
	void addByteWithClock (int data, int clock);
	void addBlock (int byte, int count);
	void addBlock (const Data &data);
	void addBlockUpdateCrc (const Data &data);

	void addGap (int count, int fill = GAP_FILL_BYTE);
	void addSync ();
	void addAM (int type);
	void addIAM ();
	void addIDAM ();
	void addDAM ();
	void addDDAM ();
	void addAltDAM ();
	void addAltDDAM ();
	void addCRC ();

	void addTrackStart ();
	void addSectorHeader (int cyl, int head, int sector, int size);
	void addSectorHeader(const Header &header);
	void addSectorData (const Data &data, bool deleted = false);
	void addSector (int cyl, int head, int sector, int size, const Data &data, int gap3, bool deleted = false);
	void addSector (const Header &header, const Data &data, int gap3, bool deleted = false);
	void addSectorUpToData (const Header &header, bool deleted);

	void addAmigaTrackStart ();
	std::vector<uint32_t> splitAmigaBits (const void *buf, int len, uint32_t &checksum);
	void addAmigaBits (std::vector<uint32_t> &bits);
	void addAmigaDword (uint32_t dword, uint32_t &checksum);
	void addAmigaSector (int cyl, int head, int sector, int remain, const void *buf);
	void addAmigaSector (const CylHead &cylhead, int sector, int remain, const void *buf);

protected:
	Encoding m_encoding{Encoding::MFM};
	bool m_onelast{false};
	CRC16 m_crc{};
};

#endif // TRACKBUFFER_H
