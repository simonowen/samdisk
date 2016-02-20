// High-level disk image/device reading and writing

#include "SAMdisk.h"
#include "record.h"
#include "SpectrumPlus3.h"
#include "types.h"
#include "BlockDevice.h"

bool ReadBuiltin (const std::string &path, std::shared_ptr<Disk> &disk);
bool UnwrapSDF (std::shared_ptr<Disk> &src_disk, std::shared_ptr<Disk> &disk);
bool ReadUnsupp (MemFile &file, std::shared_ptr<Disk> &disk);
bool WriteRecord (FILE* f_, std::shared_ptr<Disk> &disk);
bool ReadSCPDev (const std::string &path, std::shared_ptr<Disk> &disk);
bool ReadTrinLoad (const std::string &path, std::shared_ptr<Disk> &disk);
bool ReadBlkDev (const std::string &path, std::shared_ptr<Disk> &disk);

bool ReadImage (const std::string &path, std::shared_ptr<Disk> &disk, bool normalise)
{
	MemFile file;
	bool f = false;

	if (path.empty())
		throw util::exception("invalid path");
	else if (IsDir(path))
		throw util::exception("path is a directory");

	// Filter off non-file types
	if (IsBuiltIn(path))
		f = ReadBuiltin(path, disk);
	else if (IsTrinity(path))
		f = ReadTrinLoad(path, disk);
	else if (IsRecord(path))
		f = ReadRecord(path, disk);
	else if (util::lowercase(path) == "scp:")
		f = ReadSCPDev(path, disk);
	else if (IsBlockDevice(path))
		f = ReadBlkDev(path, disk);

	// Next try regular files (and archives)
	if (!f)
	{
		if (!file.open(path, !opt.nozip))
			return false;

		// Present the image to all types with read support
		for (auto p = aTypes; !f && p->pszType; ++p)
		{
			if (p->pfnRead) f = p->pfnRead(file, disk);
		}

		// Store the archive type the image was found in, if any
		if (f && file.compression() != Compress::None)
			disk->metadata["archive"] = to_string(file.compression());
	}
#if 0
	// Unwrap any sub-containers
	if (!f) f = UnwrapSDF(olddisk, disk);	// MakeSDF image
	if (!f) f = UnwrapCPM(olddisk, disk);	// BDOS CP/M record format
#endif

	if (!f)
		throw util::exception("unrecognised image format");

	if (normalise)
	{
		// ToDo: Make resize and flip optional?  replace fNormalise) and fLoadFilter_?
#if 0 // breaks with sub-ranges
		if (!opt.range.empty())
			disk->resize(opt.range.cyls(), opt.range.heads());
#endif
		// Forcibly correct +3 boot loader problems?
		if (opt.fix == 1)
			FixPlus3BootLoader(disk);

		if (opt.flip)
			disk->flip_sides();
	}

#if 0
	auto cyls = disk->cyls(), heads = disk->heads();
	for (uint8_t head = 0; head < heads; ++head)
	{
		// Determine any forced head value for sectors on this track (-1 for none)
		int nHeadVal = head ? opt.head1 : opt.head0;

		// If nothing forced and we're using a regular format, use its head values
		if (nHeadVal == -1 && olddisk->format.sectors)
			nHeadVal = head ? olddisk->format.head1 : olddisk->format.head0;

		for (uint8_t cyl = 0; cyl < cyls; ++cyl)
		{
			PTRACK pt = disk->GetTrack(cyl, head);

			// Optionally normalise the track, to allow 'scan' to normalise on the fly
			if (fNormalise_)
				pt->Normalise(fLoadFilter_);

			// Forced head?
			if (nHeadVal != -1)
			{
				for (int i = 0; i < pt->sectors; ++i)
					pt->sector[i].head = nHeadVal;
			}
		}
	}
#endif

	return f;
}


bool WriteImage (const std::string &path, std::shared_ptr<Disk> &disk)
{
	bool f = false;
	auto cpm_disk = std::make_shared<Disk>();

#if 0
	// ToDo
	// Wrap a CP/M image in a BDOS record container?
	if (opt.cpm)
		f = WrapCPM(disk, cpm_disk);
#endif

	// BDOS record?
	if (IsRecord(path))
		f = WriteRecord(path, cpm_disk ? cpm_disk : disk);

	// Normal image file
	else
	{
		auto p = aTypes;

		// Find the type matching the output file extension
		for (; p->pszType; ++p)
		{
			// Matching extension with write
			if (IsFileExt(path, p->pszType))
				break;
		}

		if (!p->pszType)
			throw util::exception("unknown output file type");
		else if (!p->pfnWrite)
			throw util::exception(p->pszType, " is not supported as an output type");

		// Create the output file
		// ToDo: change to stream or wrap in try-catch so file is closed
		FILE *file = fopen(path.c_str(), "wb");
		if (!file)
			throw posix_error(errno, path.c_str());

		try
		{
			// Write the image
			f = p->pfnWrite(file, disk);
			if (!f)
				throw util::exception("output type is unsuitable for source content");
		}
		catch (...)
		{
			fclose(file);
			std::remove(path.c_str());
			throw;
		}

		fclose(file);
	}

	return true;
}
