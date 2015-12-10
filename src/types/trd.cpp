// Betadisk / TR-DOS for Russian Spectrum clones:
//
// Later extended to support up to 1MB images (128 cyls)

#include "SAMdisk.h"
#include "trd.h"

bool ReadTRD (MemFile &file, std::shared_ptr<Disk> &disk)
{
	uint8_t disk_type = 0x00;
	if (!IsFileExt(file.name(), "trd"))
		return false;

	// Read the disk type byte
	if (!file.seek(TRD_SECTOR_SIZE * 8 + 227) || !file.read(&disk_type, sizeof(disk_type)))
		return false;

	// Check for known disk types
	if ((disk_type < 0x16 || disk_type > 0x19))
		return false;

	// Determine disk geometry from type
	auto cyls = (disk_type & 1) ? 40 : 80;
	auto heads = (disk_type & 2) ? 2 : 1;
	auto disk_end_pos = 9 * TRD_SECTOR_SIZE; // Minimum length covers volume information

	file.rewind();

	MEMORY mem(TRD_TRACK_SIZE);
	const auto pbDiskInfo = mem + TRD_SECTOR_SIZE * 8;
	file.read(mem, TRD_SECTOR_SIZE, 9);

	auto free_blocks = (pbDiskInfo[230] << 8) | pbDiskInfo[229];
	auto used_blocks = TRD_SECTORS; // Directory track

	bool valid_dir = true;
	auto num_files = 0;
	auto deleted_files = 0;
	for (auto i = 0; valid_dir && i < TRD_MAXFILES; i++)
	{
		const auto pb = mem + i * 16;

		if (*pb == 0x01)
			deleted_files++;
		else if (*pb)
		{
			num_files++;

			int sectors = pb[13];
			int sector = pb[14];
			int track = pb[15];

			// Keep track of the sectors used
			used_blocks += sectors;

			// Determine the end LBA block from the start position and sector count
			auto end_lba = ((track << 4) | (sector & 0x0f)) + sectors;

			// Calculate the file offset of the end, and update the maximum if it's beyond it
			auto end_pos = end_lba * TRD_SECTOR_SIZE;
			if (end_pos > disk_end_pos) disk_end_pos = end_pos;

			// Check for correct first file position (sector0,track1)
			valid_dir &= (i || (sector == 0 && track == 1));
		}
	}

	// If this is an extended image size, calculate the cylinder count to use
	if (disk_end_pos > TRD_SIZE_80_2 && disk_type == 0x16)
	{
		auto nDiskSize = (used_blocks + free_blocks) * TRD_SECTOR_SIZE;
		cyls = SizeToCylsTRD(nDiskSize);
	}

	// Warn things weren't quite as expected
	if (!valid_dir)
		Message(msgWarning, "inconsistencies found in TRD directory");

	Format fmt { RegularFormat::TRDOS };
	fmt.cyls = cyls;
	fmt.heads = heads;

	file.rewind();
	disk->format(fmt, file.data());
	disk->strType = "TRD";

	return true;
}

bool WriteTRD (FILE* /*f_*/, std::shared_ptr<Disk> &/*disk*/)
{
	throw std::logic_error("not implemented");
#if 0
	FORMAT fmt = fmtTRDOS, *pf_ = &fmt;
	unsigned uMissing = 0;
	bool f = true;

	// Check the type byte is valid, or we won't be able to read the image we've written
	PCSECTOR ps = pd_->GetSector(0, 0, 9, pf_);
	uint8_t disk_type = ps ? ps->apbData[0][227] : 0;

	// Ensure the type byte is suitable
	if (disk_type < 0x16 || disk_type > 0x19)
		return retUnsuitableTarget;

	uint8_t cyls = (disk_type & 1) ? 40 : 80;
	uint8_t heads = (disk_type & 2) ? 2 : 1;
	long disk_end_pos = 9 * TRD_SECTOR_SIZE; // Minimum length covers volume information
	unsigned used_blocks = TRD_SECTORS; // Directory track

	MEMORY mem(pf_->sectors * TRD_SECTOR_SIZE);

	for (uint8_t cyl = 0; f && cyl < cyls; cyl++)
	{
		for (uint8_t head = 0; f && head < heads; head++)
		{
			uMissing += pd_->ReadRegularTrack(cyl, head, pf_, mem);

			// First track?
			if (cyl == 0 && head == 0)
			{
				// Locate the directory information
				const auto pbDiskInfo = mem + TRD_SECTOR_SIZE * 8;

				// Iterate through each file
				for (int i = 0; i < TRD_MAXFILES; i++)
				{
					const auto pb = mem + i * 16;

					// Stop looping if there's no file in this slot
					if (!*pb)
						break;

					unsigned sectors = pb[13];
					unsigned sector = pb[14];
					unsigned track = pb[15];

					// Keep track of the sectors used
					used_blocks += sectors;

					// Determine the end LBA block from the start position and sector count
					unsigned end_lba = ((track << 4) | (sector & 0x0f)) + sectors;

					// Calculate the file offset of the end, and update the maximum if it's beyond it
					long end_pos = end_lba * TRD_SECTOR_SIZE;
					if (end_pos > disk_end_pos) disk_end_pos = end_pos;
				}

				// If this is an extended image size, calculate the cylinder count to use
				if (disk_end_pos > TRD_SIZE_80_2 && disk_type == 0x16)
				{
					unsigned free_blocks = (pbDiskInfo[230] << 8) | pbDiskInfo[229];
					int nDiskSize = (used_blocks + free_blocks) * TRD_SECTOR_SIZE;
					int trdcyls = SizeToCylsTRD(nDiskSize);

					// If no custom cyl range was supplied, extend to the required size.
					if (!opt.range.customcyls)
						cyls = trdcyls;
					// Otherwise cap at the user range
					else
						cyls = std::min(trdcyls, opt.range.cylto + 1);
				}
			}

			for (uint8_t sector = 0; f && sector < pf_->sectors; sector++)
			{
				const auto pb = mem.pb + sector * TRD_SECTOR_SIZE;
				long lPos = (((cyl*heads + head) * pf_->sectors) + sector) * TRD_SECTOR_SIZE;

				// Skip writing we're beyond the used portion of the disk
				if (opt.trim && lPos >= disk_end_pos)
					continue;

				// Seek to the sector offset and write it
				fseek(f_, lPos, SEEK_SET);
				f = fwrite(pb, TRD_SECTOR_SIZE, 1, f_) == 1;
			}
		}
	}

	if (f && uMissing)
		Message(msgWarning, "source missing %u/%u sectors", uMissing, cyls*heads*pf_->sectors);

	return f ? retOK : retWriteError;
#endif
}


// Return the cylinder count needed for a given disk size
int SizeToCylsTRD (int nSizeBytes_)
{
	// Minimum of 640K
	if (nSizeBytes_ <= TRD_SIZE_80_2)
		return 80;

	// Maximum of 1MB
	if (nSizeBytes_ > TRD_SIZE_128_2)
		return TRD_MAX_TRACKS;

	// Round to the nearest full cylinder (8K)
	int nBlock = 16 * 256 * 2;
	int nSizeRound = ((nSizeBytes_ + nBlock - 1) / nBlock) * nBlock;

	// Return the cylinder count
	return nSizeRound / (TRD_TRACK_SIZE*TRD_MAX_SIDES);
}
