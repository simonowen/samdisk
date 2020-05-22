#pragma once

using IMAGE_READFUNC = bool (*)(MemFile&, std::shared_ptr<Disk>&);
using IMAGE_WRITEFUNC = bool (*)(FILE*, std::shared_ptr<Disk>&);
using DEVICE_READFUNC = bool(*)(const std::string&, std::shared_ptr<Disk>&);
using DEVICE_WRITEFUNC = bool(*)(const std::string&, std::shared_ptr<Disk>&);

struct IMAGE_ENTRY
{
    const char* pszType;
    IMAGE_READFUNC pfnRead;
    IMAGE_WRITEFUNC pfnWrite;
};

struct DEVICE_ENTRY
{
    const char* pszType;
    DEVICE_READFUNC pfnRead;
    DEVICE_WRITEFUNC pfnWrite;
};

#define DECLARATIONS_ONLY
#include "types.cpp"
#undef DECLARATIONS_ONLY
