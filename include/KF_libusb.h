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

	void ReadCallback (libusb_transfer *transfer);

private:
	static constexpr int BUFFER_SIZE = 4096;
	static constexpr int BUFFER_COUNT = 128;

	KF_libusb (const KF_libusb &) = delete;
	void operator= (const KF_libusb &) = delete;

	std::string GetProductName () override;

	std::string Control (int req, int index, int value) override;
	int Read (void *buf, int len) override;
	int Write (const void *buf, int len) override;

	int ReadAsync(void *buf, int len) override;
	void StartAsyncRead() override;
	void StopAsyncRead() override;

	libusb_context *m_ctx{nullptr};
	libusb_device_handle *m_hdev{nullptr};

	int m_readret{LIBUSB_SUCCESS};
	bool m_reading{false};
	std::mutex m_readmutex{};
	std::vector<uint8_t> m_readbuf{};
	std::vector<std::array<uint8_t, BUFFER_SIZE>> m_bufpool{BUFFER_COUNT};
	std::vector<libusb_transfer*> m_xferpool{};
};

#endif // HAVE_LIBUSB1

#endif // KF_LIBUSB_H
