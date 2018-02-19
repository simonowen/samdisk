#ifndef CRC16_H
#define CRC16_H

#include <mutex>

class CRC16
{
public:
	static const uint16_t POLYNOMIAL = 0x1021;
	static const uint16_t INIT_CRC = 0xffff;
	static const uint16_t A1A1A1 = 0xcdb4;		// CRC of 0xa1, 0xa1, 0xa1

public:
	explicit CRC16 (uint16_t init = INIT_CRC);
	CRC16 (const void *buf, size_t len, uint16_t init = INIT_CRC);
	operator uint16_t () const;

	void init(uint16_t crc = INIT_CRC);
	uint16_t add (int byte);
	uint16_t add (int byte, size_t len);
	uint16_t add (const void *buf, size_t len);
	uint8_t lsb () const;
	uint8_t msb () const;

private:
	static void init_crc_table ();
	static std::array<uint16_t, 256> s_crc_lookup;
	static std::once_flag flag;

	uint16_t m_crc = INIT_CRC;
};

#endif // CRC_H
