#pragma once

#define SHORT_PACKET_TERMINATE  0x01
#define AUTO_CLEAR_STALL        0x02
#define PIPE_TRANSFER_TIMEOUT   0x03

#define USB_DEVICE_DESCRIPTOR_TYPE                          0x01
#define USB_STRING_DESCRIPTOR_TYPE                          0x03

extern "C"
{

typedef PVOID WINUSB_INTERFACE_HANDLE, * PWINUSB_INTERFACE_HANDLE;

typedef struct _USB_DEVICE_DESCRIPTOR {
    UCHAR   bLength;
    UCHAR   bDescriptorType;
    USHORT  bcdUSB;
    UCHAR   bDeviceClass;
    UCHAR   bDeviceSubClass;
    UCHAR   bDeviceProtocol;
    UCHAR   bMaxPacketSize0;
    USHORT  idVendor;
    USHORT  idProduct;
    USHORT  bcdDevice;
    UCHAR   iManufacturer;
    UCHAR   iProduct;
    UCHAR   iSerialNumber;
    UCHAR   bNumConfigurations;
} USB_DEVICE_DESCRIPTOR, * PUSB_DEVICE_DESCRIPTOR;

#pragma pack(1)

typedef struct _WINUSB_SETUP_PACKET {
    UCHAR   RequestType;
    UCHAR   Request;
    USHORT  Value;
    USHORT  Index;
    USHORT  Length;
} WINUSB_SETUP_PACKET, * PWINUSB_SETUP_PACKET;

typedef struct _USB_STRING_DESCRIPTOR {
    UCHAR   bLength;
    UCHAR   bDescriptorType;
    WCHAR   bString[1];
} USB_STRING_DESCRIPTOR, * PUSB_STRING_DESCRIPTOR;

#pragma pack()

BOOL __stdcall
WinUsb_Initialize(
    _In_  HANDLE DeviceHandle,
    _Out_ PWINUSB_INTERFACE_HANDLE InterfaceHandle
);


BOOL __stdcall
WinUsb_Free(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle
);

BOOL __stdcall
WinUsb_GetDescriptor(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR DescriptorType,
    _In_  UCHAR Index,
    _In_  USHORT LanguageID,
    _Out_writes_bytes_to_opt_(BufferLength, *LengthTransferred) PUCHAR Buffer,
    _In_  ULONG BufferLength,
    _Out_ PULONG LengthTransferred
);

BOOL __stdcall
WinUsb_GetAssociatedInterface(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR AssociatedInterfaceIndex,
    _Out_ PWINUSB_INTERFACE_HANDLE AssociatedInterfaceHandle
);

BOOL __stdcall
WinUsb_SetPipePolicy(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR PipeID,
    _In_  ULONG PolicyType,
    _In_  ULONG ValueLength,
    _In_reads_bytes_(ValueLength) PVOID Value
);

BOOL __stdcall
WinUsb_ControlTransfer(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  WINUSB_SETUP_PACKET SetupPacket,
    _Out_writes_bytes_to_opt_(BufferLength, *LengthTransferred) PUCHAR Buffer,
    _In_  ULONG BufferLength,
    _Out_opt_ PULONG LengthTransferred,
    _In_opt_  LPOVERLAPPED Overlapped
);

BOOL __stdcall
WinUsb_ReadPipe(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR PipeID,
    _Out_writes_bytes_to_opt_(BufferLength, *LengthTransferred) PUCHAR Buffer,
    _In_  ULONG BufferLength,
    _Out_opt_ PULONG LengthTransferred,
    _In_opt_ LPOVERLAPPED Overlapped
);

BOOL __stdcall
WinUsb_WritePipe(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR PipeID,
    _In_reads_bytes_(BufferLength) PUCHAR Buffer,
    _In_  ULONG BufferLength,
    _Out_opt_ PULONG LengthTransferred,
    _In_opt_ LPOVERLAPPED Overlapped
);

};  // extern "C"
