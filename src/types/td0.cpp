// Teledisk archiver
//
// https://web.archive.org/web/20130116072335/http://www.fpns.net/willy/wteledsk.htm
//
// Dave Dunfield notes: http://www.classiccmp.org/dunfield/img54306/td0notes.txt

#include "SAMdisk.h"
#include "IBMPC.h"

#define TD0_SIGNATURE_RLE       "TD"    // Normal compression (RLE)
#define TD0_SIGNATURE_HUFF      "td"    // Huffman compression also used for everything after TD0_HEADER

// Overall file header, always uncompressed
typedef struct
{
    uint8_t abSignature[2];
    uint8_t bVolSequence;       // Volume sequence (zero for the first)
    uint8_t bCheckSig;          // Check signature for multi-volume sets (all must match)
    uint8_t bTDVersion;         // Teledisk version used to create the file (11 = v1.1)
    uint8_t bSourceDensity;     // Source disk density (0 = 250K bps,  1 = 300K bps,  2 = 500K bps ; +128 = single-density FM)
    uint8_t bDriveType;         // Source drive type (1 = 360K, 2 = 1.2M, 3 = 720K, 4 = 1.44M)
    uint8_t bTrackDensity;      // 0 = source matches media density, 1 = double density in quad drive, 2 = quad density in double drive)
    uint8_t bDOSMode;           // Non-zero if disk was analysed according to DOS allocation
    uint8_t bSurfaces;          // Disk sides stored in the image
    uint8_t bCRCLow, bCRCHigh;  // 16-bit CRC for this header
} TD0_HEADER;

// Optional comment block, present if bit 7 is set in bTrackDensity above
typedef struct
{
    uint8_t bCRCLow, bCRCHigh;  // 16-bit CRC covering the comment block
    uint8_t bLenLow, bLenHigh;  // Comment block length
    uint8_t bYear, bMon, bDay;  // Date of disk creation
    uint8_t bHour, bMin, bSec;  // Time of disk creation
//  uint8_t abData[];           // Comment data, in null-terminated blocks
} TD0_COMMENT;

typedef struct
{
    uint8_t sectors;            // Number of sectors in track
    uint8_t cyl;                // Physical track we read from
    uint8_t head;               // Physical side we read from
    uint8_t crc;                // Low 8-bits of track header CRC
} TD0_TRACK;

typedef struct
{
    uint8_t cyl;                // Track number in ID field
    uint8_t head;               // Side number in ID field
    uint8_t sector;             // Sector number in ID field
    uint8_t size;               // Sector size indicator:  (128 << bSize) gives the real size
    uint8_t flags;              // Flags detailing special sector conditions
    uint8_t data_crc;           // Low 8-bits of sector data CRC
} TD0_SECTOR;

typedef struct
{
    uint8_t bOffLow, bOffHigh;  // Offset to next sector, from after this offset value
    uint8_t bMethod;            // Storage method used for sector data (0 = raw, 1 = repeated 2-byte pattern, 2 = RLE block)
} TD0_DATA;


// Namespace wrapper for the Huffman decompression code
namespace LZSS
{
int Unpack(MemFile& file, uint8_t* pOut_);
}


// Generate/update a Teledisk CRC
static uint16_t CrcTd0Block(const void* pv_, size_t uLen_, uint16_t wCRC_ = 0)
{
    // Work through all bytes in the supplied block
    for (const uint8_t* p = reinterpret_cast<const uint8_t*>(pv_); uLen_--; p++)
    {
        // Merge in the input byte
        wCRC_ ^= (*p << 8);

        // Shift through all 8 bits, using the CCITT polynomial 0xa097
        for (int i = 0; i < 8; i++)
            wCRC_ = (wCRC_ << 1) ^ ((wCRC_ & 0x8000) ? 0xa097 : 0);
    }

    return wCRC_;
}


