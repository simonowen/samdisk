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

#ifdef HAVE_LZMA
#include <lzma.h>
#endif

std::string to_string(const Compress& compression)
{
    switch (compression)
    {
    default:
    case Compress::None:    return "none";
    case Compress::Zip:     return "zip";
    case Compress::Gzip:    return "gzip";
    case Compress::Bzip2:   return "bzip2";
    case Compress::Xz:      return "xz";
    }
}


bool MemFile::open(const std::string& path_, bool uncompress)
{
    std::string filename;
    MEMORY mem(MAX_IMAGE_SIZE + 1);
    size_t uRead = 0;

    // Check if zlib is available
#ifndef HAVE_ZLIB
    bool have_zlib = false;
#else
    bool have_zlib = zlibVersion()[0] == ZLIB_VERSION[0];

    // Read start of file to check for compression signatures.
    if (uncompress && have_zlib)
    {
        FILE* f = fopen(path_.c_str(), "rb");
        if (!f)
            throw posix_error(errno, path_.c_str());

        if (!fread(mem, 1, 2, f))
            mem[0] = mem[1] = '\0';

        fclose(f);
    }

    if (uncompress && have_zlib)
    {
        // Require zip file header magic.
        if (mem[0U] == 'P' && mem[1U] == 'K')
        {
            unzFile hfZip = unzOpen(path_.c_str());
            if (!hfZip)
                throw util::exception("bad zip file");

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
                    filename = szFile;
                    break;
                }

                // Remember the largest uncompressed file size
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

            if (nRet < 0)
                throw util::exception("zip extraction failed (", nRet, ")");

            uRead = nRet;
            m_compress = Compress::Zip;
        }
        // Require gzip file header magic.
        else if (mem[0U] == 0x1f && mem[1U] == 0x8b)
        {
            // Open as gzipped or uncompressed
            gzFile gf = gzopen(path_.c_str(), "rb");
            if (gf == Z_NULL)
                throw posix_error(errno, path_.c_str());

            uRead = gzread(gf, mem, static_cast<unsigned>(mem.size));
            m_compress = gzdirect(gf) ? Compress::None : Compress::Gzip;
            gzclose(gf);

            // If gzipped, attempt to extract the original filename
            if (m_compress == Compress::Gzip)
            {
                std::ifstream zs(path_.c_str(), std::ios_base::binary);
                std::vector<uint8_t> zbuf((std::istreambuf_iterator<char>(zs)),
                    std::istreambuf_iterator<char>());

                z_stream stream{};
                stream.next_in = zbuf.data();
                stream.avail_in = static_cast<uInt>(zbuf.size());
                stream.next_out = zbuf.data();  // same as next_in!
                stream.avail_out = 0;           // we don't want data

                auto zerr = inflateInit2(&stream, 16 + MAX_WBITS); // 16=gzip
                if (zerr == Z_OK)
                {
                    Bytef name[MAX_PATH]{};
                    gz_header header{};
                    header.name = name;
                    header.name_max = MAX_PATH;

                    zerr = inflateGetHeader(&stream, &header);
                    zerr = inflate(&stream, 0);
                    if (zerr == Z_OK && name[0])
                        filename = reinterpret_cast<const char*>(name);
                    inflateEnd(&stream);
                }
            }
        }
    }
