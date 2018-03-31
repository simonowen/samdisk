#ifndef UTILS_H
#define UTILS_H

inline std::ostream& operator<<(std::ostream& os, uint8_t val)
{
	return os << static_cast<int>(val);
}

class posix_error : public std::system_error
{
public:
	posix_error (int error_code = 0, const char* message = nullptr)
		: std::system_error(error_code ? error_code : errno, std::generic_category(), message) {}
};


enum class ttycmd : uint8_t
{
	cleartoeol
};

#ifdef _WIN32

enum class colour : uint8_t
{
	black = 0,
	blue = FOREGROUND_BLUE,
	red = FOREGROUND_RED,
	magenta = red | blue,
	green = FOREGROUND_GREEN,
	cyan = green | blue,
	yellow = green | red,
	white = red | green | blue,

	bright = FOREGROUND_INTENSITY,

	BLUE = blue | bright,
	RED = red | bright,
	MAGENTA = magenta | bright,
	GREEN = green | bright,
	CYAN = cyan | bright,
	YELLOW = yellow | bright,
	WHITE = white | bright,

	grey = black | bright,
	none = white
};

#else

enum class colour : uint8_t
{
	blue = 34,
	red = 31,
	magenta = 35,
	green = 32,
	cyan = 36,
	yellow = 33,
	white = 0,

	bright = 0x80,

	BLUE = blue | bright,
	RED = red | bright,
	MAGENTA = magenta | bright,
	GREEN = green | bright,
	CYAN = cyan | bright,
	YELLOW = yellow | bright,
	WHITE = white | bright,

	grey = cyan,	// use cyan as bright black is not well supported for grey
	none = white
};

#endif


namespace util
{
inline std::ostream& operator<<(std::ostream& os, uint8_t val)
{
	return os << static_cast<int>(val);
}

template <typename ... Args>
std::string make_string (Args&& ... args)
{
	std::ostringstream ss;
	(void)std::initializer_list<bool> {(ss << args, false)...};
	return ss.str();
}

template <typename T> T byteswap (T x);

template<>
inline uint16_t byteswap<uint16_t> (uint16_t x)
{
	return (static_cast<uint16_t>(x & 0xff) << 8) | (x >> 8);
}

template<>
inline uint32_t byteswap<uint32_t> (uint32_t x)
{
	return (static_cast<uint32_t>(byteswap<uint16_t>(x & 0xffff)) << 16) | byteswap<uint16_t>(x >> 16);
}

template<>
inline uint64_t byteswap<uint64_t> (uint64_t x)
{
	return (static_cast<uint64_t>(byteswap<uint32_t>(x & 0xffffffff)) << 32) | byteswap<uint32_t>(x >> 32);
}

// ToDo: detect compile endian
template <typename T>
T betoh (T x)
{
	return byteswap(x);
}

// ToDo: detect compile endian
template <typename T>
T htobe (T x)
{
	return byteswap(x);
}

template <typename T>
T htole (T x)
{
	return x;
}

template <typename T>
T letoh (T x)
{
	return x;
}

template <int N, std::enable_if_t<N == 2>* = nullptr>
uint16_t le_value(uint8_t (&arr)[N])
{
	return (arr[1] << 8) | arr[0];
}

template <int N, std::enable_if_t<N == 3 || N == 4>* = nullptr>
auto le_value(uint8_t (&arr)[N])
{
	uint32_t value = 0, i = 0;
	for (auto x : arr)
		value |= (x << (8 * i++));
	return value;
}


class exception : public std::runtime_error
{
public:
	template <typename ... Args>
	explicit exception (Args&& ... args)
		: std::runtime_error(make_string(std::forward<Args>(args)...)) {}
};

std::string fmt (const char *fmt, ...);
std::vector<std::string> split (const std::string &str, char delim = ' ', bool skip_empty = false);
std::string trim (const std::string &str);
std::string resource_dir ();
bool is_stdout_a_tty ();
std::string lowercase (const std::string &str);

inline std::string to_string (int64_t v) { std::stringstream ss; ss << v; return ss.str(); }
inline std::string to_string (const std::string &s) { return s; }	// std:: lacks to_string to strings(!)
inline std::string format () { return ""; }							// Needed for final empty entry

template <typename T, typename ...Args>
std::string format (T arg, Args&&... args)
{
	using namespace std; // pull in to_string for other types
	std::string s = to_string(arg) + format(std::forward<Args>(args)...);
	return s;
}


struct LogHelper
{
	LogHelper(std::ostream *screen_, std::ostream* file_ = nullptr)
		: screen(screen_), file(file_)
	{
	}

	std::ostream *screen;
	std::ostream *file;
};

extern LogHelper cout;
extern std::ofstream log;

template <typename T>
LogHelper& operator<<(LogHelper& h, const T& t)
{
	*h.screen << t;
	if (h.file) *h.file << t;
	return h;
}

LogHelper& operator<<(LogHelper& h, colour c);
LogHelper& operator<<(LogHelper& h, ttycmd cmd);


template <typename ForwardIter>
void hex_dump (ForwardIter it, ForwardIter itEnd, colour *pColours = nullptr, int start_offset = 0, size_t per_line = 16)
{
	assert(per_line != 0);
	static const char hex[] = "0123456789ABCDEF";

	colour c = colour::none;
	auto base_offset = start_offset - (start_offset % per_line);
	start_offset %= per_line;

	while (it < itEnd)
	{
		std::string text(per_line, ' ');

		if (c != colour::none)
		{
			util::cout << colour::none;
			c = colour::none;
		}

		util::cout << hex[(base_offset >> 12) & 0xf] <<
			hex[(base_offset >> 8) & 0xf] <<
			hex[(base_offset >> 4) & 0xf] <<
			hex[base_offset & 0xf] << "  ";

		base_offset += per_line;

		for (size_t i = 0; i < per_line; i++)
		{
			if (start_offset-- <= 0 && it < itEnd)
			{
				if (pColours)
				{
					auto new_colour = *pColours++;
					if (new_colour != c)
					{
						util::cout << new_colour;
						c = new_colour;
					}
				}

				auto b = *it++;
				text[i] = std::isprint(b) ? b : '.';
				util::cout << hex[b >> 4] << hex[b & 0x0f] << ' ';
			}
			else
			{
				util::cout << "   ";
			}
		}

		util::cout << colour::none << " " << text << "\n";
	}
}

} // namespace util

#endif // UTILS_H
