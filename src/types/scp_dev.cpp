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
		m_supercardpro->Seek(0, 0);
	}

	~SCPDevDisk ()
	{
		m_supercardpro->DeselectDrive(0);
	}

protected:
	Track load (const CylHead &cylhead) override
	{
		m_supercardpro->SelectDrive(0);

		if (m_supercardpro->Seek(cylhead.cyl, cylhead.head))
		{
			std::vector<std::vector<uint32_t>> flux_revs;

			// Read at least 2 revolutions, but increase if requested.
			// Divide by 3 as each is scanned at 3 slightly different data rates.
			auto revs = std::min(2, opt.retries / 3);

			if (m_supercardpro->ReadFlux(revs, flux_revs))
				return scan_flux(cylhead, flux_revs);
		}

		throw util::exception(m_supercardpro->GetErrorStatusText());
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
		throw util::exception("SuperCard Pro device not found");

	auto scp_dev_disk = std::make_shared<SCPDevDisk>(std::move(supercardpro));

	for (auto head = 0; head < 2; ++head)
		for (auto cyl = 0; cyl < 83; ++cyl)
			scp_dev_disk->extend(CylHead(cyl, head));

	scp_dev_disk->strType = "SuperCard Pro";
	disk = scp_dev_disk;

	return true;
}
