#ifndef BITBUFFER_H
#define BITBUFFER_H

#include "FluxDecoder.h"

class BitBuffer
{
public:
	BitBuffer () = default;
	BitBuffer (DataRate datarate_, Encoding encoding_=Encoding::Unknown, int revolutions=1);
	BitBuffer (DataRate datarate_, const uint8_t *pb, int len);
	BitBuffer (DataRate datarate_, FluxDecoder &decoder);

	bool wrapped () const;
	int size () const;
	int remaining () const;

	int tell () const;
	bool seek (int offset);
	bool seek_index (int index);
	int tell_index () const;

	void index ();
	void sync_lost ();
	void clear ();
	void add (uint8_t bit);

	uint8_t read1 ();
	uint8_t read8_msb ();
	uint8_t read8_lsb();
	uint16_t read16 ();
	uint32_t read32 ();
	uint8_t read_byte ();

	template <typename T>
	bool read (T &buf)
	{
		static_assert(sizeof(buf[0]) == 1, "unit size must be 1 byte");

		for (auto &b : buf)
		{
			b = read_byte();

			if (wrapped())
				return false;
		}

		return true;
	}

	int track_bitsize () const;
	int track_offset (int bitpos) const;
	bool sync_lost (int begin, int end) const;

	DataRate datarate{DataRate::Unknown};
	Encoding encoding{Encoding::MFM};

private:
	std::vector<uint32_t> m_data {};
	std::vector<int> m_indexes {};
	std::vector<int> m_sync_losses {};
	int m_bitsize = 0;
	int m_bitpos = 0;
	bool m_wrapped = false;
};

#endif // BITBUFFER_H
