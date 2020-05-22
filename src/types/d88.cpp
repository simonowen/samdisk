// Toshiba Pasopia 7:
//  http://www1.plala.or.jp/aoto/tech.htm

#include "SAMdisk.h"

enum : uint8_t
{
    D88_TYPE_2D = 0x00,     // DSSD
    D88_TYPE_2DD = 0x10,    // DSDD
    D88_TYPE_2HD = 0x20,    // DSHD
    D88_TYPE_1D = 0x30,     // SSSD
    D88_TYPE_1DD = 0x40     // SSDD
};

const int D88_HEADS = 2;
const int D88_CYLS_2D = 42;         // Low density wide tracks
const int D88_CYLS_2DD2HD = 82;     // 82x2 in double-sided image

struct D88_HEADER
{
    char     szTitle[17];
    uint8_t  abReserved[9];
    uint8_t  bReadOnly;
    uint8_t  bDiskType;
    uint32_t dwDiskSize;
    uint32_t adwTrackOffsets[D88_CYLS_2DD2HD * D88_HEADS];
};

struct D88_SECTOR
{
    uint8_t c, h, r, n;
    uint8_t bSectorsLow;
    uint8_t bSectorsHigh;
    uint8_t bDensity;
    uint8_t bDeleted;
    uint8_t bStatus;
    uint8_t abReserved[5];
    uint8_t bLengthLow;
    uint8_t bLengthHigh;
};