// Unpack a possibly RLE-encoded data block
static bool UnpackData(MemFile& file, uint8_t* pb_, int uSize_)
{
    TD0_DATA td;
    if (!file.read(&td, sizeof(td)))
        return false;

    auto uDataEnd = static_cast<int>(file.tell() + ((td.bOffHigh << 8) | td.bOffLow) - sizeof(td.bMethod));
    uint8_t* pEnd = pb_ + uSize_;

    while (file.tell() < uDataEnd)
    {
        // Examine storage method used for sector data (0 = raw, 1 = repeated 2-byte pattern, 2 = RLE block)
        switch (td.bMethod)
        {
        case 0: // raw sector
            if (!file.read(pb_, uSize_))
                return false;

            pb_ += uSize_;
            break;

        case 1: // repeated 2-byte pattern
        {
            uint8_t ab[4];
            if (!file.read(ab, sizeof(ab)))
                return false;

            auto count = (ab[1] << 8) | ab[0];
            uint8_t b1 = ab[2], b2 = ab[3];

            if (pb_ + count * 2 > pEnd)
                return false;

            while (count--)
            {
                *pb_++ = b1;
                *pb_++ = b2;
            }
            break;
        }

        case 2: // RLE block
        {
            uint8_t ab[2];
            if (!file.read(ab, sizeof(ab)))
                return false;

            // Zero count means a literal data block
            if (!ab[0])
            {
                auto len = ab[1];

                if (!file.read(pb_, len))
                    return false;

                pb_ += len;
            }
            else    // repeated fragment
            {
                auto uBlock = 1 << ab[0];
                auto uCount = ab[1];

                if (!uCount || pb_ + (uBlock * uCount) > pEnd)
                    return false;

                if (!file.read(pb_, uBlock))
                    return false;

                for (uint8_t* pb = pb_; uCount--; pb_ += uBlock)
                    memcpy(pb_, pb, uBlock);
            }
            break;
        }

        default: // error!
            return false;
        }
    }

    if (file.tell() != uDataEnd)
        return false;

    return true;
}


