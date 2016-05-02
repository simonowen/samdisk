#ifndef DEMANDDISK_H
#define DEMANDDISK_H

#include "Disk.h"

class DemandDisk : public Disk
{
public:
	constexpr static int FIRST_READ_REVS = 2;
	constexpr static int REMAIN_READ_REVS = 5;

	const Track &read_track (const CylHead &cylhead) override;
	void unload () override;

	void extend (const CylHead &cylhead);

protected:
	virtual TrackData load (const CylHead &cylhead, bool first_read=false) = 0;

	std::bitset<MAX_DISK_CYLS * MAX_DISK_HEADS> m_loaded {};
};

#endif // DEMANDDISK_H
