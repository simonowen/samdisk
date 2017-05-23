// CRC-16-CCITT implementation

#include "SAMdisk.h"
#include "CRC16.h"

std::array<uint16_t, 256> CRC16::s_crc_lookup;
std::once_flag CRC16::flag;


CRC16::CRC16 (uint16_t init_)
{
	std::call_once(flag, init_crc_table);
	init(init_);
}

CRC16::CRC16 (const void *buf, size_t len, uint16_t init_)
{
	std::call_once(flag, init_crc_table);
	init(init_);
	add(buf, len);
}

/*static*/ void CRC16::init_crc_table ()
{
	if (!s_crc_lookup[0])
	{
		for (uint16_t i = 0; i < 256; ++i)
		{
			uint16_t crc = i << 8;

			for (int j = 0; j < 8; ++j)
				crc = (crc << 1) ^ ((crc & 0x8000) ? POLYNOMIAL : 0);

			s_crc_lookup[i] = crc;
		}
	}
}

CRC16::operator uint16_t () const
{
	return m_crc;
}

void CRC16::init (uint16_t init_crc)
{
	m_crc = init_crc;
}

uint16_t CRC16::add (int byte)
{
	m_crc = (m_crc << 8) ^ s_crc_lookup[((m_crc >> 8) ^ byte) & 0xff];
	return m_crc;
}

uint16_t CRC16::add (int byte, size_t len)
{
	while (len-- > 0)
		add(byte);

	return m_crc;
}

uint16_t CRC16::add (const void *buf, size_t len)
{
	const uint8_t *pb = reinterpret_cast<const uint8_t *>(buf);
	while (len-- > 0)
		add(*pb++);

	return m_crc;
}
