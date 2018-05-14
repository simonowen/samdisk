// Bit buffer to hold decoded flux data for scanning

#include "SAMdisk.h"
#include "BitBuffer.h"

BitBuffer::BitBuffer (DataRate datarate_, Encoding encoding_, int revs)
	: datarate(datarate_), encoding(encoding_)
{
	// Estimate size from double the data bitrate @300rpm, plus 20%.
	// This should be enough for most FM/MFM tracks.
	auto bitlen = bits_per_second(datarate) * revs * 60/300 * 2 * 120/100;
	m_data.resize((bitlen + 7) / 8);
}

BitBuffer::BitBuffer (DataRate datarate_, const uint8_t *pb, int bitlen)
	: datarate(datarate_)
{
	auto bytelen = (bitlen + 7) / 8;
	m_data.resize(bytelen);
	std::memcpy(m_data.data(), pb, bytelen);
	m_bitsize = bitlen;
}

BitBuffer::BitBuffer (DataRate datarate_, FluxDecoder &decoder)
	: datarate(datarate_), m_data(decoder.flux_count())
{
	auto bitlen{bits_per_second(datarate) * decoder.flux_revs() * 60/300 * 2 * 120/100};
	m_data.resize((bitlen + 7) / 8);

	for (;;)
	{
		auto bit{decoder.next_bit()};
		if (bit < 0)
			break;

		if (decoder.sync_lost())
		{
			if (opt.debug) util::cout << "sync lost at offset " << tell() << " (" << track_offset(tell()) << ")\n";
			sync_lost();
		}

		add(bit ? 1 : 0);

		if (decoder.index())
			add_index();
	}
}

const std::vector<uint8_t> &BitBuffer::data () const
{
	return m_data;
}

bool BitBuffer::wrapped () const
{
	return m_wrapped || m_bitsize == 0;
}

int BitBuffer::size () const
{
	return m_bitsize;
}

int BitBuffer::remaining () const
{
	return size() - tell() + m_splicepos;
}

int BitBuffer::tell () const
{
	return m_bitpos;
}

bool BitBuffer::seek (int offset)
{
	m_wrapped = false;
	m_bitpos = std::min(offset, m_bitsize);
	set_next_index();
	return m_bitpos == offset;
}

int BitBuffer::splicepos () const
{
	return m_splicepos;
}

void BitBuffer::splicepos (int pos)
{
	m_splicepos = pos;
}

bool BitBuffer::index ()
{
	if (m_bitpos < m_next_index)
		return false;

	set_next_index();
	return true;
}

void BitBuffer::add_index ()
{
	m_indexes.push_back(m_bitpos);
}

void BitBuffer::set_next_index ()
{
	for (auto &index : m_indexes)
	{
		if (index > m_bitpos)
		{
			m_next_index = index;
			return;
		}
	}

	m_next_index = m_bitsize;
}

void BitBuffer::sync_lost ()
{
	m_sync_losses.push_back(m_bitpos);
}

void BitBuffer::clear ()
{
	*this = BitBuffer();
}

void BitBuffer::add (uint8_t bit)
{
	size_t offset = m_bitpos / 8;
	uint32_t bit_value = 1 << (m_bitpos & 7);

	// Double the size if we run out of space
	if (offset >= m_data.size())
	{
		assert(m_data.size() != 0);
		m_data.resize(m_data.size() * 2);
		if (opt.debug) util::cout << "BitBuffer size grown to " << m_data.size() << "\n";
	}

	if (bit)
		m_data[offset] |= bit_value;
	else
		m_data[offset] &= ~bit_value;

	m_bitsize = std::max(m_bitsize, ++m_bitpos);
}

void BitBuffer::remove (int num_bits)
{
	assert(m_bitpos >= num_bits);
	m_bitpos -= std::min(num_bits, m_bitpos);
	m_bitsize = m_bitpos;
}

uint8_t BitBuffer::read1 ()
{
	uint8_t bit = (m_data[m_bitpos / 8] >> (m_bitpos & 7)) & 1;

	if (++m_bitpos == m_bitsize)
	{
		m_bitpos = 0;
		m_wrapped = true;
	}

	return bit;
}

uint8_t BitBuffer::read8_msb()
{
	uint8_t byte = 0;

	for (auto i = 0; i < 8; ++i)
		byte = (byte << 1) | read1();

	return byte;
}

