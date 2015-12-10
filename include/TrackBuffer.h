#ifndef TRACKBUFFER_H
#define TRACKBUFFER_H

#include "CRC16.h"

class TrackBuffer
{
	static const uint8_t GAP_FILL_BYTE = 0x4e;	// IBM System 34 gap filler

public:
	explicit TrackBuffer (bool mfm = true);
	virtual ~TrackBuffer () = default;

	void setFM () { m_mfm = false; }
	void setMFM () { m_mfm = true; }

	virtual void addBit (bool one) = 0;

//	void addBit (bool one);
	void addDataBit (bool one);
	void addByte (uint8_t byte);
	void addByte (uint8_t data, uint8_t clock);
	void addBlock (uint8_t byte, size_t count);
	void addBlock (const void *buf, size_t len);

	void addGap (size_t count, uint8_t fill = GAP_FILL_BYTE);
	void addSync ();
	void addAM (uint8_t type);
	void addIAM ();
	void addIDAM ();
	void addDAM ();
	void addDDAM ();
	void addAltDAM ();
	void addAltDDAM ();
	void addCRC ();

	void addTrackStart ();
	void addTrackEnd ();
	void addSectorHeader (uint8_t cyl, uint8_t head, uint8_t sector, uint8_t size);
	void addSectorData (const void *buf, size_t len, bool deleted = false);
	void addSector (uint8_t cyl, uint8_t head, uint8_t sector, uint8_t size, const void *buf, size_t len, size_t gap3, bool deleted = false);

	void addAmigaTrackStart ();
	void addAmigaTrackEnd ();
	std::vector<uint32_t> splitAmigaBits (const void *buf, size_t len, uint32_t &checksum);
	void addAmigaBits (std::vector<uint32_t> &bits);
	void addAmigaDword (uint32_t dword, uint32_t &checksum);
	void addAmigaSector (uint8_t cyl, uint8_t head, uint8_t sector, uint8_t remain, const void *buf);

protected:
	bool m_mfm = true;
	CRC16 m_crc {};
	bool m_onelast = false;
};

#endif // TRACKBUFFER_H
