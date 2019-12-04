#pragma once

class BlockDevice final : public HDD
{
public:
    BlockDevice();
    BlockDevice(const BlockDevice&) = delete;
    BlockDevice& operator= (const BlockDevice&) = delete;
    ~BlockDevice();

public:
    bool Open(const std::string& path, bool uncached) override;

    // Overrides
public:
    bool SafetyCheck() override;
    bool Lock() override;
    void Unlock() override;

    std::vector<std::string> GetVolumeList() const override;

public:
    static bool IsRecognised(const std::string& path);
    static bool IsBlockDevice(const std::string& path);
    static bool IsFileHDD(const std::string& path);

    static std::vector<std::string> GetDeviceList();

    // Helpers
protected:
    int ScsiCmd(int fd, const uint8_t* cmd, int cmd_len, void* data, int data_len, bool read);
    bool ReadIdentifyData(HANDLE h_, IDENTIFYDEVICE& pIdentify_);
    bool ReadMakeModelRevisionSerial(const std::string& path);

protected:
    HANDLE hdev;
    std::vector<std::pair<HANDLE, std::string>> lLockHandles{};
};
