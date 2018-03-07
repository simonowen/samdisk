#ifndef KF_WINUSB_H
#define KF_WINUSB_H

#ifdef HAVE_WINUSB

#include <winusb.h>
#include "KryoFlux.h"

class KF_WinUsb final : public KryoFlux
{
public:
	KF_WinUsb (HANDLE hdev, WINUSB_INTERFACE_HANDLE winusb, WINUSB_INTERFACE_HANDLE interface1);
	~KF_WinUsb ();

	static std::unique_ptr<KryoFlux> Open ();
	static std::string GetDevicePath ();

private:
	std::string KF_WinUsb::GetProductName () override;

	std::string Control (int req, int index, int value) override;
	int Read (void *buf, int len) override;
	int Write (const void *buf, int len) override;

	int ReadAsync(void *buf, int len) override;
	void StartAsyncRead();
	void StopAsyncRead() override;

	HANDLE m_hdev = NULL;
	WINUSB_INTERFACE_HANDLE m_winusb = NULL;
	WINUSB_INTERFACE_HANDLE m_interface1 = NULL;
};

#endif // HAVE_WINUSB

#endif // KF_WINUSB_H
