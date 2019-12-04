// Base class for building track content from scratch

#include "SAMdisk.h"
#include "TrackBuilder.h"
#include "IBMPC.h"

TrackBuilder::TrackBuilder(DataRate datarate, Encoding encoding)
    : m_datarate(datarate)
{
    setEncoding(encoding);
}

void TrackBuilder::setEncoding(Encoding encoding)
{
    switch (encoding)
    {
    case Encoding::MFM:
    case Encoding::FM:
    case Encoding::RX02:
    case Encoding::Amiga:
        m_encoding = encoding;
        break;
    default:
        throw util::exception("unsupported track encoding (", encoding, ")");
    }
}

void TrackBuilder::addBit(bool bit)
{
    addRawBit(bit);

    if (m_encoding == Encoding::FM)
        addRawBit(false);
}

void TrackBuilder::addDataBit(bool bit)
{
    if (m_encoding == Encoding::FM)
    {
        // FM has a reversal before every data bit
        addBit(true);
        addBit(bit);
    }
    else
    {
        // MFM has a reversal between consecutive zeros (clock or data)
        addBit(!m_lastbit && !bit);
        addBit(bit);
    }

    m_lastbit = bit;
}

void TrackBuilder::addByte(int byte)
{
    for (auto i = 0; i < 8; ++i)
    {
        addDataBit((byte & 0x80) != 0);
        byte <<= 1;
    }
}

void TrackBuilder::addByteUpdateCrc(int byte)
{
    addByte(byte);
    m_crc.add(byte);
}

void TrackBuilder::addByteWithClock(int data, int clock)
{
    for (auto i = 0; i < 8; ++i)
    {
        addBit((clock & 0x80) != 0);
        addBit((data & 0x80) != 0);
        clock <<= 1;
        data <<= 1;
    }

    m_lastbit = (data & 0x100) != 0;
}

void TrackBuilder::addBlock(int byte, int count)
{
    for (int i = 0; i < count; ++i)
        addByte(byte);
}

void TrackBuilder::addBlock(const Data& data)
{
    for (auto& byte : data)
        addByte(byte);
}

void TrackBuilder::addBlockUpdateCrc(int byte, int count)
{
    for (int i = 0; i < count; ++i)
        addByteUpdateCrc(byte);
}

void TrackBuilder::addBlockUpdateCrc(const Data& data)
{
    for (auto& byte : data)
    {
        addByte(byte);
        m_crc.add(byte);
    }
}

void TrackBuilder::addGap(int count, int fill)
{
    if (fill < 0)
        fill = (m_encoding == Encoding::FM) ? 0xff : 0x4e;

    addBlock(fill, count);
}

void TrackBuilder::addGap2(int fill)
{
    int gap2_bytes = (m_encoding == Encoding::FM) ? 11 :
        (m_datarate == DataRate::_1M) ? 41 : 22;
    addGap(gap2_bytes, fill);
}

void TrackBuilder::addSync()
{
    auto sync{ 0x00 };
    addBlock(sync, (m_encoding == Encoding::FM) ? 6 : 12);
}

void TrackBuilder::addAM(int type, bool omit_sync)
{
    if (!omit_sync)
        addSync();

    if (m_encoding == Encoding::FM)
    {
        addByteWithClock(type, 0xc7);   // FM AM uses C7 clock pattern

        m_crc.init();
        m_crc.add(type);
    }
    else
    {
        addByteWithClock(0xa1, 0x0a);   // A1 with missing clock bit
        addByteWithClock(0xa1, 0x0a);   // clock: 0 0 0 0 1 X 1 0
        addByteWithClock(0xa1, 0x0a);   // data:   1 0 1 0 0 0 0 1

        m_crc.init(0xcdb4);             // A1A1A1
        addByteUpdateCrc(type);
    }
}

void TrackBuilder::addIAM()
{
    addSync();

    if (m_encoding == Encoding::FM)
    {
        addByteWithClock(0xfc, 0xd7);   // FM IAM uses D7 clock pattern
    }
    else
    {
        addByteWithClock(0xc2, 0x14);   // C2 with missing clock bit
        addByteWithClock(0xc2, 0x14);   // clock: 0 0 0 1 X 1 0 0
        addByteWithClock(0xc2, 0x14);   // data:   1 1 0 0 0 0 1 0
        addByte(0xfc);
    }
}

void TrackBuilder::addCrcBytes(bool bad_crc)
{
    uint16_t adjust = bad_crc ? 0x5555 : 0;
    addByte((m_crc ^ adjust) >> 8);
    addByte((m_crc ^ adjust) & 0xff);
}

void TrackBuilder::addTrackStart(bool short_mfm_gap)
{
    switch (m_encoding)
    {
    case Encoding::MFM:
    case Encoding::FM:
        if (m_encoding == Encoding::MFM && short_mfm_gap)
        {
            // Short gap without IAM, for 11-sector disks.
            addGap(20);
        }
        else
        {
            addGap((m_encoding == Encoding::FM) ? 40 : 80); // gap 4a
            addIAM();
            addGap((m_encoding == Encoding::FM) ? 26 : 50); // gap 1
        }
        break;
    case Encoding::Amiga:
    {
        auto fill{ 0x00 };
        addBlock(fill, 60);
        break;
    }
    case Encoding::RX02:
        setEncoding(Encoding::FM);
        addGap(32); // gap 4a
        addIAM();
        addGap(27); // gap 1
        setEncoding(Encoding::RX02);
        break;
    default:
        throw util::exception("unsupported track start (", m_encoding, ")");
    }
}

