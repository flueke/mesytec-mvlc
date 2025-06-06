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
#ifndef __MESYTEC_MVLC_MVLC_IMPL_UDP_H__
#define __MESYTEC_MVLC_MVLC_IMPL_UDP_H__

#include <array>
#include <fstream>
#include <string>
#include <thread>

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mesytec-mvlc/mvlc_basic_interface.h"
#include "mesytec-mvlc/mvlc_constants.h"
#include "mesytec-mvlc/mvlc_counters.h"
#include "mesytec-mvlc/mvlc_eth_interface.h"
#include "mesytec-mvlc/util/protected.h"
#include "mesytec-mvlc/util/ticketmutex.h"

namespace mesytec::mvlc::eth
{

struct EthThrottleContext
{
#ifndef MESYTEC_MVLC_PLATFORM_WINDOWS
    u32 dataSocketInode; // Inode of the socket used for MVLCs data pipe. This is needed
                         // to correctly identify the socket in the netlink response data.
#else
    int dataSocket = -1; // File descriptor of the data pipe socket.
    int dataSocketReceiveBufferSize; // Size in bytes of the receive buffer in the OS.
#endif

    int delaySocket = -1; // The socket used for sending delay commands to the MVLC.

    // Amount of time to sleep after each cycle. This directly affects the number of
    // measurements taken and the number of delay packets sent out per second!
    std::chrono::milliseconds queryDelay = std::chrono::milliseconds(1);

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
        std::string getHost() const;
        std::string getRemoteAddress() const { return getHost(); }

        #if 0
        sockaddr_in getCmdSockAddress() const { return m_cmdAddr; }
        sockaddr_in getDataSockAddress() const { return m_dataAddr; }
        sockaddr_in getDelaySockAddress() const { return m_delayAddr; }
        #endif

        void setDisableTriggersOnConnect(bool b) override;
        bool disableTriggersOnConnect() const override;

        EthThrottleCounters getThrottleCounters() const override;

        int getSocket(Pipe pipe);

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

// Given the previous and current packet numbers returns the number of lost
// packets in-between, taking overflow into account.
s32 MESYTEC_MVLC_EXPORT calc_packet_loss(u16 lastPacketNumber, u16 packetNumber);

// Sends an EthDelay (0x0207) command packet through the given socket.
// No response data; the MVLC delay port is write only.
std::error_code MESYTEC_MVLC_EXPORT send_delay_command(int delaySock, u16 delay_us);

}

#endif /* __MESYTEC_MVLC_MVLC_IMPL_UDP_H__ */
