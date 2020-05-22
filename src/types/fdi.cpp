// FDI = Full Disk Image for Spectrum:
//  http://www.worldofspectrum.org/faq/reference/formats.htm#FDI

#include "SAMdisk.h"

#define FDI_SIGNATURE           "FDI"

struct FDI_HEADER
{
    uint8_t abSignature[3];     // "FDI"
    uint8_t bWriteProtect;      // Non-zero if write-protected
    uint8_t bTracks[2];         // Tracks per side
    uint8_t bSides[2];          // Disk sides
    uint8_t bDescOffset[2];     // Offset of disk description
    uint8_t bDataOffset[2];     // Data offset
    uint8_t bExtraSize[2];      // Length of additional header in following field
//  uint8_t abExtra[];          // Additional data
                                // Description data here (if description offset is non-zero)
                                // Main data starts here
};

struct FDI_TRACK
{
    uint8_t abTrackOffset[4];   // Track data offset, relative to start of main data block
    uint8_t abReserved[2];      // Reserved (should be zero)
    uint8_t sectors;            // Number of sectors in track
                                // bSectors * FDI_SECTOR structures follow here
};

struct FDI_SECTOR
{
    uint8_t bTrack;             // Track number in ID field
    uint8_t bSide;              // Side number in ID field
    uint8_t bSector;            // Sector number in ID field
    uint8_t bSize;              // Sector size indicator:  (128 << bSize) gives the real size
    uint8_t bFlags;             // Flags detailing special sector conditions
    uint8_t bSectorOffset[2];   // Offset of sector data, relative to start of track data
};