bool ReadTD0(MemFile& file, std::shared_ptr<Disk>& disk)
{
    TD0_HEADER th;
    if (!file.rewind() || !file.read(&th, sizeof(th)))
        return false;
    else if (memcmp(th.abSignature, TD0_SIGNATURE_RLE, sizeof(th.abSignature)) &&
        memcmp(th.abSignature, TD0_SIGNATURE_HUFF, sizeof(th.abSignature)))
        return false;
    else if (CrcTd0Block(&th, sizeof(th) - (sizeof(th.bCRCHigh) + sizeof(th.bCRCLow))) != ((th.bCRCHigh << 8) | th.bCRCLow))
        return false;
    else if (th.bVolSequence || th.bTDVersion < 0x10)
        throw util::exception("multi-volume Teledisk sets are not supported");
    else if (th.bTDVersion < 0x10)
        throw util::exception("unsupported Teledisk version (", th.bTDVersion >> 4, ".", th.bTDVersion & 0xf);

    auto no_id_sectors = 0;

    static DataRate datarates[] = { DataRate::_250K, DataRate::_300K, DataRate::_500K, DataRate::_500K };
    auto datarate = datarates[th.bSourceDensity & 3];

    // If the file is Huffman compressed, unpack it
    if (th.abSignature[0] == 't')
    {
        // 3MB should be enough for any TD0 image
        MEMORY mem(3 * 1024 * 1024);
        auto uSize = LZSS::Unpack(file, mem);
        std::string filename = file.name();
        file.open(mem, uSize, filename);

        disk->metadata["compress"] = "advanced";
    }

    // Skip the comment field, if present
    if (th.bTrackDensity & 0x80)
    {
        TD0_COMMENT tc;
        if (!file.read(&tc, sizeof(tc)))// || (!tc.bLenHigh && tc.b)
            throw util::exception("short file reading comment header");

        // Extract the creation date
        disk->metadata["created"] = util::fmt("%04u-%02u-%02u %02u:%02u:%02u",
            tc.bYear + ((tc.bYear < 70) ? 2000 : 1900), tc.bMon + 1, tc.bDay,
            tc.bHour, tc.bMin, tc.bSec);

        // Read the comment block, ensuring it's null-terminated
        size_t len = ((tc.bLenHigh << 8) | tc.bLenLow);
        std::vector<char> comment(len);
        if (!file.read(comment))
            throw util::exception("short file reading comment data");
        disk->metadata["comment"] = std::string(comment.data(), comment.size());
    }

    for (;;)
    {
        TD0_TRACK tt;
        if (!file.read(&tt, sizeof(tt)))
            throw util::exception("short file reading track header");

        // End marker?
        if (tt.sectors == 0xff)
            break;

        CylHead cylhead(tt.cyl, tt.head & 1);
        Track track;

        uint8_t crc = CrcTd0Block(&tt, sizeof(tt) - sizeof(tt.crc)) & 0xff;
        if (tt.crc && crc != tt.crc)
            throw util::exception("invalid track CRC at ", cylhead);

        // Bit 7 of the physical side value is set for FM tracks
        Encoding encoding = (tt.head & 0x80) ? Encoding::FM : Encoding::MFM;

        // Loop through the sectors in the track
        for (int i = 0; i < tt.sectors; ++i)
        {
            TD0_SECTOR ts;
            if (!file.read(&ts, sizeof(ts)))
                throw util::exception("short file reading header for %s", CHS(tt.cyl, tt.head & 1, i));

            Sector sector(datarate, encoding, Header(ts.cyl, ts.head, ts.sector, ts.size));

            //          bool duplicate_id = (ts.bFlags & 0x01) != 0;    // detected automatically
            bool bad_data = (ts.flags & 0x02) != 0;
            bool deleted_data = (ts.flags & 0x04) != 0;
            bool no_data = (ts.flags & 0x20) != 0;
            bool no_id = (ts.flags & 0x40) != 0;

            if (ts.flags & 0x88)
                Message(msgWarning, "invalid flags (%#02x) on %s", ts.flags, CHSR(cylhead.cyl, cylhead.head, i, sector.header.sector));

            // Does this sector have a data field with a valid size?
            if (!no_data && !(ts.size & 0xf8))
            {
                // Assume DOS filler initially
                Data data(sector.size(), 0xe5);

                // Real data present?
                if (!(ts.flags & 0x10))
                {
                    if (!UnpackData(file, data.data(), static_cast<int>(data.size())))
                        throw util::exception("failed to unpack %s", CHSR(cylhead.cyl, cylhead.head, i, sector.header.sector));

                    crc = CrcTd0Block(data.data(), data.size()) & 0xff;
                    if (crc != ts.data_crc)
                        throw util::exception("CRC bad for %s", CHSR(cylhead.cyl, cylhead.head, i, sector.header.sector));
                }

                sector.add(std::move(data), bad_data, deleted_data ? 0xf8 : 0xfb);
            }

            // If the first sector on the track shows as no-id, ignore it due to suspected Teledisk bug
            if (opt.fix != 0 && i == 0 && no_id)
            {
                no_id_sectors++;
                continue;
            }

            track.add(std::move(sector));
        }

        // Assume 360rpm for 300Kbps and 300rpm for the rest, then work out the track size
        auto drive_speed = (datarate == DataRate::_300K) ? RPM_TIME_360 : RPM_TIME_300;
        auto track_capacity = GetTrackCapacity(drive_speed, datarate, encoding);

        // Roughly sum the data stored, ignoring gaps and headers
        auto data_sum = 0;
        for (const Sector& s : track.sectors())
            data_sum += s.data_size();

        // Oversized track?
        if (opt.fix != 0 && data_sum > track_capacity)
        {
            auto dups_removed = 0;

            auto it = track.sectors().rbegin();
            auto rend = track.sectors().rend();
            while (it != rend)
            {
                if (track.is_repeated(*it))
                {
                    ++it;
                    it = decltype(it)(track.sectors().erase(it.base()));
                    dups_removed++;
                }
                else
                {
                    ++it;
                }
            }

            // If dups were removed, warn of the modification
            if (dups_removed)
                Message(msgFix, "ignored %u duplicate sectors on oversized %s", dups_removed, CH(cylhead.cyl, cylhead.head));
        }

        disk->write(cylhead, std::move(track));
    }

    if (no_id_sectors)
        Message(msgFix, "ignored %d suspect no-id sector%s", no_id_sectors, (no_id_sectors == 1) ? "" : "s");

    disk->strType = "TD0";
    return true;
}


