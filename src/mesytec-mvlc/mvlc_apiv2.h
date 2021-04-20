#ifndef __MESYTEC_MVLC_APIV2_H__
#define __MESYTEC_MVLC_APIV2_H__

#include <atomic>
#include <deque>
#include <memory>
#include <spdlog/spdlog.h>

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

std::shared_ptr<spdlog::logger> setup_logger(std::vector<spdlog::sink_ptr> sinks = {});

struct CmdPipeCounters
{
    std::atomic<size_t> reads;
    std::atomic<size_t> bytesRead;
    std::atomic<size_t> timeouts;
    std::atomic<size_t> invalidHeaders;
    std::atomic<size_t> wordsSkipped;
    std::atomic<size_t> errorBuffers;
    std::atomic<size_t> superBuffers;
    std::atomic<size_t> stackBuffers;
    std::atomic<size_t> dsoBuffers;

    std::atomic<size_t> shortSuperBuffers;
    std::atomic<size_t> superFormatErrors;
    std::atomic<size_t> superRefMismatches;
    std::atomic<size_t> stackRefMismatches;

    CmdPipeCounters()
        : reads(0)
        , bytesRead(0)
        , timeouts(0)
        , invalidHeaders(0)
        , wordsSkipped(0)
        , errorBuffers(0)
        , superBuffers(0)
        , stackBuffers(0)
        , dsoBuffers(0)
        , shortSuperBuffers(0)
        , superFormatErrors(0)
        , superRefMismatches(0)
        , stackRefMismatches(0)
    {}
};

class MVLC
{
    public:
        MVLC(std::unique_ptr<MVLCBasicInterface> &&impl);
        ~MVLC();

        MVLC(const MVLC &) = default;
        MVLC &operator=(const MVLC &) = default;

        MVLC(MVLC &&) = default;
        MVLC &operator=(MVLC &&) = default;

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

        std::error_code vmeBlockRead(u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest);
        std::error_code vmeMBLTSwapped(u32 address, u16 maxTransfers, std::vector<u32> &dest);

        // stack uploading
        std::error_code uploadStack(
            u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<StackCommand> &commands);

        inline std::error_code uploadStack(
            u8 stackOutputPipe, u16 stackMemoryOffset, const StackCommandBuilder &stack)
        {
            return uploadStack(stackOutputPipe, stackMemoryOffset, stack.getCommands());
        }

        const CmdPipeCounters &getCmdPipeCounters() const;

        // access to push data: stack error counters and dso buffers
        StackErrorCounters getStackErrorCounters() const;
        void resetStackErrorCounters();
        WaitableProtected<std::deque<std::vector<u32>>> &getDSOBuffers() const;
        //std::deque<std::vector<u32>> getDsoBuffers() const;
        //std::vector<u32> getLatestDsoBuffer() const;

        // Low level implementation and per-pipe lock access.
        MVLCBasicInterface *getImpl();
        Locks &getLocks();

    private:
        struct Private;
        std::shared_ptr<Private> d;
};

} // end namespace apiv2
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_APIV2_H__ */
