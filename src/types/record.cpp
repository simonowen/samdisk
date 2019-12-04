// BDOS record for Atom/Atom Lite HDD interface for SAM Coupe:
//   http://www.samcoupe-pro-dos.co.uk/edwin/software/bdos/bdos.htm

#include "SAMdisk.h"
#include "types.h"
#include "record.h"

bool ReadBDOS(const std::string& path, std::shared_ptr<Disk>& disk)
{
    auto record = 0;
    if (!IsRecord(path, &record))
        return false;

    auto file = path.substr(0, path.rfind(':'));    // ToDo: check substr is correct segment
    auto hdd = HDD::OpenDisk(file);

    if (!hdd)
        throw util::exception("invalid disk");

    return ReadRecord(*hdd, record, disk);
}

bool WriteBDOS(const std::string& path, std::shared_ptr<Disk>& disk)
{
    auto record = 0;
    if (!IsRecord(path, &record))
        return false;

    auto hdd_path = path.substr(0, path.rfind(':'));
    auto hdd = HDD::OpenDisk(hdd_path);

    if (!hdd)
        throw util::exception("invalid disk");

    return WriteRecord(*hdd, record, disk);
}


bool ReadRecord(HDD& hdd, int record, std::shared_ptr<Disk>& disk)
{
    MEMORY mem(MGT_DISK_SIZE);
    auto pdir = reinterpret_cast<MGT_DIR*>(mem.pb);

    BDOS_CAPS bdc;
    if (!IsBDOSDisk(hdd, bdc))
        throw util::exception("drive is not BDOS format");
    else if (record > bdc.records)
        throw util::exception("drive contains only ", bdc.records, " records");
    else if (!hdd.Seek(bdc.base_sectors + 1600 * (record - 1)))
        throw posix_error(errno, "seek");

    auto uSectors = (record < bdc.records) ? MGT_DISK_SIZE / hdd.sector_size : bdc.extra_sectors;

    if (hdd.Read(mem, uSectors, bdc.need_byteswap) != uSectors)
        throw posix_error(errno, "read");
    else if (memcmp(pdir->abBDOS, "BDOS", sizeof(pdir->abBDOS)) && !opt.nosig)
        throw util::exception("record ", record, " is not formatted");

    Data data(mem.size);
    std::copy(mem.pb, mem.pb + mem.size, data.begin());
    disk->format(Format(RegularFormat::MGT), data);
    /* TODO?
        olddisk->uRecord = uRecord_;
        olddisk->uRecordSize = uSectors;
    */
#if 0   // ToDo?
    // If we have no label, look up the record list label
    if (olddisk->strDiskLabel.empty())
    {
        MEMORY memDir(SECTOR_SIZE);
        auto list_sector = bdc.uBase - bdc.uRecordList + (uRecord_ - 1) / 32;
        if (hdd.Seek(list_sector) && hdd.Read(memDir, 1, bdc.fNeedSwap))
        {
            const char* pcszLabel = (const char*)memDir.pb + (((uRecord_ - 1) & 0x1f) << 4);
            olddisk->strDiskLabel = util::trim(std::string(pcszLabel, BDOS_LABEL_SIZE));
        }
    }

    // Format the complete disk using the data
    for (int head = 0; head < MGT_SIDES; head++)
    {
        for (int cyl = 0; cyl < MGT_TRACKS; cyl++, pb += MGT_TRACK_SIZE)
        {
            // First directory entry, with no existing label?
            if (cyl == 0 && head == 0 && disk->strDiskLabel.empty())
            {
                // Read the disk label
                MGT_DISK_INFO di;
                GetDiskInfo(pb, &di);
                olddisk->strDiskLabel = di.disk_label;
            }

            olddisk->FormatRegularTrack(cyl, head, &olddisk->format, pb);
        }
    }
#endif
    disk->strType = "BDOS Record";
    return true;
}

