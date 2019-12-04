#pragma once

bool ReadRecord(const std::string& path, std::shared_ptr<Disk>& disk);
bool WriteRecord(const std::string& path, std::shared_ptr<Disk>& disk);
bool ReadRecord(HDD& hdd, int record, std::shared_ptr<Disk>& disk);
bool WriteRecord(HDD& hdd, int record, std::shared_ptr<Disk>& disk, bool format = false);

bool UnwrapCPM(std::shared_ptr<Disk>& cpm_disk, std::shared_ptr<Disk>& disk);
bool WrapCPM(std::shared_ptr<Disk>& disk, std::shared_ptr<Disk>& cpm_disk);
