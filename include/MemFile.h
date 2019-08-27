#ifndef MEMFILE_H
#define MEMFILE_H

enum class Compress { None, Zip, Gzip, Bzip2, Xz };
std::string to_string (const Compress &compress);


class MemFile
{
public:
	bool open (const std::string &path, bool uncompress = true);
	bool open (const void *buf, int size, const std::string &path,
		const std::string &filename="");

	const Data &data () const;
	int size () const;
	int remaining () const;
	const std::string &path () const;
	const std::string &name () const;
	Compress compression () const;

	bool read(uint8_t &b);
	std::vector<uint8_t> read (int len);
	bool read (void *buf, int len);
	int read (void *buf, int size, int count);

	template <typename T>
	bool read (T &buf)
	{
		auto total_size = static_cast<int>(sizeof(buf[0]) * buf.size());

		// Fail if we can't fill the target
		if (remaining() < total_size)
			return false;

		std::memcpy(buf.data(), &*m_it, total_size);
//		std::copy(m_it, m_it + size, buf.data());
		m_it += total_size;
		return true;
	}

	template <typename T>
	auto ptr() const
	{
		return reinterpret_cast<const T*>(&*m_it);
	}

	bool rewind ();
	bool seek (int offset);
	int tell () const;
	bool eof () const;

private:
	std::string m_path {};
	std::string m_filename {};
	Data m_data {};
	Data::iterator m_it {};
	Compress m_compress = Compress::None;
};

#endif // MEMFILE_H
