// Create command

#include "SAMdisk.h"

bool CreateImage(const std::string& path, Range range)
{
    auto disk = std::make_shared<Disk>();

    // Start with legacy default formats, with automatic gap 3
    Format fmt = IsFileExt(path, "cpm") ? RegularFormat::ProDos : RegularFormat::MGT;
    fmt.gap3 = 0;

    // Allow everything about the format to be overridden, but check it
    fmt.Override(true);
    fmt.Validate();
    ValidateRange(range, MAX_TRACKS, MAX_SIDES);

    // Set the disk label, if supplied
    if (!opt.label.empty())
        disk->metadata["label"] = opt.label;

    // Extend or format the disk
    if (opt.noformat)
        disk->write(CylHead(range.cyl_end - 1, range.head_end - 1), Track());
    else
        disk->format(fmt);

    // Write to the output disk image
    WriteImage(path, disk);

    // Report the new disk parameters, unless it's already been displayed (raw)
    if (!IsFileExt(path, "raw"))
    {
        auto cyls = disk->cyls();
        auto heads = disk->heads();

        if (opt.noformat)
            util::cout << util::fmt("Created %2u cyl%s, %u head%s, unformatted.\n", cyls, (cyls == 1) ? "" : "s", heads, (heads == 1) ? "" : "s");
        else
        {
            util::cout << util::fmt("Created %2u cyl%s, %u head%s, %2u sector%s/track, %4u bytes/sector\n",
                cyls, (cyls == 1) ? "" : "s", heads, (heads == 1) ? "" : "s",
                fmt.sectors, (fmt.sectors == 1) ? "" : "s", fmt.sector_size());
        }
    }

    return true;
}

bool CreateHddImage(const std::string& path, int nSizeMB_)
{
    bool f = false;

    // If no sector count is specified, use the size parameter
    auto total_size = (opt.sectors == -1) ?
        static_cast<int64_t>(nSizeMB_) << 20 :
        static_cast<int64_t>(opt.sectors) << 9;

    if (total_size < 4 * 1024 * 1024)
        throw util::exception("needs image size in MB (>=4) or sector count with -s");

    // Create the specified HDD image, ensuring we don't overwrite any existing file
    auto hdd = HDD::CreateDisk(path, total_size, nullptr, false);
    if (!hdd)
        Error("create");
    else
    {
        // Zero-fill up to the required sector count
        f = hdd->Copy(nullptr, hdd->total_sectors, 0, 0, 0, "Creating");

        // If anything went wrong, remove the new file
        if (!f)
            unlink(path.c_str());
    }

    return f;
}