bool ReadD88(MemFile& file, std::shared_ptr<Disk>& disk)
{
    bool big_endian = false;

    D88_HEADER dh;
    if (!file.rewind() || !file.read(&dh, sizeof(dh)))
        return false;

    // Check for a supported disk type
    switch (dh.bDiskType)
    {
    case D88_TYPE_2D:   // 0x00
    case D88_TYPE_2DD:  // 0x10
    case D88_TYPE_2HD:  // 0x20
    case D88_TYPE_1D:   // 0x30
    case D88_TYPE_1DD:  // 0x40
        break;

    default:
        return false;
    }

    // Check for null terminator in the disk description
    void* pnull = memchr(&dh, 0, sizeof(dh.szTitle) + sizeof(&dh.abReserved));
    if (!pnull)
        return false;

    long disk_size = util::letoh(dh.dwDiskSize);

    // Check the size in the header matches the image size
    if (disk_size != file.size())
    {
        // Reject if the file extension isn't what we expect, to avoid false positives
        if (!IsFileExt(file.name(), "d88") &&
            !IsFileExt(file.name(), "88d") &&
            !IsFileExt(file.name(), "d77") &&
            !IsFileExt(file.name(), "d68") &&
            !IsFileExt(file.name(), "d98") &&
            !IsFileExt(file.name(), "d8u") &&
            !IsFileExt(file.name(), "1dd"))
            return false;

        // Warn about the difference but continue
        Message(msgWarning, "header size field (%u) doesn't match image size (%u)", dh.dwDiskSize, file.size());
    }

    // Store the disk type as meta-data
    disk->metadata["d88 disk type"] = util::fmt("%02X", dh.bDiskType);

    // Assume full cyl count until we discover otherwise. Determine the head count from the disk type.
    auto cyls = D88_CYLS_2DD2HD;
    auto heads = (dh.bDiskType == D88_TYPE_1D || dh.bDiskType == D88_TYPE_1DD) ? 1 : 2;

    // Copy any comment (we've already checked for a null-terminator above)
    if (dh.szTitle[0])
        disk->metadata["label"] = dh.szTitle;

    auto last_offset = 0;
    bool stop = false;

    for (auto cyl = 0; !stop && cyl < cyls; cyl++)
    {
        for (auto head = 0; !stop && head < heads; head++)
        {
            CylHead cylhead(cyl, head);
            int current_offset = dh.adwTrackOffsets[cyl * heads + head];

            // Skip blank tracks (no offset or pointing to end of disk)
            if (!current_offset || current_offset == file.size())
                continue;

            // If this is the first valid offset, use it to calculate the track offset table size
            if (!last_offset)
            {
                long table_size = current_offset - offsetof(D88_HEADER, adwTrackOffsets);
                if (table_size > 0)
                {
                    // Update the cylinder count it's beyond the table end
                    uint8_t max_cyls = static_cast<uint8_t>(table_size / 8);
                    if (cyls > max_cyls)
                        cyls = max_cyls;
                }
            }

            // Seek the start of the file data
            if (current_offset <= last_offset || !file.seek(current_offset))
            {
                Message(msgWarning, "invalid offset (%lu) for %s", current_offset, CH(cyl, head));
                stop = true;
                break;
            }

            last_offset = current_offset;
            Track track(MAX_SECTORS);
            D88_SECTOR d88s;

            do
            {
                // Read the next sector header
                if (!file.read(&d88s, sizeof(d88s)))
                    throw util::exception("short file reading ", cylhead);

                // Dummy sector indicating no sectors?
                if (!d88s.bSectorsLow && !d88s.bSectorsHigh)
                    break;

                // If the sector count is too large, use the upper byte instead.  It is unknown whether
                // this field is big or little endian, but we now support either.
                if (d88s.bSectorsLow == 0)
                {
                    d88s.bSectorsLow = d88s.bSectorsHigh;
                    d88s.bSectorsHigh = 0;

                    if (!big_endian)
                    {
                        Message(msgWarning, "correcting for big-endian sector counts");
                        big_endian = true;
                    }
                }

                // Ensure the sector count is within range, so we don't overrun the sector array and crash.
                if (d88s.bSectorsLow > MAX_SECTORS)
                    throw util::exception("invalid sector count (", d88s.bSectorsLow, ") on ", cylhead);

                DataRate datarate = (dh.bDiskType == D88_TYPE_2HD) ? DataRate::_500K : DataRate::_250K;
                Encoding encoding = (d88s.bDensity & 0x40) ? Encoding::FM : Encoding::MFM;
                Sector sector(datarate, encoding, Header(d88s.c, d88s.h, d88s.r, d88s.n));

                bool deleted_dam = (d88s.bStatus & 0xf0) == 0x10 || (d88s.bDeleted & 0x10);
                bool id_crc_error = (d88s.bStatus & 0xf0) == 0xa0;
                bool data_crc_error = (d88s.bStatus & 0xf0) == 0xb0;
                bool data_not_found = (d88s.bStatus & 0xf0) == 0xf0;

                if (id_crc_error)
                    sector.set_badidcrc();
                else if (!data_not_found)
                {
                    // Data may be shorter than the nature size (1942.D88)
                    auto length = (d88s.bLengthHigh << 8) | d88s.bLengthLow;
                    if (length > sector.size())
                        throw util::exception("too much data on ", cylhead, " sector ", sector.header.sector);

                    Data data(length);
                    file.read(data);
                    sector.add(std::move(data), data_crc_error, deleted_dam ? 0xf8 : 0xfb);
                }

                track.add(std::move(sector));
            } while (track.size() < d88s.bSectorsLow);

            // Add the track to the disk
            disk->write(cylhead, std::move(track));
        }
    }

    disk->strType = "D88";
    return true;
}