bool WriteRecord(HDD&/*hdd*/, int /*record*/, std::shared_ptr<Disk>&/*disk*/, bool /*format*/)
{
    throw std::logic_error("BDOS record writing not implemented");

#if 0 // ToDo!
    bool f = false;

    MGT_DISK_INFO di;
    BDOS_CAPS bdc;
    auto missing = 0;

    const Sector* ps = nullptr;

    if (!disk->find_sector(Header(0, 0, 1, 2), ps) || !IsMgtDirSector(*ps))
        throw util::exception("source disk is not MGT compatible");
    else if (!ps->has_data() || GetDiskInfo(ps->datas()[0].data(), &di)->bDirTracks != MGT_DIR_TRACKS)
        throw util::exception("source disk must have exactly ", MGT_DIR_TRACKS, " directory tracks");
    else if (!IsBDOSDisk(phdd_, &bdc))
        throw util::exception("target drive is not BDOS format");
    else if (uRecord_ == bdc.uRecords && bdc.uExtra >= 40)
        throw util::exception("writing to partial records is not currently supported");
    else if (uRecord_ > bdc.uRecords)
        throw util::exception("target drive contains only ", bdc.uRecords, " records");
    else if (hdd.Lock())
    {
        f = true;

        MEMORY mem(MGT_DISK_SIZE);
        PBYTE pb = mem;

#if 0   // ToDo!
        // Use the disk label, falling back on the MGT disk label
        std::string label = olddisk.strDiskLabel;
        if (label.empty())
            label = di.disk_label;

        // If we've still no label, and this isn't a format, generate one from the source
        if (label.empty() && !format)
        {
            char szPath[MAX_PATH], * p;
            char* psz = strncpy(szPath, opt.szSource, MAX_PATH);
            szPath[MAX_PATH - 1] = '\0';

            // Strip the path and file extension to leave basename
            for (; (p = strchr(psz, '/')); psz = p + 1);
            for (; (p = strchr(psz, '\\')); psz = p + 1);
            for (; (p = strrchr(psz, '.')); *p = '\0');

            di.disk_label = std::string(psz).substr(BDOS_LABEL_SIZE);
        }
#endif

        // Read the raw track data from the supplied disk
        for (int head = 0; head < MGT_SIDES; head++)
            for (int cyl = 0; cyl < MGT_TRACKS; cyl++, pb += MGT_TRACK_SIZE)
                missing += olddisk.ReadRegularTrack(cyl, head, &fmtMGT, pb);

        auto start_sector = bdc.uBase + (MGT_DISK_SIZE / hdd.uSectorSize) * (uRecord_ - 1);
        if (!hdd.Seek(start_sector))
            Error("seek");
        else
        {
            // Unless asked not to, set the signature needed for BDOS to recognise the record
            if (!opt.nosig && opt.fix != 0)
                memcpy(mem + 232, "BDOS", 4);

            // Write the record to disk
            if (!(f = (hdd.Write(mem, MGT_DISK_SIZE / hdd.uSectorSize, bdc.fNeedSwap) == MGT_DISK_SIZE / hdd.uSectorSize)))
                Error("write");

            // Determine the sector containing the appropriate record list entry
            auto list_sector = bdc.uBase - bdc.uRecordList + (uRecord_ - 1) / 32;

            // Read the sector to preserve existing entries
            if (f && hdd.Seek(list_sector) && hdd.Read(mem, 1, bdc.fNeedSwap))
            {
                // Determine the entry offset within the sector
                PBYTE pbLabel = mem + (((uRecord_ - 1) & 0x1f) << 4);

                // Set the entry label, padded with spaces and truncated if necessary
                label = label.substr(0, BDOS_LABEL_SIZE);
                memset(pbLabel, 0x00, BDOS_LABEL_SIZE);
                memcpy(pbLabel, label.c_str(), label.length());

                if (!hdd.Seek(list_sector) || !hdd.Write(mem, 1, bdc.fNeedSwap))
                    throw util::exception("failed to update record list");
            }
        }

        hdd.Unlock();
    }

    if (f && missing && !opt.minimal)
        Message(msgWarning, "source missing %d/%d sectors", missing, MGT_TRACKS * MGT_SIDES * fmtMGT.sectors);

    return f ? retOK : retReported;
#endif
}


bool UnwrapCPM(std::shared_ptr<Disk>&/*olddisk*/, std::shared_ptr<Disk>&/*newdisk*/)
{
    // Unwrapping must be requested
    if (!opt.cpm)
        return false;

    throw std::logic_error("CPM unwrapping not implemented");

    // ToDo!
#if 0
    // Determine the first track, for the CP/M image to fill the remainder of the MGT disk
    BYTE bFirstTrack = (MGT_DISK_SIZE - DOS_DISK_SIZE) / MGT_TRACK_SIZE;

    PCSECTOR ps = olddisk->GetSector(0, 0, 1, &fmtMGT);
    MGT_DIR* pdir = ps ? reinterpret_cast<MGT_DIR*>(ps->apbData[0]) : NULL;

    // Check for SPECIAL file using 1440 sectors and starting at the right sector map position
    if (!pdir || pdir->bType != 8 ||
        pdir->bSectorsHigh != ((DOS_DISK_SIZE / SECTOR_SIZE) >> 8) ||
        pdir->bSectorsLow != ((DOS_DISK_SIZE / SECTOR_SIZE) & 0xff) ||
        pdir->bStartTrack != bFirstTrack || pdir->bStartSector != 1)
    {
        // Silently ignore unsuitable source images, as the option may be meant for the target image
        return 0;
    }

    // CP/M option used
    opt.cpm--;

    MEMORY mem(DOS_DISK_SIZE);
    PBYTE pb = mem;

    for (BYTE head = 0; head < MGT_SIDES; head++)
    {
        for (BYTE cyl = 0; cyl < NORMAL_TRACKS; cyl++)
        {
            if (!head && cyl < bFirstTrack)
                continue;

            if (olddisk->ReadRegularTrack(cyl, head, &fmtMGT, pb))
                throw util::exception("CP/M track on ", CH(cyl, head), " is incomplete");

            pb += MGT_TRACK_SIZE;
        }
    }

    MemFile file;
    file.open(mem, mem.size, "record.cpm");

    olddisk.reset();
    return ReadRAW(file, olddisk, newdisk);
#endif
}

