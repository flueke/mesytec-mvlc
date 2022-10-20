#ifndef __MESYTEC_MVLC_UTIL_READOUT_BUFFER_QUEUES_H__
#define __MESYTEC_MVLC_UTIL_READOUT_BUFFER_QUEUES_H__

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mesytec-mvlc/readout_buffer.h"
#include "mesytec-mvlc/util/storage_sizes.h"
#include "mesytec-mvlc/util/threadsafequeue.h"

namespace mesytec
{
namespace mvlc
{

class MESYTEC_MVLC_EXPORT ReadoutBufferQueues
{
    public:
        using QueueType = ThreadSafeQueue<ReadoutBuffer *>;

        explicit ReadoutBufferQueues(size_t bufferCapacity = util::Megabytes(1), size_t bufferCount = 10);

        QueueType &filledBufferQueue() { return m_filledBuffers; }
        QueueType &emptyBufferQueue() { return m_emptyBuffers; }
        size_t bufferCount() { return m_bufferStorage.size(); }

    private:
        QueueType m_filledBuffers;
        QueueType m_emptyBuffers;
        std::vector<ReadoutBuffer> m_bufferStorage;
};

} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_UTIL_READOUT_BUFFER_QUEUES_H__ */
