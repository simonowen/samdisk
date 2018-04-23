#ifndef TRACKBUFFER_H
#define TRACKBUFFER_H

#include "CRC16.h"

class TrackBuffer
{
	static const int GAP_FILL_BYTE = 0x4e;	// IBM System 34 gap filler

public:
	virtual ~TrackBuffer () = default;

	virtual void setEncoding (Encoding encoding);
	virtual void addRawBit (bool one) = 0;

	void addBit (bool bit);
	void addDataBit (bool bit);
	void addByte (int byte);
	void addByteBits (int byte, int num_bits);
	void addByteWithClock (int data, int clock);
	void addWord (uint16_t word);
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
	void addCRC (bool bad_crc = false);

	void addTrackStart ();
	void addSectorHeader (int cyl, int head, int sector, int size, bool crc_error = false);
	void addSectorHeader(const Header &header, bool crc_error = false);
	void addSectorData (const Data &data, int size, bool deleted = false, bool crc_error = false);
	void addSector (const Sector &sector, int gap3);
	void addSector (int cyl, int head, int sector, int size, const Data &data, int gap3, bool deleted = false, bool crc_error = false);
	void addSector (const Header &header, const Data &data, int gap3, bool deleted = false, bool crc_error = false);
	void addSectorUpToData (const Header &header, bool deleted = false);

	void addAmigaTrackStart ();
	std::vector<uint32_t> splitAmigaBits (const void *buf, int len, uint32_t &checksum);
	void addAmigaBits (std::vector<uint32_t> &bits);
	void addAmigaDword (uint32_t dword, uint32_t &checksum);
	void addAmigaSector (int cyl, int head, int sector, int remain, const void *buf);
	void addAmigaSector (const CylHead &cylhead, int sector, int remain, const void *buf);

	void addRX02TrackStart ();
	void addRX02Sector (const CylHead &cylhead, int sector, int size, const Data &data, int gap3);

protected:
	Encoding m_encoding{Encoding::MFM};
	bool m_lastbit{false};
	CRC16 m_crc{};
};

#endif // TRACKBUFFER_H
