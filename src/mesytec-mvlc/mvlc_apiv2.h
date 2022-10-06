#ifndef __MESYTEC_MVLC_APIV2_H__
#define __MESYTEC_MVLC_APIV2_H__

#include <atomic>
#include <deque>
#include <future>
#include <memory>

#include "mesytec-mvlc/mesytec-mvlc_export.h"

#include "mvlc_basic_interface.h"
#include "mvlc_command_builders.h"
#include "mvlc_stack_errors.h"
#include "mvlc_threading.h"
#include "util/protected.h"

namespace mesytec
{
namespace mvlc
{
namespace apiv2
{

struct MESYTEC_MVLC_EXPORT CmdPipeCounters
{
    size_t reads;
    size_t bytesRead;
    size_t timeouts;
    size_t invalidHeaders;
    size_t wordsSkipped;
    size_t errorBuffers;
    size_t superBuffers;
    size_t stackBuffers;
    size_t dsoBuffers;

    size_t shortSuperBuffers;
    size_t superFormatErrors;
    size_t superRefMismatches;
    size_t stackRefMismatches;
};

class MESYTEC_MVLC_EXPORT MVLC
{
    public:
        // Warning: the default constructor creates a MVLC object which is in
        // an invalid state. Calling most methods will result in a crash
        // because the internal Private pointer is set to nullptr.
        // The constructor was added to allow creating an uninitialized MVLC
        // object and later on copy/move a properly constructed MVLC object
        // into it.
        explicit MVLC();

        MVLC(std::unique_ptr<MVLCBasicInterface> &&impl);
        ~MVLC();

        MVLC(const MVLC &) = default;
        MVLC &operator=(const MVLC &) = default;

        MVLC(MVLC &&) = default;
        MVLC &operator=(MVLC &&) = default;

        explicit operator bool() const { return d != nullptr; }
        bool isValid() const { return static_cast<bool>(*this); }

        // Contents of the hardware_id register (0x6008)
        u32 hardwareId() const;
        // Contents of the firmware_revision register (0x600e)
        u32 firmwareRevision() const;

        // connection related
        std::error_code connect();
        std::error_code disconnect();
        bool isConnected() const;
        ConnectionType connectionType() const;
        std::string connectionInfo() const;
        void setDisableTriggersOnConnect(bool b);
        bool disableTriggersOnConnect() const;

        // register and vme api
        std::error_code readRegister(u16 address, u32 &value);
        std::error_code writeRegister(u16 address, u32 value);

        std::error_code vmeRead(u32 address, u32 &value, u8 amod, VMEDataWidth dataWidth);
        std::error_code vmeWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth);

        std::error_code vmeBlockRead(u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest); // BLT, MBLT
        std::error_code vmeBlockRead(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest); // 2eSST

        std::error_code vmeBlockReadSwapped(u32 address, u16 maxTransfers, std::vector<u32> &dest); // MBLT
        std::error_code vmeBlockReadSwapped(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest); // 2eSST

        // stack uploading

        std::error_code uploadStack(
            u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<u32> &stackContents);

        std::error_code uploadStack(
            u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<StackCommand> &commands);

        inline std::error_code uploadStack(
            u8 stackOutputPipe, u16 stackMemoryOffset, const StackCommandBuilder &stack)
        {
            return uploadStack(stackOutputPipe, stackMemoryOffset, stack.getCommands());
        }

        CmdPipeCounters getCmdPipeCounters() const;

        // Access to accumulated stack error notification data
        StackErrorCounters getStackErrorCounters() const;
        void resetStackErrorCounters();

        // Low level implementation and per-pipe lock access.
        MVLCBasicInterface *getImpl();
        Locks &getLocks();

        // Lower level super and stack transactions. Note: both the super and
        // stack command builders have to start with a reference or marker
        // command respectively.
        std::error_code superTransaction(const SuperCommandBuilder &superBuilder, std::vector<u32> &dest);
        std::error_code stackTransaction(const StackCommandBuilder &stackBuilder, std::vector<u32> &dest);

        // Eth specific
        std::error_code enableJumboFrames(bool b);
        std::pair<bool, std::error_code> jumboFramesEnabled();

        // Experimental: interact with the low level future/promise/cmd_pipe_reader system.
        //std::future<std::error_code> setPendingStackResponse(std::vector<u32> &dest, u32 stackRef);

    private:
        struct Private;
        std::shared_ptr<Private> d;
};

} // end namespace apiv2
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_APIV2_H__ */
