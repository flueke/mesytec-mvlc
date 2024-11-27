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

namespace
{
using namespace mesytec::mvlc;

static const unsigned WriteTimeout_ms = 2000;
// XXX: Do not raise the read timeout above 1000ms, otherwise mvme rate monitoring will break!
static const unsigned ReadTimeout_ms  = 1000;

}

namespace mesytec::mvlc::usb
{

Impl::Impl()
    : m_connectMode{ConnectMode::First, {}, {}}
{
}

Impl::Impl(unsigned index)
    : m_connectMode{ConnectMode::ByIndex, index, {}}
{
}

Impl::Impl(const std::string &serial)
    : m_connectMode{ConnectMode::BySerial, 0, serial}
{
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

    // Linux only: after post_connect_cleanup() is done set the command pipes
    // read timeout to 0 which has the effect of only reading from the FTDI
    // library buffer.
    if (auto ec = set_endpoint_timeout(m_handle, get_endpoint(Pipe::Command, EndpointDirection::In), 0))
    {
        closeHandle();
        return ec;
    }

    logger->trace("linux: CommandPipe read timeout set to 0");

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
    {
        st = FT_ReadPipe(
            m_handle,
            get_endpoint(pipe, EndpointDirection::In),
            buffer, size,
            &transferred,
            nullptr);
    }

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

    FT_STATUS st = FT_ReadPipe(
        m_handle, get_endpoint(pipe, EndpointDirection::In),
        buffer, size,
        &transferred,
        nullptr);

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