bool ReadFDI(MemFile& file, std::shared_ptr<Disk>& disk)
{
    FDI_HEADER fh;
    if (!file.rewind() || !file.read(&fh, sizeof(fh)))
        return false;
    else if (memcmp(fh.abSignature, FDI_SIGNATURE, sizeof(fh.abSignature)))
        return false;

    int cyls = fh.bTracks[0];
    int heads = fh.bSides[0];
    Format::Validate(cyls, heads);

    auto data_pos = (fh.bDataOffset[1] << 8) | fh.bDataOffset[0];
    auto header_pos = static_cast<int>(sizeof(FDI_HEADER)) + ((fh.bExtraSize[1] << 8) | fh.bExtraSize[0]);
    file.seek(header_pos);

    //  uint8_t* pbData = pb + fh.bDataOffset[0];
    //  auto pbHeaders = (uint8_t*)&fh.bExtraSize + sizeof(fh.bExtraSize) + ((fh.bExtraSize[1] << 8) | fh.bExtraSize[0]);

    for (auto cyl = 0; cyl < cyls; ++cyl)
    {
        for (auto head = 0; head < heads; ++head)
        {
            CylHead cylhead(cyl, head);
            Track track;

            FDI_TRACK ft;
            if (!file.read(&ft, sizeof(ft)))
                throw util::exception("short file reading ", cylhead);

            if (ft.sectors > MAX_SECTORS)
                throw util::exception(cylhead, " has too many sectors (", ft.sectors, ")");

            auto track_pos = data_pos + (/*(ft.abTrackOffset[3] << 24) |*/ (ft.abTrackOffset[2] << 16) |
                (ft.abTrackOffset[1] << 8) | ft.abTrackOffset[0]);

            for (int i = 0; i < ft.sectors; ++i)
            {
                FDI_SECTOR fs;
                if (!file.read(&fs, sizeof(fs)))
                    throw util::exception("short file reading ", cylhead, " sector index ", i);

                Sector sector(DataRate::_250K, Encoding::MFM, Header(fs.bTrack, fs.bSide, fs.bSector, fs.bSize));

                bool deleted_data = (fs.bFlags & 0x80) != 0;
                bool no_data = (fs.bFlags & 0x40) != 0;
                bool bad_data = true;   // until we learn otherwise
                uint8_t dam = deleted_data ? 0xf8 : 0xfb;

                auto old_pos = file.tell();
                auto sector_pos = track_pos + ((fs.bSectorOffset[1] << 8) | fs.bSectorOffset[0]);
                if (!file.seek(sector_pos))
                    throw util::exception("failed to seek to ", cylhead, " sector ", fs.bSector, " data @", sector_pos);

                // Use only 2 bits of the size, to match FD1793 behaviour
                uint8_t b1793Size = fs.bSize & 3;
                auto sector_size = Sector::SizeCodeToLength(b1793Size);

                Data data(sector_size); // always allocate full PC size, plus room for CRC
                file.read(data.data(), 1, static_cast<int>(data.size()));
                file.seek(old_pos);

                // Loop through valid size codes to check the flag bits
                for (uint8_t size = 0; !no_data && size <= 3; ++size)
                {
                    auto ssize = Sector::SizeCodeToLength(size);

                    // Does the size match the PC treatment of the size code?
                    if (ssize == Sector::SizeCodeToLength(fs.bSize))
                    {
                        // If the flags say no error, clear the data CRC error
                        if (fs.bFlags & (1 << size))
                            bad_data = false;
                    }
                    // Or is it within the current FD1793 size?
                    else if (size <= b1793Size)
                    {
                        CRC16 crc(CRC16::A1A1A1);
                        crc.add(dam);
                        crc.add(data.data(), ssize);

                        // If the size matches, there are no CRC bytes in the data
                        if (size == b1793Size)
                        {
                            // Do the flags indicate the CRC is good?
                            if (fs.bFlags & (1 << size))
                            {
                                // Add the calculated CRC bytes to to preserve the status
                                data.push_back(crc >> 8);
                                data.push_back(crc & 0xff);
                            }
                        }
                        else
                        {
                            // Continue reading into the data to check the CRC
                            crc.add(data.data() + ssize, 2);

                            // Ensure the CRC status matches the flags
                            if (!!(fs.bFlags & (1 << size)) != (crc ? 0 : 1))
                                Message(msgWarning, "inconsistent CRC flag for size=%d for %s", sector_size, CHR(cyl, head, fs.bSector));
                        }
                    }
                    else
                    {
                        // Skip clearing the flag so we warn any set bit below
                        continue;
                    }

                    // Reset the bit for this size, whether or not we used it
                    fs.bFlags &= ~(1 << size);
                }

                // Warn about any remaining flags, in case we're missing something
                if (fs.bFlags & ~0xc0)
                    Message(msgWarning, "unexpected flags (%02X) on %s", fs.bFlags, CHSR(cyl, head, i, fs.bSector));

                sector.add(std::move(data), bad_data, dam);
                track.add(std::move(sector));
            }

            disk->write(cylhead, std::move(track));
        }
    }

    auto desc_pos = (fh.bDescOffset[1] << 8) | fh.bDescOffset[0];
    if (desc_pos && file.seek(desc_pos))
    {
        // Read the comment block
        auto len = (data_pos > desc_pos) ? (data_pos - desc_pos) : 256;
        std::vector<char> comment(len);
        if (!file.read(comment))
            throw util::exception("short file reading comment");
        std::string str = std::string(comment.data(), comment.size());

        // To avoid confusion, don't store the default TDCVT comment
        if (str.substr(0, 29) != "\r\n'This file created by TDCVT" && str.substr(0, 3) != "FDI")
            disk->metadata["comment"] = str;
    }

    disk->strType = "FDI";
    return true;
}


