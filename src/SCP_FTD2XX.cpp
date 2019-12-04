// Closed-source FTDI backend for SuperCard Pro device

#include "SAMdisk.h"
#include "SCP_FTD2XX.h"

#ifdef HAVE_FTD2XX

/*static*/ std::unique_ptr<SuperCardPro> SuperCardProFTD2XX::Open()
{
    FT_HANDLE hdev;
    auto serial = static_cast<const void*>("SuperCard Pro");

    if (!CheckLibrary("ftdi2", "FT_OpenEx"))
        return nullptr;

    FT_STATUS status = FT_OpenEx(const_cast<PVOID>(serial), FT_OPEN_BY_DESCRIPTION, &hdev);
    if (status != FT_OK)
        return nullptr;

    // Set some comms defaults for good measure
    FT_SetLatencyTimer(hdev, 2);
    FT_SetUSBParameters(hdev, 0x10000, 0x10000);
    FT_Purge(hdev, FT_PURGE_RX | FT_PURGE_TX);
    FT_SetTimeouts(hdev, 2000, 2000);

    return std::unique_ptr<SuperCardPro>(new SuperCardProFTD2XX(hdev));
}

SuperCardProFTD2XX::SuperCardProFTD2XX(FT_HANDLE hdev)
    : m_hdev(hdev), m_status(FT_OK)
{
}

SuperCardProFTD2XX::~SuperCardProFTD2XX()
{
    FT_Close(m_hdev);
}

bool SuperCardProFTD2XX::Read(void* buf, int len, int* bytes_read)
{
    DWORD dwBytesToRead = static_cast<DWORD>(len), dwBytesReturned = 0;
    m_status = FT_Read(m_hdev, buf, dwBytesToRead, &dwBytesReturned);
    if (m_status != FT_OK)
        return false;

    *bytes_read = static_cast<int>(dwBytesReturned);
    return true;
}

bool SuperCardProFTD2XX::Write(const void* buf, int len, int* bytes_written)
{
    DWORD dwBytesToWrite = static_cast<DWORD>(len), dwBytesWritten = 0;
    m_status = FT_Write(m_hdev, const_cast<LPVOID>(buf), dwBytesToWrite, &dwBytesWritten);
    if (m_status != FT_OK)
        return false;

    *bytes_written = static_cast<int>(dwBytesWritten);
    return true;
}

#endif // HAVE_FTD2XX
