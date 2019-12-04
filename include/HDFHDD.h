#pragma once

class HDFHDD final : public HDD
{
public:
    bool Open(const std::string& path, bool uncached) override;
    bool Create(const std::string& path, uint64_t ullTotalBytes_, const IDENTIFYDEVICE* pIdentify_, bool fOverwrite_/*=false*/) override;

public:
    static bool IsRecognised(const std::string& path);
};
