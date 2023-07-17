#ifndef __MESYTEC_MVLC_MVLC_REPLAY_WORKER_H__
#define __MESYTEC_MVLC_MVLC_REPLAY_WORKER_H__

#include <future>

#include "mesytec-mvlc/mesytec-mvlc_export.h"

#include "mesytec-mvlc/mvlc_listfile.h"
#include "mesytec-mvlc/mvlc_util.h"
#include "mesytec-mvlc/readout_buffer_queues.h"
#include "mesytec-mvlc/util/protected.h"

namespace mesytec::mvlc
{

class MESYTEC_MVLC_EXPORT ReplayWorker
{
    public:
        enum class State { Idle, Starting, Running, Paused, Stopping };

        struct Counters
        {
            State state;
            std::chrono::time_point<std::chrono::steady_clock> tStart;
            std::chrono::time_point<std::chrono::steady_clock> tEnd;
            size_t buffersRead;
            size_t buffersFlushed;
            size_t bytesRead;

            std::error_code ec;
            std::exception_ptr eptr;
        };

        ReplayWorker(
            ReadoutBufferQueues &snoopQueues,
            listfile::ReadHandle *lfh);
        ~ReplayWorker();

        State state() const;
        WaitableProtected<State> &waitableState();
        Counters counters();
        std::future<std::error_code> start();
        std::error_code stop();
        std::error_code pause();
        std::error_code resume();
        ReadoutBufferQueues *snoopQueues();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

enum class ReplayWorkerError
{
    NoError,
    ReplayNotIdle,
    ReplayNotRunning,
    ReplayNotPaused,
    UnknownListfileFormat,
};

std::error_code MESYTEC_MVLC_EXPORT make_error_code(ReplayWorkerError error);

} // end namespace mesytec::mvlc

namespace std
{
    template<> struct is_error_code_enum<mesytec::mvlc::ReplayWorkerError>: true_type {};
}

#endif /* __MESYTEC_MVLC_MVLC_REPLAY_WORKER_H__ */
