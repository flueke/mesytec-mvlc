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
#ifndef __MESYTEC_MVLC_MVLC_H__
#define __MESYTEC_MVLC_MVLC_H__

#include <memory>
#include <vector>

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mvlc_basic_interface.h"
#include "mvlc_buffer_validators.h"
#include "mvlc_constants.h"
#include "mvlc_core_interface.h"
#include "mvlc_threading.h"
#include "mvlc_command_builders.h"
#include "mvlc_stack_errors.h"
#include "util/protected.h"

namespace mesytec::mvlc
{

class MESYTEC_MVLC_EXPORT MVLC final: public MvlcCoreInterface
{
    public:
        // Warning: the default constructor creates a MVLC object which is in
        // an invalid state. Calling most methods will result in a crash
        // because the internal Private pointer is set to nullptr.
        // The constructor was added to allow creating an uninitialized MVLC
        // object and later on copy/move a properly constructed MVLC object
        // into it.
        explicit MVLC();
        explicit MVLC(std::unique_ptr<MvlcTransactionInterface> &&impl);
        ~MVLC();

        MVLC(const MVLC &) = default;
        MVLC &operator=(const MVLC &) = default;

        MVLC(MVLC &&) = default;
        MVLC &operator=(MVLC &&) = default;

        // Returns true if an implementation has been set, false otherwise
        // (meaning the MVLC instance is unusable).
        explicit operator bool() const { return d != nullptr; }
        bool isValid() const { return static_cast<bool>(*this); }

        MvlcTransactionInterface *getTransactionImpl() override;

        // Contents of the hardware_id register (0x6008)
        u32 hardwareId() const;
        // Contents of the firmware_revision register (0x600e)
        u32 firmwareRevision() const;

        // Firmware revision dependent number of stacks.
        unsigned getStackCount() const;
        unsigned getReadoutStackCount() const { return getStackCount() - 1; }

        // connection related
        std::error_code connect() override;
        std::error_code disconnect() override;
        bool isConnected() const override;
        ConnectionType connectionType() const override;
        std::string connectionInfo() const override;
        void setDisableTriggersOnConnect(bool b); // TODO: remove this
        bool disableTriggersOnConnect() const; // TODO: remove this

        // register and vme api
        std::error_code readRegister(u16 address, u32 &value) override;
        std::error_code writeRegister(u16 address, u32 value) override;

        std::error_code vmeRead(u32 address, u32 &value, u8 amod, VMEDataWidth dataWidth) override;
        std::error_code vmeWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth) override;

        // BLT, MBLT
        std::error_code vmeBlockRead(u32 address, u8 amod, u16 maxTransfers,
            std::vector<u32> &dest, bool fifo = true) override;

        // 2eSST
        std::error_code vmeBlockRead(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers,
            std::vector<u32> &dest, bool fifo = true) override;

        // Swaps the two 32-bit words for  64-bit reads. amod must be one of the MBLT amods!
        std::error_code vmeBlockReadSwapped(u32 address, u8 amod, u16 maxTransfers,
                                            std::vector<u32> &dest, bool fifo = true) override;
        std::error_code vmeBlockReadSwapped(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers,
                                            std::vector<u32> &dest, bool fifo = true) override;

        // stack uploading
        std::error_code uploadStack(
            u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<u32> &stackContents) override;

        // Low level implementation and per-pipe lock access.
        //MvlcBasicInterface *getImpl();
        Locks &getLocks() override;

        // Lower level super and stack transactions. Note: both the super and
        // stack command builders have to start with a reference or marker
        // command respectively.
        std::error_code superTransaction(const SuperCommandBuilder &superBuilder, std::vector<u32> &dest) override;
        std::error_code stackTransaction(const StackCommandBuilder &stackBuilder, std::vector<u32> &dest) override;
        u16 nextSuperReference() override;
        u32 nextStackReference() override;
        CmdPipeCounters getCmdPipeCounters() const override;
        StackErrorCounters getStackErrorCounters() const override;
        void resetStackErrorCounters() override;

        // Access to accumulated stack error notification data
        //StackErrorCounters getStackErrorCounters() const;
        //void resetStackErrorCounters();


        // Eth specific (TODO: maybe make those free functions, it should only be register read/writes)
        std::error_code enableJumboFrames(bool b);
        std::pair<bool, std::error_code> jumboFramesEnabled();

    private:
        struct Private;
        std::shared_ptr<Private> d;
};

// Sends an empty data frame to the UDP data pipe port to tell the MVLC where to
// send the readout data. Does nothing for MVLC_USB.
std::error_code MESYTEC_MVLC_EXPORT redirect_eth_data_stream(MVLC &theMvlc);

}

#endif /* __MESYTEC_MVLC_MVLC_H__ */
