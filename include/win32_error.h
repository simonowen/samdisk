#ifndef WIN32_ERROR_H
#define WIN32_ERROR_H

#ifdef _WIN32

std::string GetWin32ErrorStr (DWORD error_code = 0, bool english = false);


class win32_category_impl : public std::error_category
{
public:
	const char* name () const _NOEXCEPT override { return "win32"; };
	std::string message (int error_code) const override { return GetWin32ErrorStr(error_code); }
};

inline const std::error_category & win32_category ()
{
	static win32_category_impl category;
	return category;
}

class win32_error : public std::system_error
{
public:
	win32_error (DWORD error_code, const char* message)
		: std::system_error(error_code, win32_category(), message) {}
};

#endif // _WIN32

#endif // WIN32_ERROR_H