////////////////////////////////////////////////////////////////////////////////
//
// LZSS Compression - adapted from the original C code by Haruhiko Okumura (1988)
//
// For algorithm/implementation details, as well as general compression info, see:
//   http://www.fadden.com/techmisc/hdc/  (chapter 10 covers LZSS)
//
// Tweaked and reformatted to improve my own understanding, and wrapped in a
// private namespace to avoid polluting the global namespace with the following.

namespace LZSS
{
#define N           4096                    // ring buffer size
#define F           60                      // lookahead buffer size
#define THRESHOLD   2                       // match needs to be longer than this for position/length coding

#define N_CHAR      (256 - THRESHOLD + F)   // kinds of characters (character code = 0..N_CHAR-1)
#define T           (N_CHAR * 2 - 1)        // size of table
#define R           (T - 1)                 // tree root position
#define MAX_FREQ    0x8000                  // updates tree when root frequency reached this value


short parent[T + N_CHAR];                   // parent nodes (0..T-1) and leaf positions (rest)
short son[T];                               // pointers to child nodes (son[], son[] + 1)
uint16_t freq[T + 1];                       // frequency table

uint8_t ring_buff[N + F - 1];               // text buffer for match strings
unsigned r;                                 // Ring buffer position

MemFile* pfile;                             // compressed input file
unsigned uBits, uBitBuff;                   // buffered bit count and left-aligned bit buffer


static const uint8_t d_len[] = { 3,3,4,4,4,5,5,5,5,6,6,6,7,7,7,8 };

static const uint8_t d_code[] =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
    0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D, 0x0E, 0x0E, 0x0E, 0x0E, 0x0F, 0x0F, 0x0F, 0x0F,
    0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11, 0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13,
    0x14, 0x14, 0x14, 0x14, 0x15, 0x15, 0x15, 0x15, 0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x17,
    0x18, 0x18, 0x19, 0x19, 0x1A, 0x1A, 0x1B, 0x1B, 0x1C, 0x1C, 0x1D, 0x1D, 0x1E, 0x1E, 0x1F, 0x1F,
    0x20, 0x20, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23, 0x24, 0x24, 0x25, 0x25, 0x26, 0x26, 0x27, 0x27,
    0x28, 0x28, 0x29, 0x29, 0x2A, 0x2A, 0x2B, 0x2B, 0x2C, 0x2C, 0x2D, 0x2D, 0x2E, 0x2E, 0x2F, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
};


// Initialise the trees and state variables
void Init()
{
    unsigned i;

    memset(parent, 0, sizeof(parent));
    memset(son, 0, sizeof(son));
    memset(freq, 0, sizeof(freq));
    memset(ring_buff, 0, sizeof(ring_buff));


    for (i = 0; i < N_CHAR; i++)
    {
        freq[i] = 1;
        son[i] = i + T;
        parent[i + T] = i;
    }

    i = 0;
    for (int j = N_CHAR; j <= R; i += 2, j++)
    {
        freq[j] = freq[i] + freq[i + 1];
        son[j] = i;
        parent[i] = parent[i + 1] = j;
    }

    uBitBuff = uBits = 0;
    memset(ring_buff, ' ', sizeof(ring_buff));

    freq[T] = 0xffff;
    parent[R] = 0;

    r = N - F;
}

