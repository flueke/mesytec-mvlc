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
#include "mvlc_error.h"
#include <cassert>

namespace
{

class MVLCErrorCategory: public std::error_category
{
    const char *name() const noexcept override
    {
        return "mvlc_error";
    }

    std::string message(int ev) const override
    {
        using mesytec::mvlc::MVLCErrorCode;

        switch (static_cast<MVLCErrorCode>(ev))
        {
            case MVLCErrorCode::NoError:
                return "Ok";

            case MVLCErrorCode::IsConnected:
                return "MVLC is connected";

            case MVLCErrorCode::IsDisconnected:
                return "MVLC is disconnected";

            case MVLCErrorCode::ShortWrite:
                return "Short write";

            case MVLCErrorCode::ShortRead:
                return "Short read";

            case MVLCErrorCode::MirrorEmptyRequest:
                return "mirror check: empty request";

            case MVLCErrorCode::MirrorEmptyResponse:
                return "mirror check: empty response";

            case MVLCErrorCode::MirrorShortResponse:
                return "mirror check: response too short";

            case MVLCErrorCode::MirrorNotEqual:
                return "mirror check: mismatched mirror data";

            case MVLCErrorCode::MirrorMaxTriesExceeded:
                return "mirror transaction: max retries exceeded (possibly a network issue)";

            case MVLCErrorCode::InvalidBufferHeader:
                return "invalid MVLC buffer header";

            case MVLCErrorCode::ReadResponseMaxWaitExceeded:
                return "no command response received from MVLC";

            case MVLCErrorCode::UnexpectedResponseSize:
                return "unexpected response size";

            case MVLCErrorCode::CommandArgOutOfRange:
                return "command argument out of range";

            case MVLCErrorCode::NoVMEResponse:
                return "no VME response";

            case MVLCErrorCode::VMEBusError:
                return "VME bus error (BERR)";

            case MVLCErrorCode::HostLookupError:
                return "host lookup failed";

            case MVLCErrorCode::EmptyHostname:
                return "empty hostname/ip given";

            case MVLCErrorCode::BindLocalError:
                 return "could not bind local sockets";

            case MVLCErrorCode::InvalidPipe:
                 return "invalid pipe/endpoint";

            case MVLCErrorCode::SocketError:
                 return "generic socket error";

            case MVLCErrorCode::SocketReadTimeout:
                 return "socket read timeout";

            case MVLCErrorCode::SocketWriteTimeout:
                 return "socket write timeout";

            case MVLCErrorCode::UDPPacketChannelOutOfRange:
                 return "UDP packet channel out of range";

            case MVLCErrorCode::UDPDataWordCountExceedsPacketSize:
                 return "UDP dataWordCount exceeds received packet length";

            case MVLCErrorCode::UdpMaxOutgoingPacketSizeExceeded:
                 return "UDP outgoing max packet size exceeded";

            case MVLCErrorCode::StackCountExceeded:
                 return "number of stacks exceeded";

            case MVLCErrorCode::StackMemoryExceeded:
                 return "MVLC stack memory exceeded";

            case MVLCErrorCode::ImmediateStackReservedMemoryExceeded:
                 return "immediate stack reserved memory exceeeded";

            case MVLCErrorCode::StackSyntaxError:
                 return "Stack syntax error (wrong amod or opcode?)";

            case MVLCErrorCode::StackSegmentSizeExceeded:
                 return "Stack segment size (128 words) exceeded";

             case MVLCErrorCode::Stack0IsReserved:
                 return "Stack 0 is reserved for immediate commands";

            case MVLCErrorCode::MirrorTransactionMaxWordsExceeded:
                 return "Mirror transaction max words exceeded";

            case MVLCErrorCode::InvalidStackHeader:
                 return "Invalid stack header";

            case MVLCErrorCode::NonBlockAddressMode:
                 return "Non-block VME address mode given";

            case MVLCErrorCode::TimerCountExceeded:
                 return "Timer count exceeded";

            case MVLCErrorCode::ReadoutSetupError:
                 return "Readout Setup Error";

            case MVLCErrorCode::UnexpectedBufferHeader:
                 return "Unexpected buffer header";

            case MVLCErrorCode::InUse:
                 return "MVLC is in use";

            case MVLCErrorCode::USBChipConfigError:
                 return "Incorrect USB chip configuration (FTDI)";

            case MVLCErrorCode::SuperCommandTimeout:
                 return "MVLC Super Command Timeout";

            case MVLCErrorCode::StackCommandTimeout:
                 return "MVLC Stack Command Timeout";

            case MVLCErrorCode::ShortSuperFrame:
                 return "ShortSuperFrame";

            case MVLCErrorCode::SuperFormatError:
                 return "SuperFormatError";

            case MVLCErrorCode::StackFormatError:
                 return "StackFormatError";

            case MVLCErrorCode::SuperReferenceMismatch:
                 return "SuperReferenceMismatch";

            case MVLCErrorCode::StackReferenceMismatch:
                 return "StackReferenceMismatch";

            case MVLCErrorCode::StackExecRequestLost:
                return "StackExecRequestLost";

            case MVLCErrorCode::StackExecResponseLost:
                return "StackExecResponseLost";

            case MVLCErrorCode::FirmwareTooOld:
                 return "FirmwareTooOld (MVLC firmware update required)";

            case MVLCErrorCode::ErrorCodeMax:
                 return "<internal ErrorCodeMax value>";
        }

        return "unrecognized MVLC error";
    }