bool WriteD88(FILE* f_, std::shared_ptr<Disk>& disk)
{
    D88_HEADER dh = {};

    auto cyls = disk->cyls();
    auto heads = disk->heads();
    auto _1dd = IsFileExt(opt.szTarget, "1dd");

    if (_1dd && heads == 2)
        throw util::exception("can't write double-sided image to 1DD container");

    // Cap the cylinder count at the track offset table limit
    if (cyls > D88_CYLS_2DD2HD)
    {
        Message(msgWarning, "ignoring tracks >= %s", CylStr(D88_CYLS_2DD2HD));
        cyls = D88_CYLS_2DD2HD;
    }

    // Non-1DD output files are always double-sided
    if (!_1dd)
        heads = 2;

    // Use 1DD for .1dd output files, otherwise use 2D or 2DD depending on the cylinder count.
    // If we find any HD sectors we'll switch the type to 2HD later.
    dh.bDiskType = _1dd ? ((cyls <= D88_CYLS_2D) ? D88_TYPE_1D : D88_TYPE_1DD) :
        (cyls <= D88_CYLS_2D) ? D88_TYPE_2D : D88_TYPE_2DD;

    // Preserve any disk label
    if (disk->metadata.find("label") != disk->metadata.end())
        strncpy(dh.szTitle, disk->metadata["label"].c_str(), sizeof(dh.szTitle) - 1);

    // Skip the file header, which will be written at the end
    fseek(f_, sizeof(dh), SEEK_SET);

    Range(cyls, heads).each([&](const CylHead& cylhead) {
        auto& track = disk->read_track(cylhead);

        // Nothing to do for blank tracks
        if (!track.size())
            return;

        // Set the track offset to the current position
        dh.adwTrackOffsets[cylhead.cyl * heads + cylhead.head] = ftell(f_);

        for (auto& sector : track.sectors())
        {
            const auto& data = sector.data_copy();

            D88_SECTOR ds = {};
            ds.c = static_cast<uint8_t>(sector.header.cyl);
            ds.h = static_cast<uint8_t>(sector.header.head);
            ds.r = static_cast<uint8_t>(sector.header.sector);
            ds.n = static_cast<uint8_t>(sector.header.size);

            ds.bDensity = (sector.encoding == Encoding::FM) ? 0x40 : 0x00;  // note: set for FM!

            ds.bSectorsLow = track.size() & 0xff;
            ds.bSectorsHigh = 0;
            ds.bDeleted = sector.is_deleted() ? 0x10 : 0x00;

            auto data_length = sector.size();

            // For DITT compatibility, clip sectors using extended size codes
            if (sector.header.size >= 8)
                data_length = 256;

            ds.bLengthLow = data_length & 0xff;
            ds.bLengthHigh = static_cast<uint8_t>(data_length >> 8);

            // Determine the BIOS status byte for reading this sector
            if (sector.has_badidcrc())
                ds.bStatus = 0xa0;
            else if (!sector.has_data())
                ds.bStatus = 0xf0;
            else if (sector.has_baddatacrc())
                ds.bStatus = 0xb0;
            else if (sector.is_deleted())
                ds.bStatus = 0x10;
            else
                ds.bStatus = 0x00;


            // Convert 2DD to 2HD if HD content is found
            if (!_1dd && dh.bDiskType == D88_TYPE_2DD && sector.datarate == DataRate::_500K)
                dh.bDiskType = D88_TYPE_2HD;

            // The disk type and data rate must be consistent
            if ((dh.bDiskType == D88_TYPE_2HD && sector.datarate != DataRate::_500K) ||
                (dh.bDiskType != D88_TYPE_2HD && sector.datarate != DataRate::_250K &&
                    sector.datarate != DataRate::_300K))
            {
                throw util::exception(cylhead, " data rate (", sector.datarate, ") does not match disk type (", dh.bDiskType, ")");
            }

            // Write the sector header
            if (!fwrite(&ds, sizeof(ds), 1, f_))
                throw posix_error();

            // Write the sector data
            if (data.size() >= data_length)
            {
                if (!fwrite(data.data(), data_length, 1, f_))
                    throw posix_error();
            }
            else
            {
                Data fill(data_length - data.size());
                if (!fwrite(data.data(), data.size(), 1, f_) || !fwrite(fill.data(), fill.size(), 1, f_))
                    throw posix_error();
            }
        }
        });

    // Set the completed disk size
    dh.dwDiskSize = ftell(f_);

    // Write the file header to the start of the file
    fseek(f_, 0, SEEK_SET);
    if (!fwrite(&dh, sizeof(dh), 1, f_))
        throw posix_error();
    fseek(f_, 0, SEEK_END);

    return true;
}
