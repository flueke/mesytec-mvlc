#ifndef __MESYTEC_MVLC_MVLC_LISTFILE_UTIL_H__
#define __MESYTEC_MVLC_MVLC_LISTFILE_UTIL_H__

#include <cstring>
#include <ostream>

#include "mvlc_listfile.h"
#include "readout_buffer.h"
#include "mvlc_replay_worker.h"

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

struct ListfileReaderHelper
{
    // Two buffers, one to read data into, one to store temporary data after
    // fixing up the destination buffer.
    std::array<mvlc::ReadoutBuffer, 2> buffers =
    {
        ReadoutBuffer(1u << 20),
        ReadoutBuffer(1u << 20),
    };

    ReadHandle *readHandle = nullptr;
    Preamble preamble;
    ConnectionType bufferFormat;
    size_t totalBytesRead = 0;

    unsigned destBufIndex = 0;
    unsigned tempBufIndex = 1;

    ReadoutBuffer *destBuf() { return &buffers[destBufIndex]; };
    ReadoutBuffer *tempBuf() { return &buffers[tempBufIndex]; }
};

inline ListfileReaderHelper make_listfile_reader_helper(ReadHandle *readHandle)
{
    ListfileReaderHelper result;
    result.readHandle = readHandle;
    result.preamble = read_preamble(*readHandle);
    result.bufferFormat = (result.preamble.magic == get_filemagic_usb()
        ? ConnectionType::USB
        : ConnectionType::ETH);
    result.destBuf()->setType(result.bufferFormat);
    result.tempBuf()->setType(result.bufferFormat);
    readHandle->seek(get_filemagic_len());
    result.totalBytesRead = get_filemagic_len();
    return result;
}

inline const ReadoutBuffer *read_next_buffer(ListfileReaderHelper &rh)
{
    // If data has already been read the current tempBuf mayb contain temporary
    // data => swap dest and temp buffers, clear the new temp buffer and read
    // into the new dest buffer taking into account that it may already contain
    // data.
    std::swap(rh.destBufIndex, rh.tempBufIndex);
    rh.tempBuf()->clear();

    size_t bytesRead = rh.readHandle->read(rh.destBuf()->data() + rh.destBuf()->used(), rh.destBuf()->free());
    rh.destBuf()->use(bytesRead);
    rh.totalBytesRead += bytesRead;

    // Ensures that destBuf contains only complete frames/packets. Can move
    // trailing data from destBuf into tempBuf.
    mvlc::fixup_buffer(rh.bufferFormat, *rh.destBuf(), *rh.tempBuf());

    return rh.destBuf();
}

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_LISTFILE_UTIL_H__ */
