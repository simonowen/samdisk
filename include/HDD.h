#pragma once

class MEMORY;

const int SECTOR_SIZE = 512;

typedef struct
{
    int len = 0;    // used size in bytes

    union
    {
        uint8_t byte[SECTOR_SIZE]{};
        uint16_t word[SECTOR_SIZE / 2];
    };
}
IDENTIFYDEVICE;


class HDD
{
public:
    HDD() { Reset(); }
    virtual ~HDD() { Unlock(); if (h != -1) close(h); Reset(); }

    int64_t Tell() const;
    bool Seek(int64_t sector) const;
    int Read(void* pv, int sectors, bool byte_swap = false) const;
    int Write(void* pv, int sectors, bool byte_swap = false);
    bool Copy(HDD* phSrc_, int64_t uSectors_, int64_t uSrcOffset_ = 0, int64_t uDstOffset_ = 0, int64_t uTotal_ = 0, const char* pcszAction_ = nullptr);

    void SetIdentifyData(const IDENTIFYDEVICE* pIdentify_ = nullptr);

public:
    static bool IsRecognised(const std::string& path);
    static std::shared_ptr<HDD> OpenDisk(const std::string& path);
    static std::shared_ptr<HDD> CreateDisk(const std::string& path, int64_t llTotalBytes_, const IDENTIFYDEVICE* pIdentify_ = nullptr, bool fOverwrite_ = false);

public:
    virtual bool SafetyCheck();
    virtual bool Lock() { return true; }
    virtual void Unlock() {}
    virtual std::vector<std::string> GetVolumeList() const { return std::vector<std::string>(); }
    virtual std::string GetMakeModel() { return std::string(); }

protected:
    virtual bool Open(const std::string& path, bool uncached = false) = 0;
    virtual bool Create(const std::string&/*path*/, uint64_t /*ullTotalBytes_*/, const IDENTIFYDEVICE* /*pIdentify_*/ = nullptr, bool /*fOverwrite_*/ = false) { return false; }

    void Reset();
    std::string GetIdentifyString(void* p, size_t n);
    void SetIdentifyString(const std::string& str, void* p, size_t n);

public:
    int h = -1;
    int cyls = 0, heads = 0, sectors = 0;
    int sector_size = 0, data_offset = 0;
    int64_t total_sectors = 0;
    int64_t total_bytes = 0;
    std::string strMakeModel{}, strSerialNumber{}, strFirmwareRevision{};
    IDENTIFYDEVICE sIdentify = {};
};


// For v1.0 see: http://web.archive.org/web/20081016212030/http://www.ramsoft.bbk.org/tech/rs-hdf.txt
// For v1.1 see: http://ramsoft.bbk.org.omegahg.com/hdfform.html
typedef struct
{
    char szSignature[6];                // RS-IDE
    uint8_t bEOF;                       // 0x1a
    uint8_t bRevision;                  // 0x10 for v1.0, 0x11 for v1.1
    uint8_t bFlags;                     // b0 = halved sector data
    uint8_t bOffsetLow, bOffsetHigh;    // Offset from start of file to HDD data
    uint8_t abReserved[11];             // Must be zero
} RS_IDE;
