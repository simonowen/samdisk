// KryoFlux real device wrapper

#include "SAMdisk.h"
#include "DemandDisk.h"
#include "BitstreamDecoder.h"
#include "KryoFlux.h"

class KFDevDisk final : public DemandDisk
{
public:
	explicit KFDevDisk (std::unique_ptr<KryoFlux> kryoflux)
		: m_kryoflux(std::move(kryoflux))
	{
		try
		{
			m_kryoflux->SetMinTrack(0);
			m_kryoflux->SetMaxTrack(83 - 1);

			m_kryoflux->SelectDevice(0);
			m_kryoflux->EnableMotor(1);

			m_kryoflux->Seek(0);
		}
		catch (...)
		{
			throw util::exception("failed to initialise KryoFlux device");
		}
	}

	~KFDevDisk ()
	{
		m_kryoflux->Seek(0);
		m_kryoflux->Reset();
	}

protected:
	Track load (const CylHead &cylhead) override
	{
		m_kryoflux->SelectDevice(0);
		m_kryoflux->EnableMotor(1);

		m_kryoflux->Seek(cylhead.cyl * opt.step);
		m_kryoflux->SelectSide(cylhead.head);

		std::vector<std::vector<uint32_t>> flux_revs;
		auto rev_limit = std::max(opt.retries, opt.rescans + 1);
		auto first = true;
		Track track;

		for (auto total_revs = 0; total_revs < rev_limit; )
		{
			auto revs = rev_limit - total_revs;

			// Start with 1 revolution as it's faster if the track is error free,
			// but respect any rescan count requiring more.
			if (first)
				revs = std::max(1, opt.rescans + 1);
			first = false;

			std::vector<std::string> warnings;
			m_kryoflux->ReadFlux(revs + 1, flux_revs, warnings);
			for (auto &w : warnings)
				Message(msgWarning, "%s on %s", w.c_str(), CH(cylhead.cyl, cylhead.head));

			track.add(scan_flux(cylhead, flux_revs));
			total_revs += static_cast<int>(flux_revs.size()) - 1;

			// Have we read at least the minimum number of track scans?
			if (total_revs >= opt.rescans)
			{
				auto it = std::find_if(track.begin(), track.end(), [] (const Sector &sector) {
					return !sector.has_data() || sector.has_baddatacrc();
				});

				// Stop if there are no errors left to fix
				if (it == track.end())
					break;
			}
		}

		return track;
	}

	void preload (const Range &/*range*/) override
	{
		// Pre-loading is not supported on real devices
	}

private:
	std::unique_ptr<KryoFlux> m_kryoflux;
};


bool ReadKFDev (const std::string &/*path*/, std::shared_ptr<Disk> &disk)
{
	// ToDo: use path to select from multiple devices?

	auto kryoflux = KryoFlux::Open();
	if (!kryoflux)
		throw util::exception("failed to open KryoFlux device");

	auto kf_dev_disk = std::make_shared<KFDevDisk>(std::move(kryoflux));
	kf_dev_disk->extend(CylHead(83 - 1, 2 - 1));

	kf_dev_disk->strType = "KryoFlux";
	disk = kf_dev_disk;

	return true;
}
