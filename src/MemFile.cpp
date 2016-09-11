// Memory-backed files used for disk images

#include "SAMdisk.h"
#include "MemFile.h"

#ifdef HAVE_ZLIB
#include <zlib.h>
#include "unzip.h"
#endif

#ifdef HAVE_BZIP2
#define BZ_NO_STDIO
#include <bzlib.h>
#endif

std::string to_string (const Compress &compression)
{
	switch (compression)
	{
		default:
		case Compress::None:	return "none";
		case Compress::Zip:		return "zip";
		case Compress::Gzip:	return "gzip";
		case Compress::Bzip2:	return "bzip2";
	}
}


bool MemFile::open (const std::string &path_, bool uncompress)
{
	MEMORY mem(MAX_IMAGE_SIZE + 1);
	size_t uRead = 0;

	// Check if zlib is available
#ifdef HAVE_ZLIBSTAT
	bool have_zlib = true;
#elif defined(HAVE_ZLIB)
	bool have_zlib = CheckLibrary("zlib", "zlibVersion") && zlibVersion()[0] == ZLIB_VERSION[0];
#else
	bool have_zlib = false;
#endif

#ifdef HAVE_ZLIB
	// Try opening as a zip file
	if (uncompress && have_zlib)
	{
		unzFile hfZip = unzOpen(path_.c_str());
		if (hfZip)
		{
			int nRet;
			unz_file_info sInfo;
			uLong ulMaxSize = 0;

			// Iterate through the contents of the zip looking for a file with a suitable size
			for (nRet = unzGoToFirstFile(hfZip); nRet == UNZ_OK; nRet = unzGoToNextFile(hfZip))
			{
				char szFile[MAX_PATH];

				// Get details of the current file
				unzGetCurrentFileInfo(hfZip, &sInfo, szFile, MAX_PATH, nullptr, 0, nullptr, 0);

				// Ignore directories and empty files
				if (!sInfo.uncompressed_size)
					continue;

				// If the file extension is recognised, read the file contents
				// ToDo: GetFileType doesn't really belong here?
				if (GetFileType(szFile) != ftUnknown && unzOpenCurrentFile(hfZip) == UNZ_OK)
				{
					nRet = unzReadCurrentFile(hfZip, mem, static_cast<unsigned int>(mem.size));
					unzCloseCurrentFile(hfZip);
					break;
				}

				// Rememeber the largest uncompressed file size
				if (sInfo.uncompressed_size > ulMaxSize)
					ulMaxSize = sInfo.uncompressed_size;
			}

			// Did we fail to find a matching extension?
			if (nRet == UNZ_END_OF_LIST_OF_FILE)
			{
				// Loop back over the archive
				for (nRet = unzGoToFirstFile(hfZip); nRet == UNZ_OK; nRet = unzGoToNextFile(hfZip))
				{
					// Get details of the current file
					unzGetCurrentFileInfo(hfZip, &sInfo, nullptr, 0, nullptr, 0, nullptr, 0);

					// Open the largest file found about
					if (sInfo.uncompressed_size == ulMaxSize && unzOpenCurrentFile(hfZip) == UNZ_OK)
					{
						nRet = unzReadCurrentFile(hfZip, mem, static_cast<unsigned int>(mem.size));
						unzCloseCurrentFile(hfZip);
						break;
					}
				}
			}

			// Close the zip archive
			unzClose(hfZip);

			// Stop if something went wrong
			if (nRet < 0)
				throw util::exception("zip extraction failed (", nRet, ")");

			uRead = nRet;
			m_compress = Compress::Zip;
		}
		else
		{
			// Open as gzipped or uncompressed
			gzFile gf = gzopen(path_.c_str(), "rb");
			if (gf == Z_NULL)
				throw posix_error(errno, path_.c_str());

			uRead = gzread(gf, mem, static_cast<unsigned>(mem.size));
			m_compress = (gztell(gf) != FileSize(path_)) ? Compress::Gzip : Compress::None;
			gzclose(gf);
		}
	}
	else
#endif // HAVE_ZLIB
	{
		// Open as normal file if we couldn't use zlib above
		FILE *f = fopen(path_.c_str(), "rb");
		if (!f)
			throw posix_error(errno, path_.c_str());

		uRead = fread(mem, 1, mem.size, f);
		fclose(f);
		m_compress = Compress::None;
	}


	// zip compressed? (and not handled above)
	if (uncompress && mem[0U] == 'P' && mem[1U] == 'K')
		throw util::exception("zlib (zlib1.dll) is required for zip support");

	// gzip compressed?
	if (uncompress && mem[0U] == 0x1f && mem[1U] == 0x8b && mem[2U] == 0x08)
	{
		if (!have_zlib)
			throw util::exception("zlib (zlib1.dll) is required for gzip support");

#ifdef HAVE_ZLIB
		MEMORY mem2(MAX_IMAGE_SIZE + 1);
		uint8_t flags = mem[3];
		auto pb = mem.pb + 10, pbEnd = mem.pb + mem.size;

		if (flags & 0x04)	// EXTRA_FIELD
		{
			if (pb < pbEnd - 1)
				pb += 2 + pb[0] + (pb[1] << 8);
		}

		if (flags & 0x08 || flags & 0x10)	// ORIG_NAME or COMMENT
		{
			while (pb < pbEnd && *pb++);
		}

		if (flags & 0x02)	// HEAD_CRC
			pb += 2;

		z_stream stream = {};
		stream.next_in = pb;
		stream.avail_in = (pb < pbEnd) ? static_cast<uInt>(pbEnd - pb) : 0;
		stream.next_out = mem2.pb;
		stream.avail_out = mem2.size;

		auto zerr = inflateInit2(&stream, -MAX_WBITS);
		if (zerr == Z_OK)
		{
			zerr = inflate(&stream, Z_FINISH);
			inflateEnd(&stream);
		}

		if (zerr != Z_STREAM_END)
			throw util::exception("gzip decompression failed (", zerr, ")");

		memcpy(mem.pb, mem2.pb, uRead = stream.total_out);
		m_compress = Compress::Zip;
#endif
	}

	// bzip2 compressed?
	if (uncompress && mem[0U] == 'B' && mem[1U] == 'Z')
	{
#ifdef HAVE_BZIP2
		if (!CheckLibrary("bzip2", "BZ2_bzBuffToBuffDecompress"))
#endif
			throw util::exception("libbz2 (bzip2.dll) is required for bzip2 support");

#ifdef HAVE_BZIP2
		MEMORY mem2(MAX_IMAGE_SIZE + 1);

		auto uBzRead = static_cast<unsigned>(mem2.size);
		auto bzerr = BZ2_bzBuffToBuffDecompress(reinterpret_cast<char *>(mem2.pb), &uBzRead, reinterpret_cast<char *>(mem.pb), uRead, 0, 0);
		if (bzerr != BZ_OK)
			throw util::exception("bzip2 decompression failed (", bzerr, ")");

		memcpy(mem.pb, mem2.pb, uRead = uBzRead);
		m_compress = Compress::Bzip2;
#endif // HAVE_BZIP2
	}

	if (uRead <= MAX_IMAGE_SIZE)
		return open(mem.pb, static_cast<int>(uRead), path_);

	throw util::exception("file size too big");
}

