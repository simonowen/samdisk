#ifndef TRACKBUFFER_H
#define TRACKBUFFER_H

#include "CRC16.h"

class TrackBuilder
{
public:
	TrackBuilder (DataRate datarate, Encoding encoding = Encoding::Unknown);
	virtual ~TrackBuilder () = default;

	virtual void setEncoding (Encoding encoding);
	virtual void addRawBit (bool one) = 0;

	void addBit (bool bit);
	void addDataBit (bool bit);
	void addByte (int byte);
	void addByteUpdateCrc (int byte);
	void addByteWithClock (int data, int clock);
	void addBlock (int byte, int count);
	void addBlock (const Data &data);
	void addBlockUpdateCrc (const Data &data);

	void addGap (int count, int fill = -1);
	void addGap2 (int fill = -1);
	void addSync ();
	void addAM (int type, bool omit_sync = false);
	void addIAM ();
	void addCrcBytes (bool bad_crc = false);

	void addTrackStart ();
	void addSectorHeader (const Header &header, bool crc_error = false);
	void addSectorData (const Data &data, int size, uint8_t dam = 0xfb, bool crc_error = false);
	void addSector (const Sector &sector, int gap3_bytes = 0);
	void addSector (const Header &header, const Data &data, int gap3_bytes = 0, uint8_t dam = 0xfb, bool crc_error = false);
	void addSectorUpToData (const Header &header, uint8_t dam = 0xfb);

	void addAmigaTrackStart ();
	std::vector<uint32_t> splitAmigaBits (const void *buf, int len, uint32_t &checksum);
	void addAmigaBits (std::vector<uint32_t> &bits);
	void addAmigaDword (uint32_t dword, uint32_t &checksum);
	void addAmigaSector (const CylHead &cylhead, int sector, const void *buf);

	void addRX02Sector (const Header &header, const Data &data, int gap3_bytes);

protected:
	Encoding m_encoding{Encoding::MFM};
	DataRate m_datarate{DataRate::Unknown};
	bool m_lastbit{false};
	CRC16 m_crc{};
};

#endif // TRACKBUFFER_H
