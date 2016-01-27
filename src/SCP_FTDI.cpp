// FTDI backend for SuperCard Pro device

#include "SAMdisk.h"
#include "SCP_FTDI.h"

#ifdef HAVE_FTDI

/*static*/ std::unique_ptr<SuperCardPro> SuperCardProFTDI::Open ()
{
	auto hdev = ftdi_new();

	if (!CheckLibrary("ftdi", "ftdi_usb_open_desc"))
		return nullptr;

	auto status = ftdi_usb_open_desc(hdev, 0x0403, 0x6015, nullptr, "SCP-JIM");
	if (status != 0)
	{
		ftdi_free(hdev);
		return nullptr;
	}

	return std::unique_ptr<SuperCardPro>(new SuperCardProFTDI(hdev));
}

SuperCardProFTDI::SuperCardProFTDI (ftdi_context *hdev)
	: m_hdev(hdev)
{
}

SuperCardProFTDI::~SuperCardProFTDI ()
{
	ftdi_usb_close(m_hdev);
	ftdi_free(m_hdev);
}

bool SuperCardProFTDI::Read (void *buf, int len, int *bytes_read)
{
	m_status = ftdi_read_data(m_hdev, reinterpret_cast<uint8_t *>(buf), len);
	if (m_status < 0)
		return false;

	*bytes_read = m_status;
	m_status = 0;
	return true;
}

bool SuperCardProFTDI::Write (const void *buf, int len, int *bytes_written)
{
	m_status = ftdi_write_data(m_hdev, const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(buf)), len);
	if (m_status < 0)
		return false;

	*bytes_written = m_status;
	m_status = 0;
	return true;
}

#endif // HAVE_FTDI
