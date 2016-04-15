#ifndef KF_LIBUSB_H
#define KF_LIBUSB_H

#ifdef HAVE_LIBUSB1

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <libusb.h>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include "KryoFlux.h"

class KF_libusb final : public KryoFlux
{
public:
	KF_libusb (libusb_context *ctx, libusb_device_handle *hdev);
	~KF_libusb ();
	static std::unique_ptr<KryoFlux> Open ();

private:
	KF_libusb (const KF_libusb &) = delete;
	void operator= (const KF_libusb &) = delete;

	std::string GetProductName () override;

	std::string Control (int req, int index, int value) override;
	int Read (void *buf, int len) override;
	int Write (const void *buf, int len) override;

	libusb_context * m_ctx;
	libusb_device_handle * m_hdev;
};

#endif // HAVE_LIBUSB1

#endif // KF_LIBUSB_H
