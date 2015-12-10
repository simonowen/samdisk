// Win32 backend for SuperCard Pro device

#include "SAMdisk.h"
#include "SCP_Win32.h"

#ifdef _WIN32

#include <setupapi.h>
DEFINE_GUID(GUID_DEVCLASS_PORTS, 0x4D36E978, 0xE325, 0x11CE, 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18);

/*static*/ std::string SuperCardProWin32::GetDevicePath ()
{
	std::string path;

	HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
	if (hDevInfo != INVALID_HANDLE_VALUE)
	{
		for (int i = 0; ; ++i)
		{
			char szInstanceId[256];
			SP_DEVINFO_DATA DevInfoData = { sizeof(DevInfoData) };

			if (SetupDiEnumDeviceInfo(hDevInfo, i, &DevInfoData) &&
				SetupDiGetDeviceInstanceId(hDevInfo, &DevInfoData, szInstanceId, sizeof(szInstanceId), nullptr))
			{
				if (!strstr(szInstanceId, "SCP-JIM"))
					continue;

				HKEY hkey = SetupDiOpenDevRegKey(hDevInfo, &DevInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
				if (hkey != INVALID_HANDLE_VALUE)
				{
					char szPort[256];
					DWORD dwType = REG_SZ, dwSize = sizeof(szPort) / 2;
					if (SUCCEEDED(RegQueryValueEx(hkey, "PortName", nullptr, &dwType, (PBYTE)szPort, &dwSize)))
						path = std::string(R"(\\.\)") + szPort;
					RegCloseKey(hkey);
					break;
				}
			}
			else if (GetLastError() == ERROR_NO_MORE_ITEMS)
				break;
		}

		SetupDiDestroyDeviceInfoList(hDevInfo);
	}
	return path;
}

/*static*/ std::unique_ptr<SuperCardPro> SuperCardProWin32::Open ()
{
	std::string path = GetDevicePath();
	if (!path.empty())
	{
		HANDLE h = CreateFile(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (h != INVALID_HANDLE_VALUE)
		{
			DCB dcb = { sizeof(dcb) };
			if (BuildCommDCB("baud=9600 parity=N data=8 stop=1", &dcb) && SetCommState(h, &dcb))
				return std::unique_ptr<SuperCardPro>(new SuperCardProWin32(h));
		}
	}
	return std::unique_ptr<SuperCardPro>();
}

SuperCardProWin32::SuperCardProWin32 (HANDLE hdev)
	: m_hdev(hdev), m_dwError(ERROR_SUCCESS)
{
}

SuperCardProWin32::~SuperCardProWin32 ()
{
	CloseHandle(m_hdev);
}

bool SuperCardProWin32::Read (void *p, int len, int *bytes_read)
{
	DWORD dwBytesRead = 0;
	if (!ReadFile(m_hdev, p, static_cast<DWORD>(len), &dwBytesRead, nullptr))
	{
		m_dwError = GetLastError();
		return false;
	}

	*bytes_read = static_cast<int>(dwBytesRead);
	return true;
}

bool SuperCardProWin32::Write (const void *p, int len, int *bytes_written)
{
	DWORD dwBytesWritten = 0;
	if (!WriteFile(m_hdev, p, static_cast<DWORD>(len), &dwBytesWritten, nullptr))
	{
		m_dwError = GetLastError();
		return false;
	}

	*bytes_written = static_cast<int>(dwBytesWritten);
	return true;
}

#endif // WIN32
