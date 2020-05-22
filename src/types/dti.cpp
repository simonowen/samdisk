// Deep Thought disk interface for the Jupiter Ace:
//  https://raw.githubusercontent.com/simonowen/deepthought/master/dti.txt
//
//  http://www.jupiter-ace.co.uk/hardware_DeepThought.html

#include "SAMdisk.h"
#include "JupiterAce.h"

#define DTI_SIGNATURE   "H2G2"
const int DTI_BLOCK_SIZE = 9 * 256; // 2304

struct DTI_HEADER
{
    char    abSignature[4]; // H2G2
    uint8_t bTracks;        // 40 or 80
    uint8_t bSides;         // 1 or 2
    uint8_t bBlockLow;      // Track block size in bytes (LSB)
    uint8_t bBlockHigh;     // Track block size in bytes (MSB)
};

struct DTI_TRACK
{
    uint8_t flags;          // b0: framing/parity/checksum error found
    uint8_t bDataLow;       // Valid track data length (LSB)
    uint8_t bDataHigh;      // Valid track data length (MSB)
};


bool ReadDTI(MemFile& file, std::shared_ptr<Disk>& disk)
{
    file.rewind();

    DTI_HEADER dh;
    if (!file.read(&dh, sizeof(dh)) || memcmp(dh.abSignature, DTI_SIGNATURE, sizeof(dh.abSignature)))
        return false;

    Format::Validate(dh.bTracks, dh.bSides);

    auto uBlock = (dh.bBlockHigh << 8) | dh.bBlockLow;
    if (uBlock != DTI_BLOCK_SIZE)
        throw util::exception("unsupported track block size (", uBlock, ")");

    MEMORY mem(uBlock);

    for (uint8_t head = 0; head < dh.bSides; head++)
    {
        for (uint8_t cyl = 0; cyl < dh.bTracks; cyl++)
        {
            CylHead cylhead(cyl, head);

            if (!file.read(mem, uBlock))
                throw util::exception("short file reading %s", cylhead);

            uint8_t flags = mem[0];
            auto uDataLen = (mem[2] << 8) | mem[1];
            if (uDataLen > uBlock - static_cast<int>(sizeof(DTI_TRACK)))
                throw util::exception("invalid data length (", uDataLen, ") on ", cylhead);

            Track track(1);
            if (uDataLen)
            {
                bool bad_data_crc = (flags & 1) != 0;
                Sector sector(DataRate::_250K, Encoding::Ace, Header(cylhead, 0, SizeToCode(4096)));
                Data data(mem.pb + 2, mem.pb + 2 + uDataLen);
                sector.add(std::move(data), bad_data_crc);
                track.add(std::move(sector));
            }

            disk->write(cylhead, std::move(track));
        }
    }

    disk->strType = "DTI";
    return true;
}

bool WriteDTI(FILE* f_, std::shared_ptr<Disk>& disk)
{
    const Sector* ps = nullptr;

    if (!IsDeepThoughtDisk(*disk, ps) || ps->encoding != Encoding::Ace)
        throw util::exception("source is not in Deep Thought format");

    auto track_data = GetDeepThoughtData(ps->data_copy());
    if (track_data[0] != 40 && track_data[0] != 80)
        throw util::exception("invalid Deep Thought track count (", track_data[0], ")");

    DTI_HEADER dh = {};
    memcpy(dh.abSignature, DTI_SIGNATURE, sizeof(dh.abSignature));
    dh.bTracks = track_data[0];
    dh.bSides = static_cast<uint8_t>(disk->heads());
    dh.bBlockLow = DTI_BLOCK_SIZE & 0xff;
    dh.bBlockHigh = DTI_BLOCK_SIZE >> 8;

    MEMORY mem(DTI_BLOCK_SIZE);

    if (!fwrite(&dh, sizeof(dh), 1, f_))
        throw posix_error();

    for (uint8_t head = 0; head < dh.bSides; head++)
    {
        for (uint8_t cyl = 0; cyl < dh.bTracks; cyl++)
        {
            memset(mem, 0, mem.size);

            auto& sector = disk->get_sector(Header(cyl, head, 0, SizeToCode(4096)));
            auto& data = sector.data_copy();
            auto data_offset = 0;

            if (IsDeepThoughtSector(sector, data_offset) && data.size() <= (DTI_BLOCK_SIZE - static_cast<int>(sizeof(DTI_TRACK))))
            {
                mem[0] = sector.has_baddatacrc() ? 1 : 0;
                mem[1] = sector.data_size() & 0xff;
                mem[2] = static_cast<uint8_t>(sector.data_size() >> 8);
                memcpy(mem + sizeof(DTI_TRACK), data.data(), data.size());
            }

            if (!fwrite(mem, mem.size, 1, f_))
                throw posix_error();
        }
    }

    return true;
}
