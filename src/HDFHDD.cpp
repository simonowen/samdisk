// HDF HDD disk image files

#include "SAMdisk.h"
#include "HDFHDD.h"

/*static*/ bool HDFHDD::IsRecognised (const std::string &path)
{
	int h = -1;
	RS_IDE sHeader = {};

	// Open as read-write, falling back on read-only
	if ((h = open(path.c_str(), O_RDWR | O_SEQUENTIAL | O_BINARY)) == -1 &&
		(h = open(path.c_str(), O_RDONLY | O_SEQUENTIAL | O_BINARY)) == -1)
		return false;

	  // Read the file header and check the signature string
	if (read(h, &sHeader, sizeof(sHeader)) != sizeof(sHeader) ||
		memcmp(sHeader.szSignature, "RS-IDE", sizeof(sHeader.szSignature)))
	{
		close(h);
		return false;
	}

	// Appears valid
	close (h);
	return true;
}


bool HDFHDD::Open (const std::string &path)
{
	// Open as read-write, falling back on read-only
	if ((h = open(path.c_str(), O_RDWR | O_SEQUENTIAL | O_BINARY)) == -1 &&
		(h = open(path.c_str(), O_RDONLY | O_SEQUENTIAL | O_BINARY)) == -1)
		return false;

	RS_IDE sHeader = {};

	// Read the header and check for signature
	if (read(h, &sHeader, sizeof(sHeader)) == sizeof(sHeader) && !sHeader.bFlags &&
		!memcmp(sHeader.szSignature, "RS-IDE", sizeof(sHeader.szSignature)))
	{
		// Read start of sector data, and determine identify length from that
		data_offset = (sHeader.bOffsetHigh << 8) | sHeader.bOffsetLow;
		sIdentify.len = data_offset - sizeof(sHeader);

		// Limit identify data size to a single sector
		if (sIdentify.len > SECTOR_SIZE)
			sIdentify.len = SECTOR_SIZE;

		// Read the identify data from the image
		if (read(h, sIdentify.byte, sIdentify.len) == static_cast<int>(sIdentify.len))
		{
			// Sector size is fixed at 512 bytes
			sector_size = SECTOR_SIZE;

			// Byte and sector counts are taken from the file size (excluding the header)
			total_bytes = FileSize(path) - data_offset;
			total_sectors = static_cast<unsigned>(total_bytes / sector_size);

			// Update CHS from the identify data
			SetIdentifyData(&sIdentify);

			return true;
		}
	}

	return false;
}

bool HDFHDD::Create (const std::string &path, uint64_t total_bytes_, const IDENTIFYDEVICE *pIdentify_, bool fOverwrite_/*=false*/)
{
	bool fRet = true;

	if ((h = open(path.c_str(), O_CREAT | O_RDWR | O_SEQUENTIAL | O_BINARY | (fOverwrite_ ? 0 : O_EXCL), S_IREAD | S_IWRITE)) == -1)
		return false;

	// Set the new disk geometry
	data_offset = 0;
	sector_size = SECTOR_SIZE;
	total_bytes = total_bytes_;
	total_sectors = static_cast<unsigned>(total_bytes_ / sector_size);

	// Preserve the source identify data if it's complete, otherwise generate new
	if (pIdentify_ && pIdentify_->len == sizeof(*pIdentify_))
		SetIdentifyData(pIdentify_);
	else
		SetIdentifyData();

	// HDF v1.0 header
	RS_IDE sHeader = { {'R','S','-','I','D','E'}, 0x1a, 0x10, 0x00, 0,0, {} };

	// Are we creating an HDF image?
	if (IsFileExt(path, "hdf"))
	{
		// Are we forced to use HDF v1.0?
		if (opt.hdf == 10)
		{
			// v1.0 has a 128-byte limit for the header+identify data
			sIdentify.len = 128 - sizeof(RS_IDE);
		}
		else
		{
			// v1.1 preserves all identify data, and has a new version number
			sHeader.bRevision = 0x11;
		}

		// Complete the HDD data offset now it's known
		data_offset = sizeof(RS_IDE) + sIdentify.len;
		sHeader.bOffsetLow = data_offset & 0xff;
		sHeader.bOffsetHigh = static_cast<uint8_t>(data_offset >> 8);
	}

#ifdef _WIN32
	HANDLE hfile = reinterpret_cast<HANDLE>(_get_osfhandle(h));

	// Ensure there's space for the full file, then truncate and write just the header
	if (_lseeki64(h, total_bytes + data_offset, SEEK_SET) != static_cast<int64_t>(total_bytes + data_offset) ||
		!SetEndOfFile(hfile) ||
		_lseeki64(h, 0, SEEK_SET) != 0 ||
		!SetEndOfFile(hfile))
		fRet = false;
#endif

	// If this is an HDF image, write the header and identity data
	if (fRet && data_offset &&
		(write(h, &sHeader, sizeof(sHeader)) != sizeof(sHeader) ||
		 write(h, sIdentify.byte, sIdentify.len) != static_cast<int>(sIdentify.len)))
		fRet = false;

	  // If anything went wrong, delete the (possibly partial) file
	if (!fRet)
	{
		close(h);
		h = -1;

		unlink(path.c_str());
	}

	return fRet;
}
