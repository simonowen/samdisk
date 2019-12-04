// 1DD is a special type of D88 image with only 1 head in the track index

#include "SAMdisk.h"
#include "types.h"

bool Read1DD(MemFile& file, std::shared_ptr<Disk>& disk)
{
    return ReadD88(file, disk);
}

bool Write1DD(FILE* f_, std::shared_ptr<Disk>& disk)
{
    return WriteD88(f_, disk);
}
