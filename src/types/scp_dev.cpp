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

		m_supercardpro->Seek0();
	}

	~SCPDevDisk ()
	{
		m_supercardpro->DisableMotor(0);
		m_supercardpro->DeselectDrive(0);
	}

protected:
	TrackData load (const CylHead &cylhead, bool first_read) override
	{
		FluxData flux_revs;
		auto rev_limit = std::min(REMAIN_READ_REVS, SuperCardPro::MAX_FLUX_REVS);
		auto revs = first_read ? FIRST_READ_REVS : rev_limit;

		if (!m_supercardpro->SelectDrive(0) ||
			!m_supercardpro->StepTo(cylhead.cyl) ||
			!m_supercardpro->SelectSide(cylhead.head) ||
			!m_supercardpro->ReadFlux(revs, flux_revs))
		{
			throw util::exception(m_supercardpro->GetErrorStatusText());
		}

		return TrackData(cylhead, std::move(flux_revs));
	}

	bool preload (const Range &/*range*/, int /*cyl_step*/) override
	{
		return false;
	}

private:
	std::unique_ptr<SuperCardPro> m_supercardpro;
};


static std::string VersionString(int version)
{
	std::stringstream ss;
	if (version)
		ss << (version >> 4) << "." << (version & 0xf);
	return ss.str();
}

bool ReadSCPDev (const std::string &/*path*/, std::shared_ptr<Disk> &disk)
{
	// ToDo: use path to select from multiple devices?

	auto supercardpro = SuperCardPro::Open();
	if (!supercardpro)
		throw util::exception("failed to open SuperCard Pro device");

	int hw_version{0}, fw_version{0};
	supercardpro->GetInfo(hw_version, fw_version);

	auto scp_dev_disk = std::make_shared<SCPDevDisk>(std::move(supercardpro));
	scp_dev_disk->extend(CylHead(83 - 1, 2 - 1));

	scp_dev_disk->metadata["hw_version"] = VersionString(hw_version);
	scp_dev_disk->metadata["fw_version"] = VersionString(fw_version);

	scp_dev_disk->strType = "SuperCard Pro";
	disk = scp_dev_disk;

	return true;
}
