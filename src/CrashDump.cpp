// Windows crash dump support, to save a minidump image for debugging

#include "SAMdisk.h"

#ifdef _WIN32

#pragma comment(lib, "DbgHelp.lib")


BOOL CreateMiniDump(const std::string& name, EXCEPTION_POINTERS* pep)
{
    BOOL ret = false;

    TCHAR szModule[MAX_PATH];
    GetModuleFileName(nullptr, szModule, ARRAYSIZE(szModule));

    std::string path = szModule;
    path = path.substr(0, path.find_last_of('\\') + 1) + name;

    HANDLE hfile = CreateFile(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    // If the location is read-only, create in the temporary directory instead
    if (hfile == INVALID_HANDLE_VALUE)
    {
        GetTempPath(MAX_PATH, szModule);
        path = std::string(szModule) + name;
        hfile = CreateFile(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    if (hfile != INVALID_HANDLE_VALUE)
    {
        // Create the minidump
        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = pep;
        mdei.ClientPointers = TRUE;

        ret = MiniDumpWriteDump(GetCurrentProcess(),
            GetCurrentProcessId(),
            hfile,
            MiniDumpNormal,  //MiniDumpWithFullMemory,
            pep ? &mdei : nullptr,
            nullptr,
            nullptr);
        // Close the file
        CloseHandle(hfile);

        if (!ret)
            DeleteFile(path.c_str());

        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_INTENSITY);
        fprintf(stderr, "Crashed! Dump file saved to: %s", path.c_str());
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }

    return ret;
}

LONG WINAPI CrashDumpUnhandledExceptionFilter(EXCEPTION_POINTERS* ExceptionInfo)
{
    SetUnhandledExceptionFilter(nullptr);
    CreateMiniDump("SAMdisk-crash.dmp", ExceptionInfo);
    return EXCEPTION_EXECUTE_HANDLER;
}

#endif // _WIN32
