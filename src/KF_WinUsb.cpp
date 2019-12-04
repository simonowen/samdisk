// WinUsb backend for KryoFlux device

#include "SAMdisk.h"
#include "KF_WinUsb.h"

#ifdef HAVE_WINUSB

#include <codecvt>
#include <setupapi.h>

DEFINE_GUID(GUID_KRYOFLUX, 0x9E09C9CD, 0x5068, 0x4b31, 0x82, 0x89, 0xE3, 0x63, 0xE4, 0xE0, 0x62, 0xAC);

/*static*/ std::string KF_WinUsb::GetDevicePath()
{
    std::string path;

    HDEVINFO hDevInfo = SetupDiGetClassDevs(&GUID_KRYOFLUX, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (hDevInfo != INVALID_HANDLE_VALUE)
    {
        SP_DEVINFO_DATA DevInfoData = { sizeof(DevInfoData) };

        SP_DEVICE_INTERFACE_DATA DevIntfData = { sizeof(SP_DEVICE_INTERFACE_DATA) };
        if (SetupDiEnumDeviceInterfaces(hDevInfo, nullptr, &GUID_KRYOFLUX, 0, &DevIntfData))
        {
            DWORD dwSize = 0;
            SetupDiGetDeviceInterfaceDetail(hDevInfo, &DevIntfData, nullptr, 0, &dwSize, nullptr);

            std::vector<uint8_t> data(dwSize);
            auto pDevIntfData = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA>(data.data());
            pDevIntfData->cbSize = sizeof(*pDevIntfData);

            SetupDiGetDeviceInterfaceDetail(hDevInfo, &DevIntfData, pDevIntfData, dwSize, &dwSize, nullptr);
            path = pDevIntfData->DevicePath;
        }

        SetupDiDestroyDeviceInfoList(hDevInfo);
    }
    return path;
}

/*static*/ std::unique_ptr<KryoFlux> KF_WinUsb::Open()
{
    std::string path = GetDevicePath();
    if (!path.empty())
    {
        HANDLE hdev = CreateFile(
            path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_WRITE | FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr);

        if (hdev != INVALID_HANDLE_VALUE)
        {
            WINUSB_INTERFACE_HANDLE winusb;

            if (CheckLibrary("winusb", "WinUsb_Initialize") && WinUsb_Initialize(hdev, &winusb))
            {
                WINUSB_INTERFACE_HANDLE interface1;
                auto ret = WinUsb_GetAssociatedInterface(winusb, KF_INTERFACE - 1, &interface1);
                if (ret)
                {
                    ULONG timeout = KF_TIMEOUT_MS;
                    uint8_t enable = 1;

                    WinUsb_SetPipePolicy(interface1, KF_EP_BULK_IN, SHORT_PACKET_TERMINATE, sizeof(enable), &enable);
                    WinUsb_SetPipePolicy(interface1, KF_EP_BULK_IN, AUTO_CLEAR_STALL, sizeof(enable), &enable);
                    WinUsb_SetPipePolicy(interface1, KF_EP_BULK_IN, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout);

                    WinUsb_SetPipePolicy(interface1, KF_EP_BULK_OUT, SHORT_PACKET_TERMINATE, sizeof(enable), &enable);
                    WinUsb_SetPipePolicy(interface1, KF_EP_BULK_OUT, AUTO_CLEAR_STALL, sizeof(enable), &enable);
                    WinUsb_SetPipePolicy(interface1, KF_EP_BULK_OUT, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout);

                    return std::unique_ptr<KryoFlux>(new KF_WinUsb(hdev, winusb, interface1));
                }
            }

            CloseHandle(hdev);
        }
    }
    return std::unique_ptr<KryoFlux>();
}

KF_WinUsb::KF_WinUsb(
    HANDLE hdev,
    WINUSB_INTERFACE_HANDLE winusb,
    WINUSB_INTERFACE_HANDLE interface1)
    : m_hdev(hdev), m_winusb(winusb), m_interface1(interface1)
{
}

KF_WinUsb::~KF_WinUsb()
{
    WinUsb_Free(m_interface1);
    WinUsb_Free(m_winusb);
    CloseHandle(m_hdev);
}

std::string KF_WinUsb::GetProductName()
{
    ULONG len;
    USB_DEVICE_DESCRIPTOR devdesc{};

    auto ret = WinUsb_GetDescriptor(
        m_winusb,
        USB_DEVICE_DESCRIPTOR_TYPE,
        0,
        0,
        reinterpret_cast<PUCHAR>(&devdesc),
        sizeof(devdesc),
        &len);

    if (ret && devdesc.iProduct > 0)
    {
        uint8_t buf[256];
        auto strdesc = reinterpret_cast<USB_STRING_DESCRIPTOR*>(buf);

        ret = WinUsb_GetDescriptor(
            m_winusb,
            USB_STRING_DESCRIPTOR_TYPE,
            devdesc.iProduct,
            0,
            reinterpret_cast<PUCHAR>(strdesc),
            sizeof(buf),
            &len);

        if (ret && strdesc->bLength > 2)
        {
            auto wchars = (strdesc->bLength - sizeof(USB_STRING_DESCRIPTOR)) / sizeof(WCHAR) + 1;
            auto product = std::wstring(strdesc->bString, wchars);
            std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
            return conv.to_bytes(product);
        }
    }

    return "";
}

std::string KF_WinUsb::Control(int req, int index, int value)
{
    uint8_t buf[512];
    auto len = 0UL;

    WINUSB_SETUP_PACKET setup{};
    setup.RequestType = (1 << 7) | (2 << 5) | (3 << 0); // IN | VENDOR | OTHER
    setup.Request = static_cast<uint8_t>(req);
    setup.Value = static_cast<uint16_t>(value);
    setup.Index = static_cast<uint16_t>(index);
    setup.Length = sizeof(buf);

    auto ret = WinUsb_ControlTransfer(
        m_winusb,
        setup,
        buf,
        sizeof(buf),
        &len,
        nullptr);

    if (!ret)
        throw win32_error(GetLastError(), "WinUsb_ControlTransfer");

    return std::string(reinterpret_cast<char*>(buf), len);
}

int KF_WinUsb::Read(void* buf, int len)
{
    auto read = 0UL;

    auto ret = WinUsb_ReadPipe(
        m_interface1,
        KF_EP_BULK_IN,
        reinterpret_cast<uint8_t*>(buf),
        len,
        &read,
        nullptr);

    if (!ret)
        throw win32_error(GetLastError(), "WinUsb_ReadPipe");

    return read;
}

int KF_WinUsb::Write(const void* buf, int len)
{
    auto written = 0UL;

    auto ret = WinUsb_WritePipe(
        m_interface1,
        KF_EP_BULK_OUT,
        reinterpret_cast<uint8_t*>(const_cast<void*>(buf)),
        len,
        &written,
        nullptr);

    if (!ret)
        throw win32_error(GetLastError(), "WinUsb_WritePipe");

    return written;
}

int KF_WinUsb::ReadAsync(void* buf, int len)
{
    // Not implemented yet, so use synchronous version.
    return Read(buf, len);
}

void KF_WinUsb::StartAsyncRead()
{
}

void KF_WinUsb::StopAsyncRead()
{
}

#endif // HAVE_WINUSB