bool WriteFDI(FILE* /*f_*/, std::shared_ptr<Disk>&/*disk*/)
{
    throw std::logic_error("FDI writing not implemented");
#if 0
    FDI_HEADER fh = {};

    memcpy(&fh.abSignature, "FDI", sizeof(fh.abSignature));
    fh.bWriteProtect = 0;
    fh.bTracks[0] = pd_->bUsedCyls;
    fh.bSides[0] = pd_->bUsedHeads;

    auto data_offset = 0, data_size = 0;

    for (;;)
    {
        fseek(f_, 0, SEEK_SET);

        // Write the file header
        if (!fwrite(&fh, sizeof(fh), 1, f_))
            return retWriteError;

        for (uint8_t cyl = 0; cyl < pd_->bUsedCyls; cyl++)
        {
            for (uint8_t head = 0; head < pd_->bUsedHeads; head++)
            {
                PCTRACK pt = pd_->PeekTrack(cyl, head);
                //              PCSECTOR ps = pt->sector;

                                // Fail if the track isn't double-density
                if (pt->sectors && pt->encrate != (FD_OPTION_MFM | FD_RATE_250K) && pt->encrate != (FD_OPTION_MFM | FD_RATE_300K))
                {
                    throw util::exception(CH(cyl, head), " is not double-density");
                    return retUnsuitableTarget;
                }

                FDI_TRACK ft = {};
                ft.bSectors = pt->sectors;
                ft.abTrackOffset[0] = static_cast<uint8_t>(data_size & 0xff);
                ft.abTrackOffset[1] = static_cast<uint8_t>(data_size >> 8);
                ft.abTrackOffset[2] = static_cast<uint8_t>(data_size >> 16);
                ft.abTrackOffset[3] = 0;

                // Write the track header (first pass)
                if (!fwrite(&ft, sizeof(ft), 1, f_))
                    return retWriteError;

                // No data on this track yet
                auto track_size = 0;

                for (int i = 0; i < ft.bSectors; i++)
                {
                    PCSECTOR ps = &pt->sector[i];
                    //                  auto data_size = Sector::SizeCodeToLength(ps->size);

                    FDI_SECTOR fs = {};
                    fs.bTrack = ps->cyl;
                    fs.bSide = ps->head;
                    fs.bSector = ps->sector;
                    fs.bSize = ps->size;
                    fs.bFlags = 0;

                    if (ps->IsDeleted()) fs.bFlags |= 0x80;
                    if (ps->IsNoID() || ps->IsIDCRC() || ps->IsNoData()) fs.bFlags |= 0x40;

                    // Use only 2 bits of the size, to match FD1793 behaviour
                    uint8_t b1793Size = fs.bSize & 3;
                    auto u1793Size = Sector::SizeCodeToLength(b1793Size);

                    // Loop through valid size codes to generate flag bits
                    for (uint8_t size = 0; size < 4; size++)
                    {
                        // Does the size match?
                        if (size == Sector::SizeCodeToLength(fs.bSize))
                        {
                            // If the flags say no error, set the CRC OK bit for this size
                            if (!ps->IsDataCRC())
                                fs.bFlags |= (1 << size);
                        }
                        // Or is there enough data for CRC calculation?
                        else if (ps->uData >= Sector::SizeCodeToLength(size) + 2)
                        {
                            auto sector_size = Sector::SizeCodeToLength(size);

                            CRC16 crc;
                            if (ps->IsMFM()) crc.add("\xa1\xa1\xa1", 3);
                            crc.add(ps->GetDAM());
                            crc.add(ps->apbData[0], sector_size + 2);

                            // If the CRC is valid, set the appropriate bit
                            if (!crc)
                                fs.bFlags |= (1 << size);
                        }
                    }

                    // Data field to write?
                    if (!(fs.bFlags & 0x40) && data_offset)
                    {
                        if (track_size >= 0x10000)
                        {
                            throw util::exception(CH(cyl, head), " is too big for FDI's 16-bit sector offsets");
                            return retUnsuitableTarget;
                        }

                        // Warn if the 2-bit size will truncate a non-error sector
                        if (!ps->IsDataCRC() && ps->size > 3)
                        {
                            Message(msgWarning, "clipping %s size from %u to %u bytes", CHR(cyl, head, ps->sector), Sector::SizeCodeToLength(ps->size), u1793Size);
                        }

                        // Write the offset to the sector's data within the track data
                        fs.bSectorOffset[0] = (track_size & 0xff);
                        fs.bSectorOffset[1] = static_cast<uint8_t>(track_size >> 8);

                        LONG lPos = ftell(f_);
                        fseek(f_, data_offset + data_size + track_size, SEEK_SET);

                        if (!fwrite(ps->apbData[0], u1793Size, 1, f_))
                            return retWriteError;

                        fseek(f_, lPos, SEEK_SET);
                        track_size += u1793Size;
                    }

                    // Write the sector header (first pass)
                    if (!fwrite(&fs, sizeof(fs), 1, f_))
                        return retWriteError;
                }

                // Include the track size in the overall data size
                data_size += track_size;
            }
        }

        // On the second pass we're all done
        if (data_offset)
            break;

        // Do we have a comment?
        if (!pd_->strComment.empty())
        {
            auto offset = ftell(f_);
            fh.bDescOffset[0] = (offset & 0xff);
            fh.bDescOffset[1] = static_cast<uint8_t>(offset >> 8);

            fwrite(pd_->strComment.c_str(), pd_->strComment.length() + 1, 1, f_);
        }

        // We now have the data offset ready for the second pass
        data_offset = ftell(f_);
        fh.bDataOffset[0] = (data_offset & 0xff);
        fh.bDataOffset[1] = static_cast<uint8_t>(data_offset >> 8);
    }

    return retOK;
#endif
}