#endif // HAVE_ZLIB

    // If didn't read as a compressed file, open as normal file.
    if (!uRead)
    {
        FILE* f = fopen(path_.c_str(), "rb");
        if (!f)
            throw posix_error(errno, path_.c_str());

        uRead = fread(mem, 1, mem.size, f);
        fclose(f);
        m_compress = Compress::None;
    }

    // zip compressed? (and not handled above)
    if (uncompress && mem[0U] == 'P' && mem[1U] == 'K')
        throw util::exception("zlib support is not available for zipped files");
    // gzip compressed?
    if (uncompress && mem[0U] == 0x1f && mem[1U] == 0x8b)
    {
        if (!have_zlib)
            throw util::exception("zlib support is not available for gzipped files");

        // Unknowingly gzipped image files may be zipped, so we need to handle
        // a second level of decompression here.
#ifdef HAVE_ZLIB
        MEMORY mem2(MAX_IMAGE_SIZE + 1);

        z_stream stream{};
        stream.next_in = mem.pb;
        stream.avail_in = static_cast<uInt>(uRead);
        stream.next_out = mem2.pb;
        stream.avail_out = static_cast<uInt>(mem2.size);

        auto zerr = inflateInit2(&stream, 16 + MAX_WBITS);
        if (zerr == Z_OK)
        {
            Bytef name[MAX_PATH]{};
            gz_header header{};
            header.name = name;
            header.name_max = MAX_PATH;

            zerr = inflateGetHeader(&stream, &header);
            zerr = inflate(&stream, Z_FINISH);
            if (zerr == Z_STREAM_END && name[0])
                filename = reinterpret_cast<const char*>(name);
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
#ifndef HAVE_BZIP2
        throw util::exception("bzip2 support is not available");
#else
        MEMORY mem2(MAX_IMAGE_SIZE + 1);

        auto uBzRead = static_cast<unsigned>(mem2.size);
        auto bzerr = BZ2_bzBuffToBuffDecompress(
            reinterpret_cast<char*>(mem2.pb), &uBzRead,
            reinterpret_cast<char*>(mem.pb), static_cast<int>(uRead), 0, 0);
        if (bzerr != BZ_OK)
            throw util::exception("bzip2 decompression failed (", bzerr, ")");

        memcpy(mem.pb, mem2.pb, uRead = uBzRead);
        m_compress = Compress::Bzip2;
#endif // HAVE_BZIP2
    }

    if (uncompress && mem.size > 6 && !memcmp(mem.pb, "\xfd\x37\x7a\x58\x5a\x00", 6))
    {
#ifndef HAVE_LZMA
        throw util::exception("lzma support is not available");
#else
        MEMORY mem2(MAX_IMAGE_SIZE + 1);

        lzma_stream strm{};
        const uint32_t flags = LZMA_TELL_UNSUPPORTED_CHECK;
        auto ret = lzma_stream_decoder(&strm, UINT64_MAX, flags);
        if (ret == LZMA_OK)
        {
            strm.next_in = mem.pb;
            strm.avail_in = mem.size;
            strm.next_out = mem2.pb;
            strm.avail_out = mem2.size;

            ret = lzma_code(&strm, LZMA_FINISH);
            if (ret == LZMA_STREAM_END)
            {
                uRead = mem2.size - strm.avail_out;
                memcpy(mem.pb, mem2.pb, uRead);
                m_compress = Compress::Xz;
                ret = LZMA_OK;
            }
        }

        if (ret != LZMA_OK)
            throw util::exception("xz decompression failed (", ret, ")");
#endif
    }

    if (uRead <= MAX_IMAGE_SIZE)
        return open(mem.pb, static_cast<int>(uRead), path_, filename);

    throw util::exception("file size too big");
}

bool MemFile::open(const void* buf, int len, const std::string& path_, const std::string& filename_)
{
    auto pb = reinterpret_cast<const uint8_t*>(buf);

    m_data.clear();
    m_it = m_data.insert(m_data.begin(), pb, pb + len);
    m_path = path_;
    m_filename = filename_;

    // If a filename wasn't supplied from an archive, determine it here.
    if (filename_.empty())
    {
        std::string::size_type pos = m_path.rfind(PATH_SEPARATOR_CHR);
        m_filename = (pos == m_path.npos) ? m_path : m_path.substr(pos + 1);

        // Remove the archive extension from single compressed files.
        if (IsFileExt(m_filename, "gz") || IsFileExt(m_filename, "xz"))
            m_filename = m_filename.substr(0, m_filename.size() - 3);
        else if (IsFileExt(m_filename, "bz2"))
            m_filename = m_filename.substr(0, m_filename.size() - 4);
    }

    return true;
}


const Data& MemFile::data() const
{
    return m_data;
}

int MemFile::size() const
{
    return static_cast<int>(m_data.size());
}

int MemFile::remaining() const
{
    return static_cast<int>(m_data.end() - m_it);
}

const std::string& MemFile::path() const
{
    return m_path;
}

const std::string& MemFile::name() const
{
    return m_filename;
}

Compress MemFile::compression() const
{
    return m_compress;
}

bool MemFile::read(uint8_t& b)
{
    if (remaining() < 1)
        return false;

    b = *m_it++;
    return true;
}

std::vector<uint8_t> MemFile::read(int len)
{
    auto avail_bytes = std::min(len, remaining());
    std::vector<uint8_t> data(m_it, m_it + avail_bytes);
    m_it += avail_bytes;
    return data;
}

bool MemFile::read(void* buf, int len)
{
    auto avail = std::min(len, remaining());
    if (avail != len)
        return false;

    if (avail)
    {
        memcpy(buf, &(*m_it), avail);   // make this safer when callers can cope
        m_it += avail;
    }
    return true;
}

int MemFile::read(void* buf, int len, int count)
{
    if (!len) return count; // can read as many zero-length units as requested!

    auto avail_items = std::min(count, remaining() / len);
    read(buf, avail_items * len);
    return avail_items;
}

bool MemFile::rewind()
{
    return seek(0);
}

bool MemFile::seek(int offset)
{
    m_it = m_data.begin() + std::min(offset, static_cast<int>(m_data.size()));
    return tell() == offset;
}

int MemFile::tell() const
{
    return static_cast<int>(m_it - m_data.begin());
}

bool MemFile::eof() const
{
    return m_it == m_data.end();
}
