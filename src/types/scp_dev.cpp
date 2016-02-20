// SuperCard Pro real device wrapper

#include "SAMdisk.h"
#include "DemandDisk.h"
#include "BitstreamDecoder.h"
#include "SuperCardPro.h"

class SCPDevDisk final : public DemandDisk
{
public:
	explicit SCPDevDisk (std::unique_ptr<SuperCardPro> supercardpro)
		: m_supercardpro(std::move(supercardpro))
	{
		m_supercardpro->SelectDrive(0);
		m_supercardpro->EnableMotor(0);

		// Default to a slower step rate to be compatible with older drive,
		// unless the user says otherwise. Other parameters are defaults.
		auto step_delay = opt.newdrive ? 5000 : 10000;
		m_supercardpro->SetParameters(1000, step_delay, 1000, 15, 10000);

		// Move the head out before restoring back to TRACK0, just in case
		// something caused the physical head to have a negative position.
		m_supercardpro->StepTo(10);
		m_supercardpro->Seek0();
	}

	~SCPDevDisk ()
	{
		m_supercardpro->DisableMotor(0);
		m_supercardpro->DeselectDrive(0);
	}

protected:
	Track load (const CylHead &cylhead) override
	{
		m_supercardpro->SelectDrive(0);

		if (!m_supercardpro->StepTo(cylhead.cyl * opt.step) ||
			!m_supercardpro->SelectSide(cylhead.head))
		{
			throw util::exception(m_supercardpro->GetErrorStatusText());
		}

		std::vector<std::vector<uint32_t>> flux_revs;
		auto rev_limit = std::max(opt.retries, opt.rescans + 1);
		auto first = true;
		Track track;

		for (auto total_revs = 0; total_revs < rev_limit; )
		{
			auto revs = std::min(rev_limit - total_revs, SuperCardPro::MAX_FLUX_REVS);

			// Start with 2 revolutions by default, as it's faster if the track is error free.
			// This only applies if there isn't a custom rescan count requiring more.
			if (first && opt.rescans < 2)
				revs = 2;
			first = false;

			if (!m_supercardpro->ReadFlux(revs, flux_revs))
				throw util::exception(m_supercardpro->GetErrorStatusText());

			track.add(scan_flux(cylhead, flux_revs));
			total_revs += revs;

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
	std::unique_ptr<SuperCardPro> m_supercardpro;
};


bool ReadSCPDev (const std::string &/*path*/, std::shared_ptr<Disk> &disk)
{
	// ToDo: use path to select from multiple devices?

	auto supercardpro = SuperCardPro::Open();
	if (!supercardpro)
		throw util::exception("failed to open SuperCard Pro device");

	auto scp_dev_disk = std::make_shared<SCPDevDisk>(std::move(supercardpro));
	scp_dev_disk->extend(CylHead(83 - 1, 2 - 1));

	scp_dev_disk->strType = "SuperCard Pro";
	disk = scp_dev_disk;

	return true;
}
