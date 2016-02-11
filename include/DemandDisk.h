#ifndef DEMANDDISK_H
#define DEMANDDISK_H

#include "Disk.h"

class DemandDisk : public Disk
{
public:
	const Track &read_track (const CylHead &cylhead) override;
	void preload (const Range &range_) override;
	void unload (bool source_only) override;

	void extend (const CylHead &cylhead);

protected:
	virtual Track load (const CylHead &cylhead);

	std::bitset<MAX_DISK_CYLS * MAX_DISK_HEADS> m_loaded {};
	std::mutex m_mutex {};
};

#endif // DEMANDDISK_H
