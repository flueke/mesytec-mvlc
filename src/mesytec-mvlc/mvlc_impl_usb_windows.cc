/* mesytec-mvlc - driver library for the Mesytec MVLC VME controller
 *
 * Copyright (C) 2020-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "mvlc_impl_usb.h"
#include "mvlc_impl_usb_internal.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <future>
#include <iomanip>
#include <numeric>
#include <regex>

#include "mvlc_dialog.h"
#include "mvlc_dialog_util.h"
#include "mvlc_error.h"
#include "mvlc_util.h"
#include "util/logging.h"

#define USB_WIN_USE_ASYNC 0
// TODO: remove the non-ex code paths
#define USB_WIN_USE_EX_FUNCTIONS 1 // Currently only implemented for the SYNC case.
#define USB_WIN_USE_STREAMPIPE 1

namespace
{
using namespace mesytec::mvlc;

static const unsigned WriteTimeout_ms = 2000;
// XXX: Do not raise the read timeout above 1000ms, otherwise mvme rate monitoring will break!
static const unsigned ReadTimeout_ms  = 1000;

}

namespace
{

static const size_t DataBufferSize = usb::USBStreamPipeReadSize;

template<typename Impl>
std::pair<std::error_code, size_t> read_pipe_until_empty(
    Impl &impl, Pipe pipe, std::shared_ptr<spdlog::logger> &logger)
{
    size_t totalBytesTransferred = 0;
    size_t bytesTransferred = 0;
    std::array<u8, DataBufferSize> buffer;
    std::error_code ec = {};
    do
    {
        bytesTransferred = 0;
        ec = impl.read_unbuffered(pipe, buffer.data(), buffer.size(), bytesTransferred);
        totalBytesTransferred += bytesTransferred;

        if (logger)
        {
            logger->debug("read_pipe_until_empty: pipe={}, ec={}, bytes={}",
                          static_cast<int>(pipe), ec.message(), bytesTransferred);

            log_buffer(logger, spdlog::level::trace,
                       basic_string_view<u32>(reinterpret_cast<u32*>(buffer.data()), bytesTransferred/sizeof(u32)),
                       fmt::format("read_pipe_until_empty: pipe={}, ec={}, bytes={}, data:",
                                   static_cast<int>(pipe), ec.message(), bytesTransferred)
                       );
        }

        if (ec == ErrorType::ConnectionError)
            break;
    } while (bytesTransferred > 0);

    return std::make_pair(ec, totalBytesTransferred);
};

#ifdef MESYTEC_MVLC_PLATFORM_WINDOWS
std::error_code abort_pipe(void *ftdiHandle, Pipe pipe, mesytec::mvlc::usb::EndpointDirection dir)
{
    auto logger = get_logger("mvlc_usb");

    logger->trace(
        "FT_AbortPipe on pipe={}, dir={}",
        static_cast<unsigned>(pipe),
        static_cast<unsigned>(dir));

    auto st = FT_AbortPipe(ftdiHandle, get_endpoint(pipe, dir));

    if (auto ec = mesytec::mvlc::usb::make_error_code(st))
    {
        logger->warn(
            "FT_AbortPipe on pipe={}, dir={} returned an error: {}",
            static_cast<unsigned>(pipe),
            static_cast<unsigned>(dir),
            ec.message().c_str());
        return ec;
    }
    return {};
}
#endif // __WIN32

} // end anon namespace

namespace mesytec::mvlc::usb
{


//
// Impl
//
Impl::Impl()
    : m_connectMode{ConnectMode::First, {}, {}}
{
#ifdef MESYTEC_MVLC_PLATFORM_WINDOWS
    m_readBuffers.resize(PipeCount);
#endif
}

Impl::Impl(unsigned index)
    : m_connectMode{ConnectMode::ByIndex, index, {}}
{
#ifdef MESYTEC_MVLC_PLATFORM_WINDOWS
    m_readBuffers.resize(PipeCount);
#endif
}

Impl::Impl(const std::string &serial)
    : m_connectMode{ConnectMode::BySerial, 0, serial}
{
#ifdef MESYTEC_MVLC_PLATFORM_WINDOWS
    m_readBuffers.resize(PipeCount);
#endif
}

Impl::~Impl()
{
    disconnect();
}

std::error_code Impl::closeHandle()
{
    FT_STATUS st = FT_OK;

    if (m_handle)
    {
        st = FT_Close(m_handle);
        m_handle = nullptr;
    }

    return make_error_code(st);
}

std::error_code Impl::connect()
{
    auto logger = get_logger("mvlc_usb");
    logger->trace("begin {}", __PRETTY_FUNCTION__);

    if (isConnected())
        return make_error_code(MVLCErrorCode::IsConnected);

    FT_STATUS st = FT_OK;

    // Open the USB device. Try multiple times because with USB2 FT_Create()
    // sometimes fails the first time.
    const int MaxOpenAttempts = 3;

    auto infoList = get_device_info_list();

    for (int attempt=0; attempt<MaxOpenAttempts; ++attempt)
    {
        switch (m_connectMode.mode)
        {
            case ConnectMode::First:
                {
                    st = FT_DEVICE_NOT_FOUND;

                    if (!infoList.empty())
                    {
                        m_deviceInfo = infoList[0];
                        st = FT_Create(reinterpret_cast<void *>(m_deviceInfo.index),
                                       FT_OPEN_BY_INDEX, &m_handle);
                    }
                }
                break;

            case ConnectMode::ByIndex:
                {
                    st = FT_DEVICE_NOT_FOUND;

                    for (auto &info: infoList)
                    {
                        if (info.index == static_cast<int>(m_connectMode.index))
                        {
                            m_deviceInfo = info;
                            st = FT_Create(reinterpret_cast<void *>(m_deviceInfo.index),
                                           FT_OPEN_BY_INDEX, &m_handle);
                            break;
                        }
                    }
                }
                break;

            case ConnectMode::BySerial:
                {
                    st = FT_DEVICE_NOT_FOUND;

                    if ((m_deviceInfo = get_device_info_by_serial(infoList, m_connectMode.serial)))
                    {
                        st = FT_Create(reinterpret_cast<void *>(m_deviceInfo.index),
                                       FT_OPEN_BY_INDEX, &m_handle);
                    }
                }
                break;
        }

        if (st == FT_OK)
            break;
    }

    auto ec = make_error_code(st);

    logger->trace("FT_Create done: {}", ec.message());

    if (ec)
        return ec;

    if (auto ec = check_chip_configuration(m_handle))
    {
        closeHandle();
        return ec;
    }

    logger->trace("check_chip_configuration done");

    // Set actual read timeouts on the command and data pipes. Note that for
    // linux the command pipe read timeout is set to 0 later on. This initial
    // non-zero timeout is used to make the MVLCDialog operations in
    // post_connect_cleanup() work.
    for (auto pipe: { Pipe::Command, Pipe::Data})
    {
        if (auto ec = set_endpoint_timeout(
                m_handle, get_endpoint(pipe, EndpointDirection::In),
                ReadTimeout_ms))
        {
            closeHandle();
            return ec;
        }

        if (auto ec = set_endpoint_timeout(
                m_handle, get_endpoint(pipe, EndpointDirection::Out),
                WriteTimeout_ms))
        {
            closeHandle();
            return ec;
        }
    }

    logger->trace("set pipe timeouts done");

#ifdef MESYTEC_MVLC_PLATFORM_WINDOWS
    // clean up the pipes
    for (auto pipe: { Pipe::Command, Pipe::Data })
    {
        for (auto dir: { EndpointDirection::In, EndpointDirection::Out })
        {
            if (auto ec = abort_pipe(m_handle, pipe, dir))
            {
                closeHandle();
                return ec;
            }
        }
    }
    logger->trace("win32 pipe cleanup done");
#endif

#ifdef MESYTEC_MVLC_PLATFORM_WINDOWS
#if USB_WIN_USE_STREAMPIPE
    logger->trace("enabling streaming mode for all read pipes, size={}", USBStreamPipeReadSize);
    // FT_SetStreamPipe(handle, allWritePipes, allReadPipes, pipeID, streamSize)
    st = FT_SetStreamPipe(m_handle, false, true, 0, USBStreamPipeReadSize);

    if (auto ec = make_error_code(st))
    {
        logger->error("FT_SetStreamPipe failed: {}", ec.message());
        closeHandle();
        return ec;
    }

    logger->trace("win32 streampipe mode enabled");
#endif // USB_WIN_USE_STREAMPIPE
#endif // __WIN32

    logger->info("opened USB device");

    if (disableTriggersOnConnect())
    {
        for (int try_=0; try_<2; ++try_)
        {
            if (auto ec = post_connect_cleanup(*this))
            {
                logger->warn("error from USB post connect cleanup: {}", ec.message());
                return ec;
            }
            else
                break;
        }
    }

#ifndef MESYTEC_MVLC_PLATFORM_WINDOWS
    // Linux only: after post_connect_cleanup() is done set the command pipes
    // read timeout to 0 which has the effect of only reading from the FTDI
    // library buffer.
    if (auto ec = set_endpoint_timeout(m_handle, get_endpoint(Pipe::Command, EndpointDirection::In), 0))
    {
        closeHandle();
        return ec;
    }

    logger->trace("linux: CommandPipe read timeout set to 0");
#endif

    logger->trace("end {}", __PRETTY_FUNCTION__);

    return {};
}

std::error_code Impl::disconnect()
{
    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    auto ec = closeHandle();

    return ec;
}

bool Impl::isConnected() const
{
    return m_handle != nullptr;
}

#ifdef MESYTEC_MVLC_PLATFORM_WINDOWS
std::error_code Impl::write(Pipe pipe, const u8 *buffer, size_t size,
                            size_t &bytesTransferred)
{
    auto logger = get_logger("mvlc_usb");

    assert(buffer);
    assert(size <= USBSingleTransferMaxBytes);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    if (static_cast<unsigned>(pipe) >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    ULONG transferred = 0; // FT API needs a ULONG*

    logger->trace("write(): pipe={}, size={}", static_cast<unsigned>(pipe), size);

#if !USB_WIN_USE_ASYNC

#if USB_WIN_USE_EX_FUNCTIONS

    static const int MaxWriteAttempts = 3;
    FT_STATUS st = FT_OK;

    for (int writeAttempt=0;
         writeAttempt<MaxWriteAttempts;
         ++writeAttempt)
    {
        logger->trace("write(): sync write (Ex variant), attempt {}/{}", writeAttempt+1, MaxWriteAttempts);

        st = FT_WritePipeEx(
            m_handle, get_endpoint(pipe, EndpointDirection::Out),
            const_cast<u8 *>(buffer), size,
            &transferred,
            nullptr);

        if (st != FT_OK && st != FT_IO_PENDING)
        {
            if (auto ec = abort_pipe(m_handle, pipe, EndpointDirection::Out))
                return ec;
        }

        if (st == FT_TIMEOUT && transferred == 0)
        {
            logger->warn("write(): retrying write of size {}, attempt={}/{}", size, writeAttempt+1, MaxWriteAttempts);
            continue;
        }
        break;
    }

#else // !USB_WIN_USE_EX_FUNCTIONS
    logger->trace("sync write");
    FT_STATUS st = FT_WritePipe(
        m_handle, get_endpoint(pipe, EndpointDirection::Out),
        const_cast<u8 *>(buffer), size,
        &transferred,
        nullptr);
#endif // USB_WIN_USE_EX_FUNCTIONS

#else // USB_WIN_USE_ASYNC
    FT_STATUS st = FT_OK;
    {
        logger->trace("async write");
        OVERLAPPED vOverlapped = {};
        std::memset(&vOverlapped, 0, sizeof(vOverlapped));
        vOverlapped.hEvent = CreateEvent(nullptr, false, false, nullptr);
        //st = FT_InitializeOverlapped(m_handle, &vOverlapped);

        //qDebug("%s: vOverlapped.hEvent after call to FT_InitializeOverlapped: %p",
        //       __PRETTY_FUNCTION__, vOverlapped.hEvent);

        //if (auto ec = make_error_code(st))
        //{
        //    logger->warn("pipe=%u, FT_InitializeOverlapped failed: ec=%s",
        //             static_cast<unsigned>(pipe), ec.message().c_str());
        //    return ec;
        //}

        st = FT_WritePipe(
            m_handle, get_endpoint(pipe, EndpointDirection::Out),
            const_cast<u8 *>(buffer), size,
            &transferred,
            &vOverlapped);

        if (st == FT_IO_PENDING)
            st = FT_GetOverlappedResult(m_handle, &vOverlapped, &transferred, true);

        CloseHandle(vOverlapped.hEvent);
        //FT_ReleaseOverlapped(m_handle, &vOverlapped);
    }
#endif // USB_WIN_USE_ASYNC

    if (st != FT_OK && st != FT_IO_PENDING)
        abort_pipe(m_handle, pipe, EndpointDirection::Out);

    bytesTransferred = transferred;

    auto ec = make_error_code(st);

    if (ec)
    {
        logger->warn("write(): pipe={}, wrote {} of {} bytes, result={}",
                 static_cast<unsigned>(pipe),
                 bytesTransferred, size,
                 ec.message().c_str());
    }

    return ec;
}

#else // Impl::write() linux

std::error_code Impl::write(Pipe pipe, const u8 *buffer, size_t size,
                            size_t &bytesTransferred)
{
    auto logger = get_logger("mvlc_usb");

    assert(buffer);
    assert(size <= USBSingleTransferMaxBytes);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    if (static_cast<unsigned>(pipe) >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    ULONG transferred = 0; // FT API needs a ULONG*

    FT_STATUS st = FT_WritePipeEx(m_handle, get_fifo_id(pipe),
                                  const_cast<u8 *>(buffer), size,
                                  &transferred,
                                  WriteTimeout_ms);

    bytesTransferred = transferred;

    auto ec = make_error_code(st);

    if (ec)
    {
        logger->warn("pipe={}, wrote {} of {} bytes, result={}",
                 static_cast<unsigned>(pipe),
                 bytesTransferred, size,
                 ec.message().c_str());
    }

    return ec;
}
#endif

#ifdef MESYTEC_MVLC_PLATFORM_WINDOWS

// Update Tue 11/05/2019:
// The note below was written before trying out overlapped I/O. This might need
// to be updated.

/* Explanation:
 * When reading from a pipe under windows any available data that was not
 * retrieved is lost instead of being returned on the next read attempt. This
 * is different than the behaviour under linux where multiple short reads can
 * be done without losing data.
 * Also the windows library does not run into a timeout if less data than
 * requested is available.
 * To work around the above issue the windows implementation uses a single read
 * buffer of size USBSingleTransferMaxBytes and only issues read requests of
 * that size. Client requests are satisfied from buffered data until the buffer
 * is empty at which point another full sized read is performed.
 */
