// Didaktik D80:
//  http://web.archive.org/web/20041020185446/http://zoom.czweb.org/files/mdos.htm

#include "SAMdisk.h"

#define D80_SIGNATURE	"SDOS"


bool ReadD80 (MemFile &file, std::shared_ptr<Disk> &disk)
{
	uint8_t ab[256];
	if (!file.rewind() || !file.read(&ab, sizeof(ab)))
		return false;

	// Check the signature and the duplicate geometry block
	if (memcmp(ab + 204, D80_SIGNATURE, sizeof(D80_SIGNATURE) - 1) || memcmp(ab + 177, ab + 181, 3))
		return false;

	uint8_t heads = (ab[177] & 0x10) ? 2 : 1;
	uint8_t cyls = ab[178];
	uint8_t sectors = ab[179];

	Format fmt { RegularFormat::D80 };
	fmt.cyls = cyls;
	fmt.heads = heads;
	fmt.sectors = sectors;

	ValidateGeometry(fmt);

	// Allow cylinder count correction if the image size is a multiple of the track size
	if (opt.fix != 0 && file.size() != fmt.disk_size() && !(file.size() % (heads * fmt.track_size())))
	{
		fmt.cyls = static_cast<uint8_t>(file.size() / (heads * fmt.track_size()));
		Message(msgWarning, "corrected cylinder count to match disk size");
	}

	file.rewind();
	disk->format(fmt, file.data());
	disk->strType = "D80";

	return true;
}