uint8_t BitBuffer::read8_lsb()
{
	uint8_t byte = 0;

	for (auto i = 0; i < 8; ++i)
		byte |= (read1() << i);

	return byte;
}

uint16_t BitBuffer::read16 ()
{
	uint16_t word = 0;

	for (auto i = 0; i < 16; ++i)
		word = (word << 1) | read1();

	return word;
}

uint32_t BitBuffer::read32()
{
	uint32_t dword = 0;

	for (auto i = 0; i < 32; ++i)
		dword = (dword << 1) | read1();

	return dword;
}

uint8_t BitBuffer::read_byte ()
{
	uint8_t data = 0;

	switch (encoding)
	{
	case Encoding::FM:
		for (auto i = 0; i < 8; ++i)
		{
			read1();
			read1();
			data = (data << 1) | read1();
			read1();
		}
		break;

	case Encoding::MFM:
		for (auto i = 0; i < 8; ++i)
		{
			read1();
			data = (data << 1) | read1();
		}
		break;

	case Encoding::Apple:
		for (auto i = 0; i < 8; ++i)
		{
			data = (data << 1) | read1();
		}
		// Disk ][ keeps reading until bit 7 is 1
		for (;(data & 0x80) == 0;)
		{
			data = (data << 1) | read1();
		}
		break;

	default:
		for (auto i = 0; i < 8; ++i)
		{
			data = (data << 1) | read1();
		}
		break;
	}

	return data;
}

int BitBuffer::track_bitsize () const
{
	return m_indexes.size() ? m_indexes[0] : m_bitsize;
}

int BitBuffer::track_offset (int bitpos) const
{
	for (auto it = m_indexes.rbegin(); it != m_indexes.rend(); ++it)
	{
		if (bitpos >= *it)
			return bitpos - *it;
	}

	return bitpos;
}

BitBuffer BitBuffer::track_bitstream () const
{
	BitBuffer newbuf(datarate, encoding);
	auto track_bits = track_bitsize();
	auto track_bytes = (track_bits + 7) / 8;

	newbuf.m_data.insert(newbuf.m_data.begin(), m_data.begin(), m_data.begin() + track_bytes);
	newbuf.m_bitsize = track_bits;
	return newbuf;
}

bool BitBuffer::align ()
{
	bool modified = false;
	auto bits_per_byte = (encoding == Encoding::FM) ? 32 : 16;
	uint16_t sync_mask = opt.a1sync ? 0xffdf : 0xffff;
	uint32_t dword = 0;
	int i;

	BitBuffer newbuf(datarate, encoding);
	newbuf.m_indexes = m_indexes;

	seek(0);
	while (!wrapped())
	{
		bool found_am = false;
		for (i = 0; !found_am && i < bits_per_byte; ++i)
		{
			dword = (dword << 1) | read1();

			if (encoding == Encoding::MFM && (dword & sync_mask) == 0x4489)
			{
				found_am = true;
			}
			else if (encoding == Encoding::FM)
			{
				switch (dword)
				{
				case 0xaa222888:	// F8/C7 DDAM
				case 0xaa22288a:	// F9/C7 Alt-DDAM
				case 0xaa2228a8:	// FA/C7 Alt-DAM
				case 0xaa2228aa:	// FB/C7 DAM
				case 0xaa2a2a88:	// FC/D7 IAM
				case 0xaa222a8a:	// FD/C7 RX02 DAM
				case 0xaa222aa8:	// FE/C7 IDAM
					found_am = true;
					break;
				}
			}
		}

		if (i != bits_per_byte)
		{
			// Adjust index positions beyond removal point.
			for (auto &idx_pos : m_indexes)
				if (idx_pos >= m_bitpos)
					idx_pos -= bits_per_byte;

			// Remove the last encoding unit, ready to add the aligned sync.
			newbuf.remove(bits_per_byte);
			i = bits_per_byte;
			modified = true;
		}

		while (--i >= 0)
			newbuf.add(static_cast<uint8_t>((dword >> i) & 1));
	}

	if (modified)
		std::swap(*this, newbuf);

	return modified;
}

bool BitBuffer::sync_lost(int begin, int end) const
{
	for (auto pos : m_sync_losses)
		if (begin < pos && end >= pos)
			return true;

	return false;
}
