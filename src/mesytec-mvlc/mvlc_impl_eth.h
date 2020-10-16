/* mesytec-mvlc - driver library for the Mesytec MVLC VME controller
 *
 * Copyright (c) 2020 mesytec GmbH & Co. KG
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
#ifndef __MESYTEC_MVLC_MVLC_IMPL_UDP_H__
#define __MESYTEC_MVLC_MVLC_IMPL_UDP_H__

#ifndef __WIN32
#include <netinet/ip.h> // sockaddr_in
#else
#include <winsock2.h>
#endif

#include <array>
#include <fstream>
#include <string>
#include <thread>

#include "mesytec-mvlc_export.h"
#include "mvlc_basic_interface.h"
#include "mvlc_constants.h"
#include "mvlc_counters.h"
#include "mvlc_eth_interface.h"
#include "util/protected.h"
#include "util/ticketmutex.h"

namespace mesytec
{
namespace mvlc
{
namespace eth
{

struct EthThrottleContext
{
#ifndef __WIN32
    u32 dataSocketInode; // Inode of the socket used for MVLCs data pipe. This is needed
                         // to correctly identify the socket in the netlink response data.
#else
    int dataSocket = -1; // File descriptor of the data pipe socket.
    int dataSocketReceiveBufferSize; // Size in bytes of the receive buffer in the OS.
#endif

    int delaySocket = -1; // The socket used for sending delay commands to the MVLC.

    std::chrono::milliseconds queryDelay = std::chrono::milliseconds(10); // Amount of time to sleep after each cycle.

    float threshold = 0.5; // Throttling begins when buffer fill level / buffer capacity is above this level.
    float range = 0.45; // The buffer fill level range until max throttle is reached.
                        // Throttling starts at threshold and reaches its max value at
                        // threshold+range.
    bool quit = false; // Set to true to make the throttler thread quit

    std::ofstream debugOut; // Will receive throttling debug output if open.
};

class MESYTEC_MVLC_EXPORT Impl: public MVLCBasicInterface, public MVLC_ETH_Interface
{
    public:
        explicit Impl(const std::string &host);
        ~Impl() override;

        std::error_code connect() override;
        std::error_code disconnect() override;
        bool isConnected() const override;

        std::error_code setWriteTimeout(Pipe pipe, unsigned ms) override;
        std::error_code setReadTimeout(Pipe pipe, unsigned ms) override;

        unsigned writeTimeout(Pipe pipe) const override;
        unsigned readTimeout(Pipe pipe) const override;

        std::error_code write(Pipe pipe, const u8 *buffer, size_t size,
                              size_t &bytesTransferred) override;

        std::error_code read(Pipe pipe, u8 *buffer, size_t size,
                             size_t &bytesTransferred) override;

        PacketReadResult read_packet(Pipe pipe, u8 *buffer, size_t size) override;

        ConnectionType connectionType() const override { return ConnectionType::ETH; }
        std::string connectionInfo() const override;

        std::array<PipeStats, PipeCount> getPipeStats() const override;
        std::array<PacketChannelStats, NumPacketChannels> getPacketChannelStats() const override;
        void resetPipeAndChannelStats() override;

        // These methods return the remote IPv4 address used for the command
        // and data sockets respectively. This is the address resolved from the
        // host string given to the constructor.
        u32 getCmdAddress() const;
        u32 getDataAddress() const;

        // Returns the host/IP string given to the constructor.
        std::string getHost() const { return m_host; }

        sockaddr_in getCmdSockAddress() const { return m_cmdAddr; }
        sockaddr_in getDataSockAddress() const { return m_dataAddr; }
        sockaddr_in getDelaySockAddress() const { return m_delayAddr; }

        void setDisableTriggersOnConnect(bool b) override
        {
            m_disableTriggersOnConnect = b;
        }

        bool disableTriggersOnConnect() const override
        {
            return m_disableTriggersOnConnect;
        }

        std::error_code enableJumboFrames(bool b) override;
        std::pair<bool, std::error_code> jumboFramesEnabled() override;

        u32 getDataSocketReceiveBufferSize() const override { return m_dataSocketReceiveBufferSize; }
        std::error_code sendDelayCommand(u16 delay_us) override;
        EthThrottleCounters getThrottleCounters() const override;

    private:
        int getSocket(Pipe pipe) { return pipe == Pipe::Command ? m_cmdSock : m_dataSock; }

        std::string m_host;
        int m_cmdSock = -1;
        int m_dataSock = -1;
        int m_delaySock = -1;
        struct sockaddr_in m_cmdAddr = {};
        struct sockaddr_in m_dataAddr = {};
        struct sockaddr_in m_delayAddr = {};

        std::array<unsigned, PipeCount> m_writeTimeouts = {
            DefaultWriteTimeout_ms, DefaultWriteTimeout_ms
        };

        std::array<unsigned, PipeCount> m_readTimeouts = {
            DefaultReadTimeout_ms, DefaultReadTimeout_ms
        };

        // Used internally for buffering in read()
        struct ReceiveBuffer
        {
            std::array<u8, JumboFrameMaxSize> buffer;
            u8 *start = nullptr; // start of unconsumed payload data
            u8 *end = nullptr; // end of packet data

            u32 header0() { return reinterpret_cast<u32 *>(buffer.data())[0]; }
            u32 header1() { return reinterpret_cast<u32 *>(buffer.data())[1]; }

            // number of bytes available
            size_t available() { return end - start; }
            void reset() { start = end = nullptr; }
        };

        std::array<ReceiveBuffer, PipeCount> m_receiveBuffers;
        std::array<PipeStats, PipeCount> m_pipeStats;
        std::array<PacketChannelStats, NumPacketChannels> m_packetChannelStats;
        std::array<s32, NumPacketChannels> m_lastPacketNumbers;
        bool m_disableTriggersOnConnect = true;
        mutable TicketMutex m_statsMutex;
        int m_dataSocketReceiveBufferSize = 0;
        mutable Protected<EthThrottleCounters> m_throttleCounters;
        Protected<EthThrottleContext> m_throttleContext;
        std::thread m_throttleThread;
};

// Given the previous and current packet numbers returns the number of lost
// packets in-between, taking overflow into account.
s32 calc_packet_loss(u16 lastPacketNumber, u16 packetNumber);

} // end namespace eth
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_IMPL_UDP_H__ */
