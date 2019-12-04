// High-level disk image/device reading and writing

#include "SAMdisk.h"
#include "record.h"
#include "SpectrumPlus3.h"
#include "types.h"
#include "BlockDevice.h"

bool UnwrapSDF(std::shared_ptr<Disk>& src_disk, std::shared_ptr<Disk>& disk);

bool ReadImage(const std::string& path, std::shared_ptr<Disk>& disk, bool normalise)
{
    MemFile file;
    bool f = false;

    if (path.empty())
        throw util::exception("invalid empty path");

    // Try devices first as the path may use a custom syntax
    for (auto p = aDeviceTypes; !f && p->pszType; ++p)
    {
        if (p->pfnRead) f = p->pfnRead(path, disk);
    }

    if (!f && IsDir(path))
        throw util::exception("path is a directory");

    // Next try regular files (and archives)
    if (!f)
    {
        if (!file.open(path, !opt.nozip))
            return false;

        // Present the image to all types with read support
        for (auto p = aImageTypes; !f && p->pszType; ++p)
        {
            if (p->pfnRead) f = p->pfnRead(file, disk);
        }

        // Store the archive type the image was found in, if any
        if (f)
        {
            if (file.compression() != Compress::None)
                disk->metadata["archive"] = to_string(file.compression());
            if (file.path().rfind(file.name()) + file.name().size() != file.path().size())
                disk->metadata["filename"] = file.name();
        }
    }

#if 0
    // Unwrap any sub-containers
    if (!f) f = UnwrapSDF(olddisk, disk);   // MakeSDF image
    if (!f) f = UnwrapCPM(olddisk, disk);   // BDOS CP/M record format
#endif

    if (!f)
        throw util::exception("unrecognised disk image format");

    if (normalise)
    {
        // ToDo: Make resize and flip optional?  replace fNormalise) and fLoadFilter_?
#if 0 // breaks with sub-ranges
        if (!opt.range.empty())
            disk->resize(opt.range.cyls(), opt.range.heads());
#endif
        // Forcibly correct +3 boot loader problems?
        if (opt.fix == 1)
            FixPlus3BootLoader(disk);

        if (opt.flip)
            disk->flip_sides();
    }

#if 0
    auto cyls = disk->cyls(), heads = disk->heads();
    for (uint8_t head = 0; head < heads; ++head)
    {
        // Determine any forced head value for sectors on this track (-1 for none)
        int nHeadVal = head ? opt.head1 : opt.head0;

        // If nothing forced and we're using a regular format, use its head values
        if (nHeadVal == -1 && olddisk->format.sectors)
            nHeadVal = head ? olddisk->format.head1 : olddisk->format.head0;

        for (uint8_t cyl = 0; cyl < cyls; ++cyl)
        {
            PTRACK pt = disk->GetTrack(cyl, head);

            // Optionally normalise the track, to allow 'scan' to normalise on the fly
            if (fNormalise_)
                pt->Normalise(fLoadFilter_);

            // Forced head?
            if (nHeadVal != -1)
            {
                for (int i = 0; i < pt->sectors; ++i)
                    pt->sector[i].head = nHeadVal;
            }
        }
    }
#endif

    return f;
}


bool WriteImage(const std::string& path, std::shared_ptr<Disk>& disk)
{
    bool f = false;

#if 0
    // TODO: Wrap a CP/M image in a BDOS record container
    auto cpm_disk = std::make_shared<Disk>();
    if (opt.cpm)
        f = WrapCPM(disk, cpm_disk);
#endif

    // Try devices first as the path may use a custom syntax
    for (auto p = aDeviceTypes; !f && p->pszType; ++p)
    {
        if (p->pfnWrite) f = p->pfnWrite(path, disk);
    }

    // Normal image file
    if (!f)
    {
        auto p = aImageTypes;

        // Find the type matching the output file extension
        for (; p->pszType; ++p)
        {
            // Matching extension with write
            if (IsFileExt(path, p->pszType))
                break;
        }

        if (!p->pszType)
            throw util::exception("unknown output file type");
        else if (!p->pfnWrite)
            throw util::exception(util::format(p->pszType, " is not supported for output"));

        FILE* file = fopen(path.c_str(), "wb");
        if (!file)
            throw posix_error(errno, path.c_str());

        try
        {
            // Write the image
            f = p->pfnWrite(file, disk);
            if (!f)
                throw util::exception("output type is unsuitable for source content");
        }
        catch (...)
        {
            fclose(file);
            std::remove(path.c_str());
            throw;
        }

        fclose(file);
    }

    return true;
}
