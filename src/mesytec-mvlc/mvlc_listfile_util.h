#ifndef __MESYTEC_MVLC_MVLC_LISTFILE_UTIL_H__
#define __MESYTEC_MVLC_MVLC_LISTFILE_UTIL_H__

#include <cstring>

#include "mvlc_listfile.h"
#include "readout_buffer.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

// Implements the listfile::WriteHandle interface. Writes are directed to the
// underlying ReadoutBuffer. The buffer is resized in write() if there's not
// enough room available.
class MESYTEC_MVLC_EXPORT BufferWriteHandle: public WriteHandle
{
    public:
        explicit BufferWriteHandle(ReadoutBuffer &buffer)
            : m_buffer(buffer)
        {
        }

        ~BufferWriteHandle() override {}

        size_t write(const u8 *data, size_t size) override
        {
            m_buffer.ensureFreeSpace(size);
            assert(m_buffer.free() >= size);
            std::memcpy(m_buffer.data() + m_buffer.used(),
                        data, size);
            m_buffer.use(size);
            return size;
        }

    private:
        ReadoutBuffer &m_buffer;
};

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_LISTFILE_UTIL_H__ */