// Rebuilt the tree
void RebuildTree()
{
    unsigned i, j, k, f, l;

    // Collect leaf nodes in the first half of the table and replace the freq by (freq + 1) / 2
    for (i = j = 0; i < T; i++)
    {
        if (son[i] >= T)
        {
            freq[j] = (freq[i] + 1) / 2;
            son[j] = son[i];
            j++;
        }
    }

    // Begin constructing tree by connecting sons
    for (i = 0, j = N_CHAR; j < T; i += 2, j++)
    {
        k = i + 1;
        f = freq[j] = freq[i] + freq[k];
        for (k = j - 1; f < freq[k]; k--);
        k++;
        l = (j - k) * sizeof(*freq);

        memmove(&freq[k + 1], &freq[k], l);
        freq[k] = f;
        memmove(&son[k + 1], &son[k], l);
        son[k] = i;
    }

    // Connect parent
    for (i = 0; i < T; i++)
        if ((k = son[i]) >= T)
            parent[k] = i;
        else
            parent[k] = parent[k + 1] = i;
}


// Increment frequency of given code by one, and update tree
void UpdateTree(int c)
{
    unsigned i, j, k, l;

    if (freq[R] == MAX_FREQ)
        RebuildTree();

    c = parent[c + T];

    do
    {
        k = ++freq[c];

        // If the order is disturbed, exchange nodes
        if (k > freq[l = c + 1])
        {
            while (k > freq[++l]);
            l--;
            freq[c] = freq[l];
            freq[l] = k;

            i = son[c];
            parent[i] = l;
            if (i < T)
                parent[i + 1] = l;

            j = son[l];
            son[l] = i;

            parent[j] = c;
            if (j < T)
                parent[j + 1] = c;
            son[c] = j;

            c = l;
        }
    } while ((c = parent[c]) != 0);  // Repeat up to root
}

inline unsigned GetChar()
{
    uint8_t b;
    return pfile->read(&b, sizeof(b)) ? b : 0;
}

// Get one bit
unsigned GetBit()
{
    if (!uBits--)
    {
        uBitBuff |= GetChar() << 8;
        uBits = 7;
    }

    uBitBuff <<= 1;
    return (uBitBuff >> 16) & 1;
}

// Get one byte
unsigned GetByte()
{
    if (uBits < 8)
        uBitBuff |= GetChar() << (8 - uBits);
    else
        uBits -= 8;

    uBitBuff <<= 8;
    return (uBitBuff >> 16) & 0xff;
}

unsigned DecodeChar()
{
    unsigned c = son[R];

    // Travel from root to leaf, choosing the smaller child node (son[]) if the
    // read bit is 0, the bigger (son[]+1} if 1
    while (c < T)
        c = son[c + GetBit()];

    c -= T;
    UpdateTree(c);
    return c;
}

unsigned DecodePosition()
{
    unsigned i, j, c;

    // Recover upper 6 bits from table
    i = GetByte();
    c = d_code[i] << 6;
    j = d_len[i >> 4];

    // Read lower 6 bits verbatim
    for (j -= 2; j--; i = (i << 1) | GetBit());

    return c | (i & 0x3f);
}


// Unpack a given block into the supplied output buffer
int Unpack(MemFile& file, uint8_t* pOut_)
{
    unsigned i, j, c;
    auto uCount = 0;

    // Store the input start/end positions and prepare to unpack
    pfile = &file;
    Init();

    // Loop until we've processed all the input
    while (!file.eof())
    {
        c = DecodeChar();

        // Single output character?
        if (c < 256)
        {
            *pOut_++ = c;
            uCount++;

            // Update the ring buffer and position (wrapping if necessary)
            ring_buff[r++] = c;
            r &= (N - 1);
        }
        else
        {
            // Position in ring buffer and length
            i = (r - DecodePosition() - 1) & (N - 1);
            j = c - 255 + THRESHOLD;

            // Output the block
            for (unsigned k = 0; k < j; ++k)
            {
                c = ring_buff[(i + k) & (N - 1)];
                *pOut_++ = c;
                uCount++;

                ring_buff[r++] = c;
                r &= (N - 1);
            }
        }
    }

    // Return the unpacked size
    return uCount;
}

} // namespace LZSS
