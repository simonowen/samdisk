// Jupiter Ace helper functions

int GetDeepThoughtDataOffset (const Data &data);
std::string GetDeepThoughtData (const Data &data);
bool IsDeepThoughtSector (const Sector &sector, int &data_offset);
bool IsDeepThoughtDisk (Disk &disk, const Sector *&sector);
bool IsValidDeepThoughtData (const Data &data);