void TrackBuilder::addSectorHeader(const Header& header, bool crc_error)
{
    addAM(0xfe);
    addByteUpdateCrc(header.cyl);
    addByteUpdateCrc(header.head);
    addByteUpdateCrc(header.sector);
    addByteUpdateCrc(header.size);
    addCrcBytes(crc_error);
}

void TrackBuilder::addSectorData(const Data& data, int size, uint8_t dam, bool crc_error)
{
    // Ensure this isn't used for over-sized protected sectors.
    assert(Sector::SizeCodeToLength(size) == Sector::SizeCodeToLength(size));

    addAM(dam);

    // Ensure the written data matches the sector size code.
    auto len_bytes{ Sector::SizeCodeToLength(size) };
    if (data.size() == len_bytes)
    {
        // Normal data and appropriate CRC.
        addBlockUpdateCrc(data);
        addCrcBytes(crc_error);
    }
    else if (data.size() > len_bytes)
    {
        // Data plus gap, which will include data CRC.
        addBlockUpdateCrc(data);
    }
    else
    {
        // Short data padded to full size, and an appropriate CRC.
        addBlockUpdateCrc(data);
        Data data_pad(len_bytes - data.size(), 0x00);
        addBlockUpdateCrc(data_pad);
        addCrcBytes(crc_error);
    }
}

void TrackBuilder::addSector(const Sector& sector, int gap3_bytes)
{
    setEncoding(sector.encoding);

    switch (m_encoding)
    {
    case Encoding::MFM:
    case Encoding::FM:
    {
        addSectorHeader(sector.header);
        addGap2();
        if (sector.has_data())
            addSectorData(sector.data_copy(), sector.header.size, sector.dam, sector.has_baddatacrc());
        if (!sector.has_gapdata())
            addGap(gap3_bytes);
        break;
    }
    case Encoding::Amiga:
        addAmigaSector(sector.header, sector.header.sector, sector.data_copy().data());
        break;
    case Encoding::RX02:
        addRX02Sector(sector.header, sector.data_copy(), gap3_bytes);
        setEncoding(sector.encoding);
        break;
    default:
        throw util::exception("unsupported sector encoding (", sector.encoding, ")");
    }
}

void TrackBuilder::addSector(const Header& header, const Data& data, int gap3_bytes, uint8_t dam, bool crc_error)
{
    Sector sector(m_datarate, m_encoding, header, gap3_bytes);
    sector.add(Data(data), crc_error, dam);
    addSector(sector, sector.gap3);
}

// Sector header and DAM, but no data, CRC, or gap3 -- for weak sectors.
void TrackBuilder::addSectorUpToData(const Header& header, uint8_t dam)
{
    addSectorHeader(header);
    addGap2();
    addAM(dam);
}


void TrackBuilder::addAmigaTrackStart()
{
    addBlock(0x00, 60);
}

void TrackBuilder::addAmigaDword(uint32_t dword, uint32_t& checksum)
{
    dword = util::htobe(dword);
    std::vector<uint32_t> bits = splitAmigaBits(&dword, sizeof(uint32_t), checksum);
    addAmigaBits(bits);
}

void TrackBuilder::addAmigaBits(std::vector<uint32_t>& bits)
{
    for (auto it = bits.begin(); it != bits.end(); ++it)
    {
        uint32_t data = *it;
        for (auto i = 0; i < 16; ++i)
        {
            addDataBit((data & 0x40000000) != 0);
            data <<= 2;
        }
    }
}

std::vector<uint32_t> TrackBuilder::splitAmigaBits(const void* buf, int len, uint32_t& checksum)
{
    auto dwords = len / static_cast<int>(sizeof(uint32_t));
    const uint32_t* pdw = reinterpret_cast<const uint32_t*>(buf);
    std::vector<uint32_t> odddata;
    odddata.reserve(dwords * 2);

    // Even then odd passes over the data
    for (auto i = 0; i < 2; ++i)
    {
        // All DWORDs in the block
        for (int j = 0; j < dwords; ++j)
        {
            uint32_t bits = 0;
            uint32_t data = frombe32(pdw[j]) << i;

            // 16 bits (odd or even) from each DWORD pass
            for (auto k = 0; k < 16; ++k)
            {
                bits |= ((data & 0x80000000) >> (1 + k * 2));
                data <<= 2;
            }

            odddata.insert(odddata.end(), bits);
            checksum ^= bits;
        }
    }

    return odddata;
}

void TrackBuilder::addAmigaSector(const CylHead& cylhead, int sector, const void* buf)
{
    addByte(0x00);
    addByteWithClock(0xa1, 0x0a);   // A1 with missing clock bit
    addByteWithClock(0xa1, 0x0a);

    auto sectors = (m_datarate == DataRate::_500K) ? 22 : 11;
    auto remain = sectors - sector;

    uint32_t checksum = 0;
    uint32_t info = (0xff << 24) | (((cylhead.cyl << 1) | cylhead.head) << 16) |
        (sector << 8) | remain;
    addAmigaDword(info, checksum);

    uint32_t sector_label[4] = {};
    auto bits = splitAmigaBits(sector_label, sizeof(sector_label), checksum);
    addAmigaBits(bits);
    addAmigaDword(checksum, checksum);

    checksum = 0;
    bits = splitAmigaBits(buf, 512, checksum);
    addAmigaDword(checksum, checksum);
    addAmigaBits(bits);

    addByte(0x00);
}


void TrackBuilder::addRX02Sector(const Header& header, const Data& data, int gap3_bytes)
{
    setEncoding(Encoding::FM);

    addSectorHeader(header);
    addGap2();
    addAM(0xfd);    // RX02 DAM

    setEncoding(Encoding::MFM);

    addBlockUpdateCrc(data);
    addCrcBytes();
    addGap(gap3_bytes);
}
