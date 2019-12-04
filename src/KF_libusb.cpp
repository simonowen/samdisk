// libusb backend for KryoFlux device

#include "SAMdisk.h"
#include "KF_libusb.h"

#ifdef HAVE_LIBUSB1

/*static*/ std::unique_ptr<KryoFlux> KF_libusb::Open()
{
    libusb_context* ctx = nullptr;
    bool claimed = false;

    auto ret = libusb_init(&ctx);
    if (ret == LIBUSB_SUCCESS)
    {
        libusb_device* dev{ nullptr };
        libusb_device** dev_list{ nullptr };
        auto num_devices = libusb_get_device_list(ctx, &dev_list);
        ssize_t i;

        for (i = 0; i < num_devices; ++i)
        {
            libusb_device_descriptor desc{};
            ret = libusb_get_device_descriptor(dev_list[i], &desc);
            if (ret == LIBUSB_SUCCESS)
            {
                if (desc.idVendor == KF_VID && desc.idProduct == KF_PID)
                {
                    dev = dev_list[i];
                    break;
                }
            }
        }

        libusb_device_handle* hdev{ nullptr };
        if (dev)
            ret = libusb_open(dev, &hdev);

        if (dev_list)
            libusb_free_device_list(dev_list, 1);

        if (hdev)
        {
            libusb_set_auto_detach_kernel_driver(hdev, 1);

            if (ret == LIBUSB_SUCCESS)
            {
                ret = libusb_claim_interface(hdev, KF_INTERFACE);
                claimed = (ret == LIBUSB_SUCCESS);
            }

            if (ret == LIBUSB_SUCCESS)
                return std::make_unique<KF_libusb>(ctx, hdev);

            if (claimed)
                libusb_release_interface(hdev, KF_INTERFACE);

            libusb_close(hdev);
        }

        libusb_exit(ctx);
    }

    if (ret == LIBUSB_ERROR_ACCESS)
        throw util::exception(util::format("(open) ", libusb_error_name(ret), " (need root?)"));
    else if (ret != LIBUSB_SUCCESS)
        throw util::exception(util::format("(open) ", libusb_error_name(ret)));

    return std::unique_ptr<KryoFlux>();
}

KF_libusb::KF_libusb(
    libusb_context* ctx,
    libusb_device_handle* hdev)
    : m_ctx(ctx), m_hdev(hdev)
{
}

KF_libusb::~KF_libusb()
{
    libusb_release_interface(m_hdev, KF_INTERFACE);
    libusb_close(m_hdev);
    libusb_exit(m_ctx);
}

std::string KF_libusb::GetProductName()
{
    auto dev = libusb_get_device(m_hdev);

    libusb_device_descriptor devdesc{};
    libusb_get_device_descriptor(dev, &devdesc);

    uint8_t product[256]{};
    libusb_get_string_descriptor_ascii(
        m_hdev,
        devdesc.iProduct,
        product,
        sizeof(product) - 1);

    return std::string(reinterpret_cast<char*>(product));
}

std::string KF_libusb::Control(int req, int index, int value)
{
    uint8_t buf[256];

    auto ret = libusb_control_transfer(
        m_hdev,
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_OTHER,
        static_cast<uint8_t>(req),
        0,
        static_cast<uint16_t>(index),
        buf,
        sizeof(buf),
        KF_TIMEOUT_MS);

    if (ret < 0)
        throw util::exception(util::format("(control) ", libusb_error_name(ret)));

    return std::string(reinterpret_cast<char*>(buf), ret);
}

int KF_libusb::Read(void* buf, int len)
{
    auto read = 0;
    auto ret = libusb_bulk_transfer(
        m_hdev,
        KF_EP_BULK_IN,
        reinterpret_cast<uint8_t*>(buf),
        len,
        &read,
        KF_TIMEOUT_MS);

    if (ret != LIBUSB_SUCCESS)
        throw util::exception(util::format("(read) ", libusb_error_name(ret)));

    return read;
}

int KF_libusb::Write(const void* buf, int len)
{
    auto written = 0;
    auto ret = libusb_bulk_transfer(
        m_hdev,
        KF_EP_BULK_OUT,
        reinterpret_cast<uint8_t*>(const_cast<void*>(buf)),
        len,
        &written,
        KF_TIMEOUT_MS);

    if (ret != LIBUSB_SUCCESS)
        throw util::exception(util::format("(write) ", libusb_error_name(ret)));

    return written;
}

static void read_callback(libusb_transfer* xfer)
{
    auto pobj = reinterpret_cast<KF_libusb*>(xfer->user_data);
    pobj->ReadCallback(xfer);
}

void KF_libusb::ReadCallback(libusb_transfer* xfer)
{
    if (xfer->status != LIBUSB_TRANSFER_COMPLETED)
    {
        m_readret = xfer->status;
        return;
    }

    std::lock_guard<std::mutex> lock(m_readmutex);
    m_readbuf.insert(
        m_readbuf.end(),
        reinterpret_cast<const uint8_t*>(xfer->buffer),
        reinterpret_cast<const uint8_t*>(xfer->buffer) + xfer->actual_length);

    if (m_reading)
        m_readret = libusb_submit_transfer(xfer);
}

void KF_libusb::StartAsyncRead()
{
    if (m_reading)
        return;

    m_readbuf.clear();
    m_xferpool.resize(m_bufpool.size());

    int i = 0;
    for (auto& xfer : m_xferpool)
    {
        xfer = libusb_alloc_transfer(0);
        libusb_fill_bulk_transfer(
            xfer,
            m_hdev,
            KF_EP_BULK_IN,
            m_bufpool[i++].data(),
            m_bufpool[0].size(),
            read_callback,
            this,
            KF_TIMEOUT_MS);
    }

    for (auto& xfer : m_xferpool)
    {
        auto ret = libusb_submit_transfer(xfer);
        if (ret != LIBUSB_SUCCESS)
            throw util::exception(util::format("(submit) ", libusb_error_name(ret)));
    }

    m_reading = true;
}

void KF_libusb::StopAsyncRead()
{
    if (!m_reading)
        return;

    std::lock_guard<std::mutex> lock(m_readmutex);
    m_reading = false;

    for (auto& xfer : m_xferpool)
    {
        if (xfer)
            libusb_cancel_transfer(xfer);
    }

    m_xferpool.clear();
}

int KF_libusb::ReadAsync(void* buf, int len)
{
    struct timeval tv { KF_TIMEOUT_MS / 1000, (KF_TIMEOUT_MS % 1000) * 1000 };
    auto ret = libusb_handle_events_timeout(m_ctx, &tv);
    if (ret == LIBUSB_SUCCESS || ret == LIBUSB_ERROR_INTERRUPTED)
        ret = m_readret;

    if (ret != LIBUSB_SUCCESS)
        throw util::exception(util::format("(events) ", libusb_error_name(ret)));

    std::lock_guard<std::mutex> lock(m_readmutex);
    len = std::min(len, static_cast<int>(m_readbuf.size()));
    std::memcpy(buf, m_readbuf.data(), len);
    m_readbuf.erase(m_readbuf.begin(), m_readbuf.begin() + len);
    return len;
}

#endif // HAVE_LIBUSB1
