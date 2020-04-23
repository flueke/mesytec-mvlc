#ifndef __MESYTEC_MVLC_MVLC_READOUT_H__
#define __MESYTEC_MVLC_MVLC_READOUT_H__

#include "mesytec-mvlc_export.h"

#include "mvlc.h"
#include "mvlc_listfile.h"
#include "mvlc_readout_config.h"
#include "mvlc_stack_executor.h"
#include "util/readout_buffer_queues.h"

namespace mesytec
{
namespace mvlc
{

#if 0
enum class MESYTEC_MVLC_EXPORT ReadoutWorkerError
{
    NoError,
    ReadoutRunning,
    ReadoutNotRunning,
    ReadoutPaused,
    ReadoutNotPaused,
};

MESYTEC_MVLC_EXPORT std::error_code make_error_code(ReadoutWorkerError error);

class MESYTEC_MVLC_EXPORT ReadoutWorker
{
    public:
        enum class State { Idle, Starting, Running, Paused, Stopping };

        struct Counters
        {
            std::chrono::time_point<std::chrono::steady_clock> tStart;
            std::chrono::time_point<std::chrono::steady_clock> tEnd;
            size_t buffersRead;
            size_t bytesRead;
            size_t framingErrors;
            size_t unusedBytes;
            size_t readTimeouts;
            std::array<size_t, stacks::StackCount> stackHits;
        };

        ReadoutWorker(MVLC &mvlc, const std::vector<StackTrigger> &stackTriggers);

        State state() const;
        std::error_code start(const std::chrono::seconds &duration = {});
        std::error_code stop();
        std::error_code pause();
        std::error_code resume();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};
#endif

MVLC MESYTEC_MVLC_EXPORT make_mvlc(const CrateConfig &crateConfig);

struct ReadoutInitResults
{
    std::error_code ec;
    GroupedStackResults init;
    GroupedStackResults triggerIO;
};

ReadoutInitResults MESYTEC_MVLC_EXPORT init_readout(
    MVLC &mvlc, const CrateConfig &crateConfig,
    const CommandExecOptions stackExecOptions = {});

struct ListfileBufferWriterState
{
    using Clock = std::chrono::steady_clock;

    enum State { Idle, Running };

    State state = Idle;
    Clock::time_point tStart;
    Clock::time_point tEnd;
    size_t writes;
    size_t bytesWritten;
    std::exception_ptr exception;
};

// Usage:
// auto writerThread = std::thread(listfile_buffer_writer, lfh,
//                                 std::ref(bufferQueues), std::ref(writerState), std::ref(stateMutex));

void MESYTEC_MVLC_EXPORT listfile_buffer_writer(
    listfile::WriteHandle *lfh,
    ReadoutBufferQueues &bufferQueues,
    ListfileBufferWriterState &state,
    TicketMutex &stateMutex);

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_READOUT_H__ */
