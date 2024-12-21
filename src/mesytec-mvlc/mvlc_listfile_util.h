#ifndef __MESYTEC_MVLC_MVLC_LISTFILE_UTIL_H__
#define __MESYTEC_MVLC_MVLC_LISTFILE_UTIL_H__

#include <array>
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
    ReadoutBuffer destBuf_ = ReadoutBuffer{1u << 20};
    std::vector<u8> tmpBuf_;
    ReadHandle *readHandle = nullptr;
    Preamble preamble;
    ConnectionType bufferFormat;
    size_t totalBytesRead = 0;

    ReadoutBuffer &destBuf() { return destBuf_; };
    std::vector<u8> &tempBuf() { return tmpBuf_; }
};

inline ListfileReaderHelper make_listfile_reader_helper(ReadHandle *readHandle)
{
    ListfileReaderHelper result;
    result.readHandle = readHandle;
    result.preamble = read_preamble(*readHandle);
    result.bufferFormat = (result.preamble.magic == get_filemagic_usb()
        ? ConnectionType::USB
        : ConnectionType::ETH);
    result.destBuf().setType(result.bufferFormat);
    result.tempBuf().reserve(result.destBuf().capacity());
    result.totalBytesRead = get_filemagic_len();
    return result;
}

inline const ReadoutBuffer *read_next_buffer(ListfileReaderHelper &rh)
{
    // If data has already been read the current tempBuf mayb contain temporary
    // data => swap dest and temp buffers, clear the new temp buffer and read
    // into the new dest buffer taking into account that it may already contain
    // data.

    auto &destBuf = rh.destBuf();
    auto &tempBuf = rh.tempBuf();
    destBuf.ensureFreeSpace(tempBuf.size());

    std::copy(std::begin(tempBuf), std::end(tempBuf), destBuf.data() + destBuf.used());
    destBuf.use(tempBuf.size());
    tempBuf.clear();

    try
    {
        size_t bytesRead = rh.readHandle->read(destBuf.data() + destBuf.used(),
            destBuf.free());
        destBuf.use(bytesRead);
        rh.totalBytesRead += bytesRead;

        // Ensures that destBuf contains only complete frames/packets. Can move
        // trailing data from destBuf into tempBuf.
        size_t bytesMoved = mvlc::fixup_buffer(rh.bufferFormat, destBuf.data(), destBuf.used(), tempBuf);
        destBuf.setUsed(destBuf.used() - bytesMoved);
    }
    catch (const std::exception &e)
    {
    }

    return &destBuf;
}

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_LISTFILE_UTIL_H__ */
