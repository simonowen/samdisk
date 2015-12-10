// Basic support for SAM Coupe Pro-DOS images

#include "SAMdisk.h"
#include "types.h"

bool WriteCPM (FILE *f_, std::shared_ptr<Disk> &disk)
{
	return WriteRAW(f_, disk);
}
