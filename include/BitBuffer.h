#pragma once

#include "FluxDecoder.h"

class BitBuffer
{
public:
    BitBuffer() = default;
    BitBuffer(DataRate datarate_, Encoding encoding_ = Encoding::Unknown, int revolutions = 1);
    BitBuffer(DataRate datarate_, const uint8_t* pb, int len);
    BitBuffer(DataRate datarate_, FluxDecoder& decoder);

    const std::vector<uint8_t>& data() const;
    bool wrapped() const;
    int size() const;
    int remaining() const;

    int tell() const;
    bool seek(int offset);

    int splicepos() const;
    void splicepos(int pos);

    bool index();
    void add_index();
    void set_next_index();

    void sync_lost();
    void clear();
    void add(uint8_t bit);
    void remove(int num_bits);

    uint8_t read1();
    uint8_t read2();
    uint8_t read8_msb();
    uint8_t read8_lsb();
    uint16_t read16();
    uint32_t read32();
    uint8_t read_byte();

    template <typename T>
    bool read(T& buf)
    {
        static_assert(sizeof(buf[0]) == 1, "unit size must be 1 byte");
        bool clean = remaining() >= static_cast<int>(sizeof(buf));

        for (auto& b : buf)
            b = read_byte();

        return clean;
    }

    int track_bitsize() const;
    int track_offset(int bitpos) const;
    BitBuffer track_bitstream() const;
    bool align();
    bool sync_lost(int begin, int end) const;

    DataRate datarate{ DataRate::Unknown };
    Encoding encoding{ Encoding::MFM };

private:
    std::vector<uint8_t> m_data{};
    std::vector<int> m_indexes{};
    std::vector<int> m_sync_losses{};
    int m_bitsize = 0;
    int m_bitpos = 0;
    int m_splicepos = 0;
    int m_next_index = -1;
    bool m_wrapped = false;
};
