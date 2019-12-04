#pragma once

using IMAGE_READFUNC = bool (*)(MemFile&, std::shared_ptr<Disk>&);
using IMAGE_WRITEFUNC = bool (*)(FILE*, std::shared_ptr<Disk>&);
using DEVICE_READFUNC = bool(*)(const std::string&, std::shared_ptr<Disk>&);
using DEVICE_WRITEFUNC = bool(*)(const std::string&, std::shared_ptr<Disk>&);

typedef struct
{
    const char* pszType;
    IMAGE_READFUNC pfnRead;
    IMAGE_WRITEFUNC pfnWrite;
} IMAGE_ENTRY;

typedef struct
{
    const char* pszType;
    DEVICE_READFUNC pfnRead;
    DEVICE_WRITEFUNC pfnWrite;
} DEVICE_ENTRY;

#define DECLARATIONS_ONLY
#include "types.cpp"
#undef DECLARATIONS_ONLY
