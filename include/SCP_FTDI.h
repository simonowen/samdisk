#ifndef SCP_FTDI_H
#define SCP_FTDI_H

#ifdef HAVE_FTD2XX

#include "SuperCardPro.h"

class SuperCardProFTDI final : public SuperCardPro
{
public:
	~SuperCardProFTDI ();
	static std::unique_ptr<SuperCardPro> Open ();

private:
	explicit SuperCardProFTDI (FT_HANDLE hdev);

	bool Read (void *p, int len, int *bytes_read) override;
	bool Write (const void *p, int len, int *bytes_written) override;

	FT_HANDLE m_hdev;
	FT_STATUS m_status;
};

#endif // HAVE_FTD2XX

#endif // SCP_FTDI_H