std::error_code Impl::read(Pipe pipe, u8 *buffer, size_t size,
                           size_t &bytesTransferred)
{
    auto logger = get_logger("mvlc_usb");

    assert(buffer);
    assert(size <= USBSingleTransferMaxBytes);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    if (static_cast<unsigned>(pipe) >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    if (size == 0)
        return {};

    const size_t requestedSize = size;
    bytesTransferred = 0u;

    auto &readBuffer = m_readBuffers[static_cast<unsigned>(pipe)];

    // Copy from readBuffer into the dest buffer while updating local
    // variables.
    auto copy_and_update = [&buffer, &size, &bytesTransferred, &readBuffer] ()
    {
        if (size_t toCopy = std::min(readBuffer.size(), size))
        {
            memcpy(buffer, readBuffer.first, toCopy);
            buffer += toCopy;
            size -= toCopy;
            readBuffer.first += toCopy;
            bytesTransferred += toCopy;
        }
    };

    logger->trace("read(): pipe={}, size={}, bufferSize={}",
              static_cast<unsigned>(pipe), requestedSize, readBuffer.size());

    copy_and_update();

    if (size == 0)
    {
        logger->trace("read(): pipe={}, size={}, read request satisfied from buffer, new buffer size={}",
                  static_cast<unsigned>(pipe), requestedSize, readBuffer.size());
        return {};
    }

    // All data from the read buffer should have been consumed at this point.
    // It's time to issue an actual read request.
    assert(readBuffer.size() == 0);

    logger->trace("read(): pipe={}, requestedSize={}, remainingSize={}, reading from MVLC...",
              static_cast<unsigned>(pipe), requestedSize, size);

    ULONG transferred = 0; // FT API wants a ULONG* parameter

#if !USB_WIN_USE_ASYNC

#if USB_WIN_USE_STREAMPIPE
    if (readBuffer.capacity() != USBStreamPipeReadSize)
        throw std::runtime_error("Read size does not equal stream pipe size");
#endif

#if USB_WIN_USE_EX_FUNCTIONS
    logger->trace("read(): sync read (Ex variant)");

    FT_STATUS st = FT_ReadPipeEx(
        m_handle, get_endpoint(pipe, EndpointDirection::In),
        readBuffer.data.data(),
        readBuffer.capacity(),
        &transferred,
        nullptr);
#else // !USB_WIN_USE_EX_FUNCTIONS
    logger->trace("sync read");

    FT_STATUS st = FT_ReadPipe(
        m_handle, get_endpoint(pipe, EndpointDirection::In),
        readBuffer.data.data(),
        readBuffer.capacity(),
        &transferred,
        nullptr);
#endif // USB_WIN_USE_EX_FUNCTIONS

#else // USB_WIN_USE_ASYNC
    FT_STATUS st = FT_OK;
    {
        logger->trace("async read");
        OVERLAPPED vOverlapped = {};
        std::memset(&vOverlapped, 0, sizeof(vOverlapped));
        vOverlapped.hEvent = CreateEvent(nullptr, false, false, nullptr);
        //st = FT_InitializeOverlapped(m_handle, &vOverlapped);

        //if (auto ec = make_error_code(st))
        //{
        //    logger->warn("pipe=%u, FT_InitializeOverlapped failed: ec=%s",
        //             static_cast<unsigned>(pipe), ec.message().c_str());
        //    return ec;
        //}

        st = FT_ReadPipe(
            m_handle, get_endpoint(pipe, EndpointDirection::In),
            readBuffer.data.data(),
            readBuffer.capacity(),
            &transferred,
            &vOverlapped);

        if (st == FT_IO_PENDING)
            st = FT_GetOverlappedResult(m_handle, &vOverlapped, &transferred, true);

        CloseHandle(vOverlapped.hEvent);
        //FT_ReleaseOverlapped(m_handle, &vOverlapped);
    }
#endif // USB_WIN_USE_ASYNC

    if (st != FT_OK && st != FT_IO_PENDING)
        abort_pipe(m_handle, pipe, EndpointDirection::In);

    auto ec = make_error_code(st);

    logger->trace("read(): pipe={}, requestedSize={}, remainingSize={}, read result: ec={}, transferred={}",
                  static_cast<unsigned>(pipe), requestedSize, size,
                  ec.message().c_str(), transferred);

    readBuffer.first = readBuffer.data.data();
    readBuffer.last  = readBuffer.first + transferred;

    copy_and_update();


#if 0
    if (ec && ec != ErrorType::Timeout)
        return ec;

    if (size > 0)
    {
        logger->debug("pipe={}, requestedSize={}, remainingSize={} after read from MVLC, "
                  "returning FT_TIMEOUT (original ec={})",
                  static_cast<unsigned>(pipe), requestedSize, size,
                  ec.message().c_str());

        return make_error_code(FT_TIMEOUT);
    }
#endif

    logger->trace("read(): pipe={}, size={}, read request satisfied after read from MVLC. new buffer size={}",
                  static_cast<unsigned>(pipe), requestedSize, readBuffer.size());

    return ec;
}
#else // Impl::read() linux
std::error_code Impl::read(Pipe pipe, u8 *buffer, size_t size,
                           size_t &bytesTransferred)
{
    auto logger = get_logger("mvlc_usb");

    assert(buffer);
    assert(size <= USBSingleTransferMaxBytes);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    if (static_cast<unsigned>(pipe) >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    //logger->trace("begin read: pipe={}, size={} bytes",
    //          static_cast<unsigned>(pipe), size);

    ULONG transferred = 0; // FT API wants a ULONG* parameter

    FT_STATUS st = {};

//#if 0
    if (getDeviceInfo().flags & DeviceInfo::Flags::USB2)
    {
        // Possible fix for USB2 CommandTimeout issues with older USB chipsets:
        // explicitly specify a read timeout here.
        // -> Makes USB communication very slow.
        st = FT_ReadPipeEx(
            m_handle,
            get_fifo_id(pipe),
            buffer, size,
            &transferred,
            1);
    }
    else
//#else
    {
        #ifdef __x86_64__
        st = FT_ReadPipe(
            m_handle,
            get_endpoint(pipe, EndpointDirection::In),
            buffer, size,
            &transferred,
            nullptr);
        #else
        st = FT_ReadPipe(
            m_handle,
            get_endpoint(pipe, EndpointDirection::In),
            buffer, size,
            &transferred,
            1);
        #endif
    }
//#endif

    bytesTransferred = transferred;

    auto ec = make_error_code(st);

    if (ec && ec != ErrorType::Timeout)
    {
        logger->warn("pipe={}, read {} of {} bytes, result={}",
                     static_cast<unsigned>(pipe),
                     bytesTransferred, size,
                     ec.message().c_str());
    }

    return ec;
}
#endif // Impl::read

std::error_code Impl::read_unbuffered(Pipe pipe, u8 *buffer, size_t size,
                                      size_t &bytesTransferred)
{
    auto logger = get_logger("mvlc_usb");

    assert(buffer);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    if (static_cast<unsigned>(pipe) >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    //logger->trace("begin unbuffered read: pipe={}, size={} bytes",
    //          static_cast<unsigned>(pipe), size);

    ULONG transferred = 0; // FT API wants a ULONG* parameter
    std::error_code ec = {};

#ifdef MESYTEC_MVLC_PLATFORM_WINDOWS
#if !USB_WIN_USE_ASYNC

#if USB_WIN_USE_STREAMPIPE
    //logger->info("streampipe check");
    if (size != usb::USBSingleTransferMaxBytes)
        throw std::runtime_error("Read size does not equal stream pipe size");
#endif

#if USB_WIN_USE_EX_FUNCTIONS
    FT_STATUS st = FT_ReadPipeEx(
        m_handle, get_endpoint(pipe, EndpointDirection::In),
        buffer, size,
        &transferred,
        nullptr);
#else // !USB_WIN_USE_EX_FUNCTIONS
    FT_STATUS st = FT_ReadPipe(
        m_handle, get_endpoint(pipe, EndpointDirection::In),
        buffer, size,
        &transferred,
        nullptr);
#endif

#else // USB_WIN_USE_ASYNC
    FT_STATUS st = FT_OK;
    {
        //logger->warn("async read_unbuffered");
        OVERLAPPED vOverlapped = {};
        std::memset(&vOverlapped, 0, sizeof(vOverlapped));
        vOverlapped.hEvent = CreateEvent(nullptr, false, false, nullptr);
        //st = FT_InitializeOverlapped(m_handle, &vOverlapped);

        //if ((ec = make_error_code(st)))
        //{
        //    logger->warn("pipe=%u, FT_InitializeOverlapped failed: ec=%s",
        //             static_cast<unsigned>(pipe), ec.message().c_str());
        //    return ec;
        //}

        st = FT_ReadPipe(
            m_handle, get_endpoint(pipe, EndpointDirection::In),
            buffer, size,
            &transferred,
            &vOverlapped);

        if (st == FT_IO_PENDING)
            st = FT_GetOverlappedResult(m_handle, &vOverlapped, &transferred, true);

        CloseHandle(vOverlapped.hEvent);
        //FT_ReleaseOverlapped(m_handle, &vOverlapped);
    }

#endif // USB_WIN_USE_ASYNC

    logger->trace("result from unbuffered read: pipe={}, size={} bytes, ec={}",
              static_cast<unsigned>(pipe), size, make_error_code(st).message().c_str());

    if (st != FT_OK && st != FT_IO_PENDING)
        abort_pipe(m_handle, pipe, EndpointDirection::In);

#else // linux
    #ifdef __x86_64__
    FT_STATUS st = FT_ReadPipe(
        m_handle, get_endpoint(pipe, EndpointDirection::In),
        buffer, size,
        &transferred,
        nullptr);
    #else
    FT_STATUS st = FT_ReadPipe(
        m_handle, get_endpoint(pipe, EndpointDirection::In),
        buffer, size,
        &transferred,
        1);
    #endif
#endif

    bytesTransferred = transferred;
    ec = make_error_code(st);

    logger->trace("end unbuffered read: pipe={}, size={} bytes, transferred={} bytes, ec={}",
              static_cast<unsigned>(pipe), size, bytesTransferred, ec.message().c_str());

    return ec;
}

std::string Impl::connectionInfo() const
{
    std::string result = "mvlc_usb: speed=";

    auto devInfo = getDeviceInfo();

    if (devInfo.flags & DeviceInfo::Flags::USB2)
        result += "USB2";
    else if (devInfo.flags & DeviceInfo::Flags::USB3)
        result += "USB3";
    else
        result += "<unknown>";

    const auto serialString = devInfo.serial.empty() ? std::string("<unknown>") : devInfo.serial;

    result += ", serial='" + serialString + "'";

    return result;
}

std::error_code set_endpoint_timeout(void *handle, u8 ep, unsigned ms)
{
    FT_STATUS st = FT_SetPipeTimeout(handle, ep, ms);
    return mesytec::mvlc::usb::make_error_code(st);
}


}
