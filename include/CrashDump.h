#pragma once

#include "SAMdisk.h"

#ifdef _WIN32

#pragma warning(push)
#pragma warning(disable:4091)   // Ignore for 8.1 SDK + VS2015 warning
#include <DbgHelp.h>
#pragma warning(pop)

LONG WINAPI CrashDumpUnhandledExceptionFilter(EXCEPTION_POINTERS* ExceptionInfo);

#endif // _WIN32
