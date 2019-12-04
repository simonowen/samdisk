// SuperCard Pro real device wrapper
//  http://www.cbmstuff.com/downloads/scp/scp_sdk.pdf

#include "SAMdisk.h"
#include "DemandDisk.h"
#include "BitstreamDecoder.h"
#include "SuperCardPro.h"

class SCPDevDisk final : public DemandDisk
{
public:
    explicit SCPDevDisk(std::unique_ptr<SuperCardPro> supercardpro)
        : m_supercardpro(std::move(supercardpro))
    {
        m_supercardpro->SelectDrive(0);
        m_supercardpro->EnableMotor(0);

        // Default to a slower step rate to be compatible with older drive,
        // unless the user says otherwise.
        auto step_delay = opt.newdrive ? 5000 : 16'000;
        m_supercardpro->SetParameters(1000, step_delay, 1000, 50, 10'000);

        m_supercardpro->StepTo(1);
        m_supercardpro->Seek0();
    }

    ~SCPDevDisk()
    {
        m_supercardpro->DisableMotor(0);
        m_supercardpro->DeselectDrive(0);
    }

protected:
    TrackData load(const CylHead& cylhead, bool first_read) override
    {
        FluxData flux_revs;
        auto rev_limit = std::min(REMAIN_READ_REVS, SuperCardPro::MAX_FLUX_REVS);
        auto revs = first_read ? FIRST_READ_REVS : rev_limit;

        if (!m_supercardpro->SelectDrive(0) ||
            !m_supercardpro->EnableMotor(0) ||
            !m_supercardpro->StepTo(cylhead.cyl) ||
            !m_supercardpro->SelectSide(cylhead.head) ||
            !m_supercardpro->ReadFlux(revs, flux_revs))
        {
            throw util::exception(m_supercardpro->GetErrorStatusText());
        }

        return TrackData(cylhead, std::move(flux_revs));
    }

    bool preload(const Range&/*range*/, int /*cyl_step*/) override
    {
        return false;
    }

    void save(TrackData& trackdata) override
    {
        auto preferred = trackdata.preferred();
        auto& flux_revs = preferred.flux();
        auto flux_times = flux_revs[0];

        auto& bitstream = preferred.bitstream();
        if (bitstream.splicepos() && flux_revs.size() > 1)
        {
            int64_t extra_ns = bitcell_ns(bitstream.datarate) * (bitstream.splicepos() + 128);
            for (auto flux_time : flux_revs[1])
            {
                if (flux_time > extra_ns)
                    break;

                flux_times.push_back(flux_time);
                extra_ns -= flux_time;
            }
        }

        if (m_supercardpro->SelectDrive(0) &&
            m_supercardpro->EnableMotor(0) &&
            m_supercardpro->StepTo(trackdata.cylhead.cyl) &&
            m_supercardpro->SelectSide(trackdata.cylhead.head))
        {
            if (!track_time_ns)
            {
                FluxData flux_rev{};
                if (!m_supercardpro->ReadFlux(1, flux_rev))
                    throw util::exception(m_supercardpro->GetErrorStatusText());

                track_time_ns = std::accumulate(flux_rev[0].begin(), flux_rev[0].end(), 0ULL);

                constexpr auto write_margin_ns = 1'000'000; // 1ms
                track_time_ns -= write_margin_ns;
            }

            auto total_time_ns = std::accumulate(flux_times.begin(), flux_times.end(), 0ULL);
            if (total_time_ns > track_time_ns)
            {
                scale_flux(flux_times, track_time_ns, total_time_ns);
                total_time_ns = std::accumulate(flux_times.begin(), flux_times.end(), 0ULL);
            }

            if (m_supercardpro->WriteFlux(flux_times))
                return;
        }

        throw util::exception(m_supercardpro->GetErrorStatusText());
    }

private:
    std::unique_ptr<SuperCardPro> m_supercardpro;
    uint64_t track_time_ns{ 0 };
};


static std::string VersionString(int version)
{
    std::stringstream ss;
    if (version)
        ss << (version >> 4) << "." << (version & 0xf);
    return ss.str();
}

bool ReadSuperCardPro(const std::string& path, std::shared_ptr<Disk>& disk)
{
    // ToDo: use path to select from multiple devices?
    if (util::lowercase(path) != "scp:")
        return false;

    auto supercardpro = SuperCardPro::Open();
    if (!supercardpro)
        throw util::exception("failed to open SuperCard Pro device");

    int hw_version = 0, fw_version = 0;
    supercardpro->GetInfo(hw_version, fw_version);

    auto scp_dev_disk = std::make_shared<SCPDevDisk>(std::move(supercardpro));
    scp_dev_disk->extend(CylHead(83 - 1, 2 - 1));

    scp_dev_disk->metadata["hw_version"] = VersionString(hw_version);
    scp_dev_disk->metadata["fw_version"] = VersionString(fw_version);

    scp_dev_disk->strType = "SuperCard Pro";
    disk = scp_dev_disk;

    return true;
}

bool WriteSuperCardPro(const std::string& path, std::shared_ptr<Disk>& disk)
{
    if (util::lowercase(path) != "scp:")
        return false;

    auto supercardpro = SuperCardPro::Open();
    if (!supercardpro)
        throw util::exception("failed to open SuperCard Pro device");

    auto scp_dev_disk = std::make_shared<SCPDevDisk>(std::move(supercardpro));
    ValidateRange(opt.range, MAX_TRACKS, MAX_SIDES, 1, scp_dev_disk->cyls(), scp_dev_disk->heads());

    opt.range.each([&](const CylHead& cylhead)
        {
            auto trackdata = disk->read(cylhead);
            Message(msgStatus, "Writing %s", CH(cylhead.cyl, cylhead.head));
            scp_dev_disk->write(std::move(trackdata));
        });

    return true;
}
