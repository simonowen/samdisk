// Win32 error exception

#include "SAMdisk.h"

#ifdef _WIN32

std::string GetWin32ErrorStr (DWORD error_code, bool english)
{
	if (!error_code)
		error_code = GetLastError();

	if (!error_code)
		return "";

	LPWSTR pMessage = nullptr;
	DWORD length = 0;

	// Try for English first?
	if (english)
	{
		length = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, error_code, MAKELANGID(LANG_ENGLISH, SUBLANG_NEUTRAL), reinterpret_cast<LPWSTR>(&pMessage), 0, nullptr);
	}

	if (!length)
	{
		length = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), reinterpret_cast<LPWSTR>(&pMessage), 0, nullptr);
	}

	std::ostringstream ss;

	if (length)
	{
		std::wstring wstr { pMessage, length };
		wstr.erase(wstr.find_last_not_of(L"\r\n. ") + 1);
		LocalFree(pMessage);

		int wstr_length = static_cast<int>(wstr.length());
		int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wstr_length, nullptr, 0, nullptr, nullptr);

		std::vector<char> utf8_str(utf8_size);
		WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wstr_length, utf8_str.data(), utf8_size, nullptr, nullptr);

		ss << std::string(utf8_str.data(), utf8_str.size());
	}
	else
	{
		ss << "Unknown Win32 error";
	}

	ss << " (" << error_code << ')';
	return ss.str();
}

#endif // _WIN32
