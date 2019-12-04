#pragma once

#include "Header.h"

class Data : public std::vector<uint8_t>
{
public:
    using std::vector<uint8_t>::vector;
    int size() const { return static_cast<int>(std::vector<uint8_t>::size()); }
};

using DataList = std::vector<Data>;


class Sector
{
public:
    enum class Merge { Unchanged, Improved, NewData };

public:
    Sector(DataRate datarate, Encoding encoding, const Header& header = Header(), int gap3 = 0);
    bool operator==(const Sector& sector) const;

    Merge merge(Sector&& sector);

    bool has_data() const;
    bool has_good_data() const;
    bool has_gapdata() const;
    bool has_shortdata() const;
    bool has_badidcrc() const;
    bool has_baddatacrc() const;
    bool is_deleted() const;
    bool is_altdam() const;
    bool is_rx02dam() const;
    bool is_8k_sector() const;

    void set_badidcrc(bool bad = true);
    void set_baddatacrc(bool bad = true);
    void remove_data();
    void remove_gapdata(bool keep_crc = false);
    void limit_copies(int max_copies);

    int size() const;
    int data_size() const;

    const DataList& datas() const;
    DataList& datas();
    const Data& data_copy(int copy = 0) const;
    Data& data_copy(int copy = 0);

    Merge add(Data&& data, bool bad_crc = false, uint8_t dam = 0xfb);
    int copies() const;

    static int SizeCodeToRealSizeCode(int size);
    static int SizeCodeToLength(int size);

public:
    Header header{ 0,0,0,0 };               // cyl, head, sector, size
    DataRate datarate = DataRate::Unknown;  // 250Kbps
    Encoding encoding = Encoding::Unknown;  // MFM
    int offset = 0;                         // bitstream offset from index, in bits
    int gap3 = 0;                           // inter-sector gap size
    uint8_t dam = 0xfb;                     // data address mark

private:
    bool m_bad_id_crc = false;
    bool m_bad_data_crc = false;
    std::vector<Data> m_data{};         // copies of sector data
};
#pragma once
