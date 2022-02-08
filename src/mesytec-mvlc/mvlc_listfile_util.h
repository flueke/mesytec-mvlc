#ifndef __MESYTEC_MVLC_MVLC_LISTFILE_UTIL_H__
#define __MESYTEC_MVLC_MVLC_LISTFILE_UTIL_H__

#include <cstring>
#include <ostream>

#include "mvlc_listfile.h"
#include "readout_buffer.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

// BufferedWriteHandle: writes are directed to an underlying std::vector<u8> buffer.
class MESYTEC_MVLC_EXPORT BufferedWriteHandle: public WriteHandle
{
    public:
        size_t write(const u8 *data, size_t size) override
        {
            std::copy(data, data+size, std::back_inserter(buffer_));
            return size;
        }

        const std::vector<u8> &getBuffer() const { return buffer_; }
        std::vector<u8> getBuffer() { return buffer_; }

    private:
        std::vector<u8> buffer_;
};

// Implements the listfile::WriteHandle interface. Writes are directed to the
// underlying ReadoutBuffer. The buffer is resized in write() if there's not
// enough room available.
class MESYTEC_MVLC_EXPORT ReadoutBufferWriteHandle: public WriteHandle
{
    public:
        explicit ReadoutBufferWriteHandle(ReadoutBuffer &buffer)
            : m_buffer(buffer)
        {
        }

        ~ReadoutBufferWriteHandle() override {}

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

// WriteHandle working on a std::ostream.
struct OStreamWriteHandle: public mvlc::listfile::WriteHandle
{
    OStreamWriteHandle(std::ostream &out_)
        : out(out_)
    { }

    size_t write(const u8 *data, size_t size) override
    {
        out.write(reinterpret_cast<const char *>(data), size);
        return size;
    }

    std::ostream &out;
};

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_LISTFILE_UTIL_H__ */
