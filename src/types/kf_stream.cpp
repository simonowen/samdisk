// KryoFlux STREAM format:
//  http://www.softpres.org/kryoflux:stream

#include "SAMdisk.h"
#include "KryoFlux.h"


bool ReadSTREAM(MemFile& file, std::shared_ptr<Disk>& disk)
{
    uint8_t type;

    std::string path = file.path();
    if (!IsFileExt(path, "raw") || !file.rewind() || !file.read(&type, sizeof(type)) || type != KryoFlux::OOB)
        return false;

    auto len = path.length();
    if (len < 8 || !std::isdigit(static_cast<uint8_t>(path[len - 8])) ||
        !std::isdigit(static_cast<uint8_t>(path[len - 7])) ||
        path[len - 6] != '.' ||
        !std::isdigit(static_cast<uint8_t>(path[len - 5])))
        return false;

    auto ext = path.substr(len - 3);
    path = path.substr(0, len - 8);

    auto missing0 = 0, missing1 = 0, missing_total = 0;

    Range(MAX_TRACKS, MAX_SIDES).each([&](const CylHead& cylhead) {
        auto track_path = util::fmt("%s%02u.%u.%s", path.c_str(), cylhead.cyl, cylhead.head, ext.c_str());

        MemFile f;
        if (!IsFile(track_path) || !f.open(track_path))
        {
            missing0 += (cylhead.head == 0);
            missing1 += (cylhead.head == 1);
        }
        else
        {
            // Track anything missing within the bounds of existing tracks
            if (cylhead.head == 0)
            {
                missing_total += missing0;
                missing0 = 0;
            }
            else
            {
                missing_total += missing1;
                missing1 = 0;
            }

            std::vector<std::string> warnings;
            auto flux_revs = KryoFlux::DecodeStream(f.data(), warnings);
            for (auto& w : warnings)
                Message(msgWarning, "%s on %s", w.c_str(), CH(cylhead.cyl, cylhead.head));

            disk->write(cylhead, std::move(flux_revs));
        }
        });

    if (missing_total)
        Message(msgWarning, "%d missing or invalid stream track%s", missing_total, (missing_total == 1) ? "" : "s");

    disk->strType = "STREAM";

    return true;
}
