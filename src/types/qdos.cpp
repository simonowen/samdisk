// QDOS = Sinclair QL
//
// http://www.qdosmsq.dunbar-it.co.uk/doku.php?id=qdosmsq:fs:dsdd

#include "SAMdisk.h"
#include "qdos.h"

bool ReadQDOS (MemFile &file, std::shared_ptr<Disk> &disk)
{
	QDOS_HEADER qh{};
	if (!file.rewind() || !file.read(&qh, sizeof(qh)))
		return false;

	if (memcmp(qh.signature, "QL5A", 4) && memcmp(qh.signature, "QL5B", 4))
		return false;

	auto total_sectors = util::betoh(qh.total_sectors);
	auto sectors_per_track = util::betoh(qh.sectors_per_track);
	auto sectors_per_cyl = util::betoh(qh.sectors_per_cyl);
	auto cyls_per_side = util::betoh(qh.cyls_per_side);

	Format fmt { RegularFormat::QDOS };
	fmt.cyls = cyls_per_side;
	fmt.heads = sectors_per_cyl / sectors_per_track;
	fmt.sectors = sectors_per_track;
	fmt.datarate = (fmt.track_size() > 6000) ? DataRate::_500K : DataRate::_250K;

	if (total_sectors != fmt.total_sectors())
		Message(msgWarning, "sector count (%u) doesn't match geometry (%d)", total_sectors, fmt.total_sectors());

	if (fmt.disk_size() != file.size())
		Message(msgWarning, "image file isn't expected size (%d)", fmt.disk_size());

	auto label = util::trim(std::string(qh.label, sizeof(qh.label)));
	disk->metadata["label"] = label;
	disk->metadata["type"] = std::string(qh.signature, sizeof(qh.signature));

	disk->format(fmt, file.data());
	disk->strType = "QDOS (Sinclair QL)";
	return true;
}

bool WriteQDOS (FILE* f_, std::shared_ptr<Disk> &disk)
{
	const Sector *sector = nullptr;
	if (!disk->find(Header(0, 0, 1, 2), sector))
		return false;

	auto &data = sector->data_copy();
	if (data.size() < 512 || memcmp(data.data(), "QL5", 3))
		return false;

	auto pqh = reinterpret_cast<const QDOS_HEADER *>(data.data());
	auto total_sectors = util::betoh(pqh->total_sectors);
	auto cyls_per_side = util::betoh(pqh->cyls_per_side);
	auto sectors_per_cyl = util::betoh(pqh->sectors_per_cyl);
	auto sectors_per_track = util::betoh(pqh->sectors_per_track);

	Format fmt { RegularFormat::QDOS };
	fmt.cyls = cyls_per_side;
	fmt.heads = sectors_per_track ? (sectors_per_cyl / sectors_per_track) : 0;
	fmt.sectors = sectors_per_track;
	fmt.datarate = (fmt.track_size() > 6000) ? DataRate::_500K : DataRate::_250K;
	fmt.Validate();

	if (total_sectors != fmt.total_sectors())
		Message(msgWarning, "sector count (%u) doesn't match geometry (%d)", total_sectors, fmt.total_sectors());

	WriteRegularDisk(f_, *disk, fmt);
	return true;
}