bool WrapCPM(std::shared_ptr<Disk>&/*disk*/, std::shared_ptr<Disk>&/*cpm_disk*/)
{
    throw std::logic_error("CPM wrapping not implemented");
    // ToDo!
#if 0
    MEMORY memcpm(DOS_DISK_SIZE), memblank(MGT_TRACK_SIZE);
    PBYTE pb = memcpm;
    auto missing = 0;

    // Sniff the track to determine a suitable base sector
    PCTRACK pt = olddisk.PeekTrack(0, 0);
    FORMAT fmtCPM = fmtDOSD;
    fmtCPM.base = 1 + (pt->sectors ? pt->sector[0].sector & 0xc0 : 0);

    // Extract the raw CP/M disk content from the source image
    for (BYTE cyl = 0; cyl < NORMAL_TRACKS; cyl++)
        for (BYTE head = 0; head < NORMAL_SIDES; head++, pb += DOS_TRACK_SIZE)
            missing += olddisk.ReadRegularTrack(cyl, head, &fmtCPM, pb);

    if (missing && !opt.minimal)
        Message(msgWarning, "source missing %d sectors", missing);

    // Wipe the source disk ready for the new format
    pb = memcpm;

    // Determine the first track, for the CP/M image to fill the remainder of the MGT disk
    BYTE bFirstTrack = (MGT_DISK_SIZE - DOS_DISK_SIZE) / MGT_TRACK_SIZE;

    for (BYTE head = 0; head < NORMAL_SIDES; head++)
    {
        for (BYTE cyl = 0; cyl < NORMAL_TRACKS; cyl++)
        {
            // Pre-CP/M track on head 0?
            if (!head && cyl < bFirstTrack)
            {
                // Format with default (zero) filler
                cpm_disk->FormatRegularTrack(cyl, head, &fmtMGT, memblank);

                // Cyl 0 head 0?
                if (!cyl)
                {
                    // First the first sector, which contains the first directory entry
                    PCSECTOR ps = olddisk.GetSector(0, 0, 1, &fmtMGT);
                    MGT_DIR* pdir = reinterpret_cast<MGT_DIR*>(ps->apbData[0]);

                    // Add a SPECIAL file entry spanning the CP/M content at the end of the disk
                    pdir->bType = 8;
                    memcpy(pdir->abName, "CP/M DISK ", sizeof(pdir->abName));
                    pdir->bSectorsHigh = (DOS_DISK_SIZE / SECTOR_SIZE) >> 8;
                    pdir->bSectorsLow = (DOS_DISK_SIZE / SECTOR_SIZE) & 0xff;
                    pdir->bStartTrack = bFirstTrack;
                    pdir->bStartSector = 1;
                    memset(pdir->abSectorMap + (((bFirstTrack - MGT_DIR_TRACKS) * MGT_SECTORS) >> 3),
                        0xff, (DOS_DISK_SIZE / SECTOR_SIZE) >> 3);
                }
            }
            else
            {
                // Format using the next portion of CP/M data
                cpm_disk->FormatRegularTrack(cyl, head, &fmtMGT, pb);
                pb += MGT_TRACK_SIZE;
            }
        }
    }

    /*
        PCSECTOR ps = pd_->GetSector(bFirstTrack,0,1,&fmtMGT);
    //  memcpy(ps->apbData[0], "\x03\x81\x50\x09\x02\x01\x04\x04\x2A\x52", 10);
        memcpy(ps->apbData[0], "\x01\x00\x28\x09\x02\x02\x03\x02\x2a\x52", 10);
    */
    return retOK;
#endif
}
