/* mesytec-mvlc - driver library for the Mesytec MVLC VME controller
 *
 * Copyright (c) 2020-2023 mesytec GmbH & Co. KG
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __MESYTEC_MVLC_MVLC_ETH_INTERFACE_H__
#define __MESYTEC_MVLC_MVLC_ETH_INTERFACE_H__

#include <system_error>

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mvlc_constants.h"
#include "mvlc_counters.h"

namespace mesytec::mvlc::eth
{

struct MESYTEC_MVLC_EXPORT PayloadHeaderInfo
{
    u32 header0;
    u32 header1;

    inline u16 packetChannel() const
    {
        return (header0 >> header0::PacketChannelShift) & header0::PacketChannelMask;
    }

    inline u16 packetNumber() const
    {
        return (header0 >> header0::PacketNumberShift)  & header0::PacketNumberMask;
    }

    inline u16 controllerId() const
    {
        return (header0 >> header0::CtrlIdShift) & header0::CtrlIdMask;
    }

    inline u16 dataWordCount() const
    {
        return (header0 >> header0::NumDataWordsShift)  & header0::NumDataWordsMask;
    }

    inline u16 udpTimestamp() const
    {
        return (header1 >> header1::TimestampShift)     & header1::TimestampMask;
    }

    inline u16 nextHeaderPointer() const
    {
        return (header1 >> header1::HeaderPointerShift) & header1::HeaderPointerMask;
    }

    inline u16 isNextHeaderPointerPresent() const
    {
        return nextHeaderPointer() != header1::NoHeaderPointerPresent;
    }

    inline bool isNextHeaderPointerValid() const
    {
        if (isNextHeaderPointerPresent())
            return nextHeaderPointer() < dataWordCount();
        return false;
    }
};

// Single line info about each header
std::string MESYTEC_MVLC_EXPORT eth_header0_to_string(u32 header0);
std::string MESYTEC_MVLC_EXPORT eth_header1_to_string(u32 header1);
// Info about both headers in one line.
std::string MESYTEC_MVLC_EXPORT eth_headers_to_string(u32 header0, u32 header1);
std::string MESYTEC_MVLC_EXPORT eth_headers_to_string(const PayloadHeaderInfo &info);

struct MESYTEC_MVLC_EXPORT PacketReadResult
{
    std::error_code ec;
    u8 *buffer;             // Equal to the dest pointer passed to read_packet()
    u16 bytesTransferred;
    s32 lostPackets;        // Loss between the previous and current packets

    inline bool hasHeaders() const { return bytesTransferred >= HeaderBytes; }

    inline u32 header0() const { return reinterpret_cast<u32 *>(buffer)[0]; }
    inline u32 header1() const { return reinterpret_cast<u32 *>(buffer)[1]; }

    inline u16 packetChannel() const
    {
        return PayloadHeaderInfo{header0(), header1()}.packetChannel();
    }

    inline u16 packetNumber() const
    {
        return PayloadHeaderInfo{header0(), header1()}.packetNumber();
    }

    inline u16 controllerId() const
    {
        return PayloadHeaderInfo{header0(), header1()}.controllerId();
    }

    inline u16 dataWordCount() const
    {
        return PayloadHeaderInfo{header0(), header1()}.dataWordCount();
    }

    inline u16 udpTimestamp() const
    {
        return PayloadHeaderInfo{header0(), header1()}.udpTimestamp();
    }

    inline u16 nextHeaderPointer() const
    {
        return PayloadHeaderInfo{header0(), header1()}.nextHeaderPointer();
    }

    inline u16 availablePayloadWords() const
    {
        if (bytesTransferred < HeaderBytes)
            return 0u;

        return (bytesTransferred - HeaderBytes) / sizeof(u32);
    }

    inline u16 leftoverBytes() const
    {
        return bytesTransferred % sizeof(u32);
    }

    inline u32 *payloadBegin() const
    {
        return reinterpret_cast<u32 *>(buffer + HeaderBytes);
    }

    inline u32 *payloadEnd() const
    {
        return payloadBegin() + availablePayloadWords();
    }

    inline bool hasNextHeaderPointer() const
    {
        const u16 nhp = nextHeaderPointer();

        return nhp != header1::NoHeaderPointerPresent;
    }

    inline bool isNextHeaderPointerValid() const
    {
        if (hasNextHeaderPointer())
        {
            const u16 nhp = nextHeaderPointer();
            return payloadBegin() + nhp < payloadEnd();
        }

        return false;
    }
};

struct EthThrottleCounters
{
    u32 rcvBufferSize = 0u;
    u32 rcvBufferUsed = 0u;
    u16 currentDelay = 0u;
    u16 maxDelay = 0u;
    float avgDelay = 0u;
};

class MVLC_ETH_Interface
{
    public:
        virtual ~MVLC_ETH_Interface() {}

        virtual PacketReadResult read_packet(Pipe pipe, u8 *buffer, size_t size) = 0;
        virtual std::array<eth::PipeStats, PipeCount> getPipeStats() const = 0;
        virtual std::array<PacketChannelStats, NumPacketChannels> getPacketChannelStats() const = 0;
        virtual void resetPipeAndChannelStats() = 0;

        virtual EthThrottleCounters getThrottleCounters() const = 0;
};

}

#endif /* __MESYTEC_MVLC_MVLC_ETH_INTERFACE_H__ */
