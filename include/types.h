#ifndef TYPES_H
#define TYPES_H

using READFUNC = bool (*)(MemFile&, std::shared_ptr<Disk>&);
using WRITEFUNC = bool (*)(FILE*, std::shared_ptr<Disk>&);

typedef struct
{
	const char *pszType;
	READFUNC pfnRead;
	WRITEFUNC pfnWrite;
} FILE_TYPE;

#define DECLARATIONS_ONLY
#include "types.cpp"
#undef DECLARATIONS_ONLY

#endif