bool MemFile::open (const void *buf, int len, const std::string &path_)
{
	auto pb = reinterpret_cast<const uint8_t *>(buf);

	m_data.clear();
	m_it = m_data.insert(m_data.begin(), pb, pb + len);
	m_path = path_;
	return true;
}


const Data &MemFile::data () const
{
	return m_data;
}

int MemFile::size () const
{
	return static_cast<int>(m_data.size());
}

int MemFile::remaining () const
{
	return static_cast<int>(m_data.end() - m_it);
}

const std::string &MemFile::path () const
{
	return m_path;
}

const char *MemFile::name () const
{
	std::string::size_type pos = m_path.rfind(PATH_SEPARATOR_CHR);
	return m_path.c_str() + ((pos == m_path.npos) ? 0 : pos + 1);
}

Compress MemFile::compression () const
{
	return m_compress;
}

std::vector<uint8_t> MemFile::read (int len)
{
	auto avail_bytes = std::min(len, remaining());
	return std::vector<uint8_t>(m_it, m_it + avail_bytes);
}

bool MemFile::read (void *buf, int len)
{
	auto avail = std::min(len, remaining());
	if (avail != len)
		return false;

	if (avail)
	{
		memcpy(buf, &(*m_it), avail);	// make this safer when callers can cope
		m_it += avail;
	}
	return true;
}

int MemFile::read (void *buf, int len, int count)
{
	if (!len) return count;	// can read as many zero-length units as requested!

	auto avail_items = std::min(count, remaining() / len);
	read(buf, avail_items * len);
	return avail_items;
}

bool MemFile::rewind ()
{
	return seek(0);
}

bool MemFile::seek (int offset)
{
	m_it = m_data.begin() + std::min(offset, static_cast<int>(m_data.size()));
	return tell() == offset;
}

int MemFile::tell () const
{
	return static_cast<int>(m_it - m_data.begin());
}

bool MemFile::eof () const
{
	return m_it == m_data.end();
}
