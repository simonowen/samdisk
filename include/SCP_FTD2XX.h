#pragma once

#ifdef HAVE_FTD2XX

#include "SuperCardPro.h"
#include <ftd2xx.h>

class SuperCardProFTD2XX final : public SuperCardPro
{
public:
    SuperCardProFTD2XX(const SuperCardProFTD2XX&) = delete;
    void operator= (const SuperCardProFTD2XX&) = delete;
    ~SuperCardProFTD2XX();
    static std::unique_ptr<SuperCardPro> Open();

private:
    explicit SuperCardProFTD2XX(FT_HANDLE hdev);

    bool Read(void* p, int len, int* bytes_read) override;
    bool Write(const void* p, int len, int* bytes_written) override;

    FT_HANDLE m_hdev;
    FT_STATUS m_status;
};

#endif // HAVE_FTD2XX
