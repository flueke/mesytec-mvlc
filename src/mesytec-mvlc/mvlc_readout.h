#ifndef __MESYTEC_MVLC_MVLC_READOUT_H__
#define __MESYTEC_MVLC_MVLC_READOUT_H__

#include <future>

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mesytec-mvlc/mvlc_eth_interface.h"
#include "mesytec-mvlc/mvlc.h"
#include "mesytec-mvlc/mvlc_impl_eth.h"
#include "mesytec-mvlc/mvlc_listfile.h"
#include "mesytec-mvlc/mvlc_readout_config.h"
#include "mesytec-mvlc/mvlc_stack_errors.h"
#include "mesytec-mvlc/mvlc_stack_executor.h"
#include "mesytec-mvlc/readout_buffer_queues.h"
#include "mesytec-mvlc/util/protected.h"

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

struct MESYTEC_MVLC_EXPORT ListfileWriterCounters
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

enum class ReadoutWorkerError
{
    NoError,
    ReadoutNotIdle,
    ReadoutNotRunning,
    ReadoutNotPaused,
};

std::error_code MESYTEC_MVLC_EXPORT make_error_code(ReadoutWorkerError error);

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

            // Number of times we did not land on an expected frame header
            // while following the framing structure. To recover from this case
            // the readotu data is searched for a new frame header.
            size_t usbFramingErrors;

            // Number of bytes that where moved into temporary storage so that
            // the current USB readout buffer only contains full frames.
            size_t usbTempMovedBytes;

            // Number of packets received that where shorter than
            // eth::HeaderBytes.
            size_t ethShortReads;

            // Number of usb/socket reads that timed out. Note that the DAQ
            // shutdown procedure will always run into at least one timeout
            // while reading buffered datat from the MVLC.
            size_t readTimeouts;

            std::array<size_t, stacks::StackCount> stackHits = {};
            std::array<eth::PipeStats, PipeCount> ethStats;
            std::error_code ec;
            std::exception_ptr eptr;
            StackErrorCounters stackErrors;
            ListfileWriterCounters listfileWriterCounters = {};
        };

        ReadoutWorker(
            MVLC mvlc,
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

using StackHits = std::array<size_t, stacks::StackCount>;

// Follows the outer stack framing starting at
// PacketReadResult::nextHeaderPointer(). For each header extracts the stack id
// and increments the corresponding entry in the stackHits array.
//
// Returns true if the framing structure is intact and the packet could thus be
// parsed to the end.
MESYTEC_MVLC_EXPORT bool count_stack_hits(const eth::PacketReadResult &prr, StackHits &stackHits);

MESYTEC_MVLC_EXPORT const char *readout_worker_state_to_string(const ReadoutWorker::State &state);

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_READOUT_H__ */
