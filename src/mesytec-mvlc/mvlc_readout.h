#ifndef __MESYTEC_MVLC_MVLC_READOUT_H__
#define __MESYTEC_MVLC_MVLC_READOUT_H__

#include "mesytec-mvlc_export.h"

#include <future>

#include "mvlc.h"
#include "mvlc_impl_eth.h"
#include "mvlc_listfile.h"
#include "mvlc_readout_config.h"
#include "mvlc_stack_errors.h"
#include "mvlc_stack_executor.h"
#include "util/readout_buffer_queues.h"
#include "util/protected.h"

namespace mesytec
{
namespace mvlc
{

MVLC MESYTEC_MVLC_EXPORT make_mvlc(const CrateConfig &crateConfig);

struct MESYTEC_MVLC_EXPORT ReadoutInitResults
{
    std::error_code ec;
    GroupedStackResults init;
    GroupedStackResults triggerIO;
};

ReadoutInitResults MESYTEC_MVLC_EXPORT init_readout(
    MVLC &mvlc, const CrateConfig &crateConfig,
    const CommandExecOptions stackExecOptions = {});

struct ListfileWriterCounters
{
    using Clock = std::chrono::steady_clock;

    enum State { Idle, Running };

    State state = Idle;
    Clock::time_point tStart;
    Clock::time_point tEnd;
    size_t writes;
    size_t bytesWritten;
    std::exception_ptr eptr;
};

// Usage:
// Protected<ListfileWriterCounters> writerState({});
// auto writerThread = std::thread(
//   listfile_buffer_writer, lfh, std::ref(bufferQueues),
//   std::ref(writerState));
// Note: the WriteHandle *lfh may be nullptr in. In this case the writer will
// still dequeue filled buffers from the queue and immediately re-enqueue them
// on the empty queue.

void MESYTEC_MVLC_EXPORT listfile_buffer_writer(
    listfile::WriteHandle *lfh,
    ReadoutBufferQueues &bufferQueues,
    Protected<ListfileWriterCounters> &state);

enum class MESYTEC_MVLC_EXPORT ReadoutWorkerError
{
    NoError,
    ReadoutNotIdle,
    //ReadoutRunning,
    ReadoutNotRunning,
    //ReadoutPaused,
    ReadoutNotPaused,
};

MESYTEC_MVLC_EXPORT std::error_code make_error_code(ReadoutWorkerError error);

class MESYTEC_MVLC_EXPORT ReadoutWorker
{
    public:
        enum class State { Idle, Starting, Running, Paused, Stopping };

        struct Counters
        {
            // Current state of the readout at the time the counters are observed.
            State state;

            // A note about the time points: to get the best measure of the
            // actual readout rate of the DAQ it's best to use the duration of
            // (tTerminateStart - tStart) as that will not include the overhead
            // of the termination procedure.
            // The (tEnd - tStart) time will more closely reflect the real-time
            // spent in the readout loop but it will yield lower data rates
            // because it includes at least one read timeout at the very end of
            // the DAQ run.
            // For long DAQ runs the calculated rates should be almost the
            // same.

            // tStart is recorded right before entering the readout loop
            std::chrono::time_point<std::chrono::steady_clock> tStart;

            // tEnd is recorded at the end of the readout loop but before
            // stopping the error polling thread. This reflects the DAQ time
            // including the termination sequence.
            std::chrono::time_point<std::chrono::steady_clock> tEnd;

            // tTerminateStart is recorded right before the termination sequence is run.
            std::chrono::time_point<std::chrono::steady_clock> tTerminateStart;

            // tTerminateEnd is recorded when the termination sequence finishes.
            std::chrono::time_point<std::chrono::steady_clock> tTerminateEnd;

            // Number of buffers filled with readout data from the controller.
            size_t buffersRead;

            // Number of buffers flushed to the listfile writer. This can be
            // more than buffersRead as periodic software timeticks,
            // pause/resume events and the EndOfFile system event are also
            // written into buffers and handed to the listfile writer.
            size_t buffersFlushed;

            // Total number of bytes read from the controller.
            size_t bytesRead;

            // Number of buffers that could not be added to the snoop queue
            // because no free buffer was available. This is the number of
            // buffers the analysis side did not see.
            size_t snoopMissedBuffers;

            size_t usbFramingErrors;
            size_t usbTempMovedBytes;
            size_t ethShortReads;
            size_t readTimeouts;

            std::array<size_t, stacks::StackCount> stackHits = {};
            std::array<eth::PipeStats, PipeCount> ethStats;
            std::error_code ec;
            std::exception_ptr eptr;
            StackErrorCounters stackErrors;
            ListfileWriterCounters listfileWriterCounters = {};
        };

        ReadoutWorker(
            MVLC &mvlc,
            const std::vector<u32> &stackTriggers,
            ReadoutBufferQueues &snoopQueues,
            listfile::WriteHandle *lfh
            );

        ~ReadoutWorker();

        State state() const;
        WaitableProtected<State> &waitableState();
        Counters counters();
        std::future<std::error_code> start(const std::chrono::seconds &timeToRun = {});
        std::error_code stop();
        std::error_code pause();
        std::error_code resume();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_READOUT_H__ */