    std::error_condition default_error_condition(int ev) const noexcept override
    {
        using mesytec::mvlc::MVLCErrorCode;
        using mesytec::mvlc::ErrorType;

        switch (static_cast<MVLCErrorCode>(ev))
        {
            case MVLCErrorCode::NoError:
            case MVLCErrorCode::ErrorCodeMax:
                return ErrorType::Success;

            case MVLCErrorCode::IsConnected:
            case MVLCErrorCode::IsDisconnected:
            case MVLCErrorCode::HostLookupError:
            case MVLCErrorCode::BindLocalError:
            case MVLCErrorCode::SocketError:
            case MVLCErrorCode::EmptyHostname:
            case MVLCErrorCode::InUse:
            case MVLCErrorCode::USBChipConfigError:
            case MVLCErrorCode::MirrorMaxTriesExceeded:
            case MVLCErrorCode::FirmwareTooOld:
            case MVLCErrorCode::StackExecRequestLost:
            case MVLCErrorCode::StackExecResponseLost:
                return ErrorType::ConnectionError;

            case MVLCErrorCode::ShortWrite:
            case MVLCErrorCode::ShortRead:
                return ErrorType::ShortTransfer;

            case MVLCErrorCode::MirrorEmptyRequest:
            case MVLCErrorCode::MirrorEmptyResponse:
            case MVLCErrorCode::MirrorShortResponse:
            case MVLCErrorCode::MirrorNotEqual:
            case MVLCErrorCode::InvalidBufferHeader:
            case MVLCErrorCode::UnexpectedResponseSize:
            case MVLCErrorCode::CommandArgOutOfRange:
            case MVLCErrorCode::InvalidPipe:
            case MVLCErrorCode::StackCountExceeded:
            case MVLCErrorCode::StackMemoryExceeded:
            case MVLCErrorCode::ImmediateStackReservedMemoryExceeded:
            case MVLCErrorCode::StackSyntaxError:
            case MVLCErrorCode::StackSegmentSizeExceeded:
            case MVLCErrorCode::Stack0IsReserved:
            case MVLCErrorCode::MirrorTransactionMaxWordsExceeded:
            case MVLCErrorCode::InvalidStackHeader:
            case MVLCErrorCode::TimerCountExceeded: // FIXME: does not belong here (used in mvlc_daq.cc)
            case MVLCErrorCode::ReadoutSetupError:  // FIXME: does not belong here (used in mvlc_daq.cc)
            case MVLCErrorCode::UnexpectedBufferHeader:
            case MVLCErrorCode::UDPPacketChannelOutOfRange:
            case MVLCErrorCode::UDPDataWordCountExceedsPacketSize:
            case MVLCErrorCode::UdpMaxOutgoingPacketSizeExceeded:
            case MVLCErrorCode::NonBlockAddressMode:
            case MVLCErrorCode::ShortSuperFrame:
            case MVLCErrorCode::SuperFormatError:
            case MVLCErrorCode::StackFormatError:
            case MVLCErrorCode::SuperReferenceMismatch:
            case MVLCErrorCode::StackReferenceMismatch:
                return ErrorType::ProtocolError;

            case MVLCErrorCode::NoVMEResponse:
            case MVLCErrorCode::VMEBusError:
                return ErrorType::VMEError;

            case MVLCErrorCode::SocketReadTimeout:
            case MVLCErrorCode::SocketWriteTimeout:
            case MVLCErrorCode::ReadResponseMaxWaitExceeded:
            case MVLCErrorCode::SuperCommandTimeout:
            case MVLCErrorCode::StackCommandTimeout:
                return ErrorType::Timeout;
        }
        assert(false);
        return {};
    }
};

const MVLCErrorCategory theMVLCErrorCategory {};

class ErrorTypeCategory: public std::error_category
{
    const char *name() const noexcept override
    {
        return "mvlc error type";
    }

    std::string message(int ev) const override
    {
        using mesytec::mvlc::ErrorType;

        switch (static_cast<ErrorType>(ev))
        {
            case ErrorType::Success:
                return "Success";

            case ErrorType::ConnectionError:
                return "Connection Error";

            case ErrorType::Timeout:
                return "Timeout";

            case ErrorType::ShortTransfer:
                return "Short Transfer";

            case ErrorType::ProtocolError:
                return "MVLC Protocol Error";

            case ErrorType::VMEError:
                return "VME Error";
        }

        return "unrecognized error type";
    }

    // Equivalence between local conditions and any error code
    bool equivalent(const std::error_code &ec, int condition) const noexcept override
    {
        using mesytec::mvlc::ErrorType;

        switch (static_cast<ErrorType>(condition))
        {
            case ErrorType::Timeout:
                return ec == std::error_code(EAGAIN, std::system_category());

            default:
                break;
        }

        return false;
    }
};

const ErrorTypeCategory theErrorTypeCategory;

} // end anon namespace

namespace mesytec
{
namespace mvlc
{

std::error_code make_error_code(MVLCErrorCode error)
{
    return { static_cast<int>(error), theMVLCErrorCategory };
}

std::error_condition make_error_condition(ErrorType et)
{
    return { static_cast<int>(et), theErrorTypeCategory };
}

} // end namespace mvlc
} // end namespace mesytec
