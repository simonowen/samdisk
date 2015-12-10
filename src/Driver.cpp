// fdrawcmd.sys driver checking

#include "SAMdisk.h"

#ifdef _WIN32
#include <fdrawcmd.h>

DWORD GetDriverVersion ()
{
	DWORD version = 0, ret;

	HANDLE h = CreateFile(R"(\\.\fdrawcmd)", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h != INVALID_HANDLE_VALUE)
	{
		DeviceIoControl(h, IOCTL_FDRAWCMD_GET_VERSION, nullptr, 0, &version, sizeof(version), &ret, nullptr);
		CloseHandle(h);
	}

	return version;
}


bool CheckDriver ()
{
	auto version = GetDriverVersion();
	auto compatible = (version & 0xffff0000) == (FDRAWCMD_VERSION & 0xffff0000);

	// Compatible driver installed?
	if (version && compatible)
		return true;

	// If a version was found we're not compatible with it
	if (version)
		throw util::exception("installed fdrawcmd.sys is incompatible, please update");
	else
		util::cout << "\nFloppy features require fdrawcmd.sys from " << colour::CYAN << "http://simonowen.com/fdrawcmd/" << colour::none << '\n';

	return false;
}

bool IsFdcDriverRunning ()
{
	auto running = false;

	// Open the Service Control manager
	SC_HANDLE hSCM = OpenSCManager(nullptr, nullptr, GENERIC_READ);
	if (hSCM)
	{
		// Open the FDC service, for fdc.sys (floppy disk controller driver)
		SC_HANDLE hService = OpenService(hSCM, "fdc", GENERIC_READ);
		if (hService)
		{
			SERVICE_STATUS ss;
			if (QueryServiceStatus(hService, &ss))
			{
				// If fdc.sys is running then fdrawcmd.sys is supported
				running = ss.dwCurrentState == SERVICE_RUNNING;
			}

			CloseServiceHandle(hService);
		}

		CloseServiceHandle(hSCM);
	}

	return running;
}


bool ReportDriverVersion ()
{
	auto version = GetDriverVersion();

	if (!version)
	{
		if (IsFdcDriverRunning())
			util::cout << "\nfdrawcmd.sys is not currently installed.\n";
		else
			util::cout << "\nfdrawcmd.sys is not supported on this system.\n";

		return false;
	}

	// Report the version number of the active driver
	util::cout << util::fmt("\nfdrawcmd.sys version %u.%u.%u.%u is installed.",
							(version >> 24) & 0xff, (version >> 16) & 0xff, (version >> 8) & 0xff, version & 0xff);

	if (version < FDRAWCMD_VERSION) util::cout << ' ' << colour::YELLOW << "[update available]" << colour::none;
	util::cout << '\n';

	return true;
}

#endif // _WIN32
