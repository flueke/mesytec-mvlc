#include "mvlc_listfile_zip.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <sys/stat.h>

#include <lz4frame.h>
#include <mz.h>
#include <mz_os.h>
#include <mz_strm.h>
#include <mz_strm_buf.h>
#include <mz_strm_os.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

#include <boost/scope_exit.hpp>

#include "util/algo.h"
#include "util/filesystem.h"
#include "util/fmt.h"
#include "util/logging.h"
#include "util/overloaded.hpp"
#include "util/storage_sizes.h"
#include "util/string_util.h"
#include "util/string_view.hpp"

using namespace nonstd;
using std::cout;
using std::endl;
namespace fs = std::filesystem;

namespace mesytec::mvlc::listfile
{

//
// ZipCreator
//

ZipEntryWriteHandle::ZipEntryWriteHandle(ZipCreator *creator)
    : m_zipCreator(creator)
{
}

ZipEntryWriteHandle::~ZipEntryWriteHandle() {}

size_t ZipEntryWriteHandle::write(const u8 *data, size_t size)
{
    return m_zipCreator->writeToCurrentEntry(data, size);
}

struct ZipCreator::Private
{
    struct LZ4WriteContext
    {
        struct CompressResult
        {
            int error;
            unsigned long long size_in;
            unsigned long long size_out;
        };

        static constexpr size_t ChunkSize = util::Megabytes(1);

        LZ4F_preferences_t lz4Prefs = {
            {LZ4F_max4MB, LZ4F_blockLinked, LZ4F_noContentChecksum, LZ4F_frame,
             0 /* unknown content size */, 0 /* no dictID */, LZ4F_noBlockChecksum},
            0,         /* compression level; 0 == default */
            0,         /* autoflush */
            0,         /* favor decompression speed */
            {0, 0, 0}, /* reserved, must be set to 0 */
        };

        LZ4F_compressionContext_t ctx = {};
        std::vector<u8> buffer;

        void begin(int compressLevel)
        {
            if (auto err = LZ4F_createCompressionContext(&ctx, LZ4F_VERSION))
                throw std::runtime_error("LZ4F_createCompressionContext: " + std::to_string(err));

            lz4Prefs.compressionLevel = compressLevel;
            size_t bufferSize = LZ4F_compressBound(ChunkSize, &lz4Prefs);
            buffer = std::vector<u8>(bufferSize);
        }

        void end() { LZ4F_freeCompressionContext(ctx); /* supports free on NULL */ }
    };

    explicit Private()
    {
        mz_osStream = mz_stream_os_create();
        mz_bufStream = mz_stream_buffered_create();
        mz_stream_set_base(mz_bufStream, mz_osStream);
        mz_stream_buffered_open(mz_bufStream, NULL, MZ_OPEN_MODE_WRITE);

        mz_zipWriter = mz_zip_writer_create();
        mz_zip_writer_set_follow_links(mz_zipWriter, true);
    }

    ~Private()
    {
        mz_zip_writer_delete(&mz_zipWriter);
        mz_stream_delete(&mz_bufStream);
        mz_stream_delete(&mz_osStream);

        mz_zipWriter = nullptr;
        mz_bufStream = nullptr;
        mz_osStream = nullptr;
    }

    size_t writeToCurrentZIPEntry(const u8 *data, size_t size)
    {
        s32 bytesWritten = mz_zip_writer_entry_write(mz_zipWriter, data, size);

        if (bytesWritten < 0)
            throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));

        assert(static_cast<size_t>(bytesWritten) == size);

        return static_cast<size_t>(bytesWritten);
    }

    void *mz_zipWriter = nullptr;
    void *mz_bufStream = nullptr;
    void *mz_osStream = nullptr;

    ZipEntryInfo entryInfo;
    LZ4WriteContext lz4Ctx;
    std::string archiveName;
};

ZipCreator::ZipCreator()
    : d(std::make_unique<Private>())
{
}

ZipCreator::~ZipCreator()
{
    try
    {
        closeCurrentEntry();
    }
    catch (...)
    {
    }

    try
    {
        closeArchive();
    }
    catch (...)
    {
    }
}

void ZipCreator::createArchive(const std::string &zipFilename, const OverwriteMode &mode)
{
    if (isOpen())
        throw std::runtime_error("ZipCreator has an open archive");

    if (mode == OverwriteMode::DontOverwrite && util::file_exists(zipFilename))
        throw std::runtime_error("output archive file exists");

    s32 mzMode = MZ_OPEN_MODE_WRITE | MZ_OPEN_MODE_CREATE;

    if (auto err = mz_stream_open(d->mz_bufStream, zipFilename.c_str(), mzMode))
        throw std::runtime_error("mz_stream_open: " + std::to_string(err));

    if (auto err = mz_zip_writer_open(d->mz_zipWriter, d->mz_bufStream, false))
        throw std::runtime_error("mz_zip_writer_open: " + std::to_string(err));

    d->archiveName = zipFilename;
}

void ZipCreator::closeArchive()
{
    if (auto err = mz_zip_writer_close(d->mz_zipWriter))
        throw std::runtime_error("mz_zip_writer_close: " + std::to_string(err));

    if (auto err = mz_stream_close(d->mz_bufStream))
        throw std::runtime_error("mz_stream_close: " + std::to_string(err));

    d->archiveName = {};
}

bool ZipCreator::isOpen() const { return mz_stream_is_open(d->mz_bufStream) == MZ_OK; }

std::string ZipCreator::archiveName() const { return d->archiveName; }

std::unique_ptr<WriteHandle> ZipCreator::createZIPEntry(const std::string &entryName,
                                                        int compressLevel)
{
    if (hasOpenEntry())
        throw std::runtime_error("ZipCreator has open archive entry");

    mz_zip_file file_info = {};
    file_info.filename = entryName.c_str();
    file_info.modified_date = time(nullptr);
    file_info.version_madeby = MZ_VERSION_MADEBY;
    file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
    file_info.zip64 = MZ_ZIP64_FORCE;
#ifdef MESYTEC_MVLC_PLATFORM_POSIX
    file_info.external_fa = (S_IFREG) | (0644u << 16);
#elif defined MESYTEC_MVLC_PLATFORM_WINDOWS
    file_info.external_fa = (_S_IFREG) | (0644u << 16);
#endif

    mz_zip_writer_set_compress_method(d->mz_zipWriter, MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(d->mz_zipWriter, compressLevel);

    if (auto err = mz_zip_writer_entry_open(d->mz_zipWriter, &file_info))
        throw std::runtime_error("mz_zip_writer_entry_open: " + std::to_string(err));

    d->entryInfo = {};
    d->entryInfo.type = ZipEntryInfo::ZIP;
    d->entryInfo.name = entryName;
    d->entryInfo.isOpen = true;

    // std::make_unique() does not work here because it's not a friend of ZipEntryWriteHandle
    return std::unique_ptr<ZipEntryWriteHandle>(new ZipEntryWriteHandle(this));
}

std::unique_ptr<WriteHandle> ZipCreator::createLZ4Entry(const std::string &entryName_,
                                                        int compressLevel)
{
    if (hasOpenEntry())
        throw std::runtime_error("ZipCreator has open archive entry");

    const std::string entryName = entryName_ + ".lz4";

    mz_zip_file file_info = {};
    file_info.filename = entryName.c_str();
    file_info.modified_date = time(nullptr);
    file_info.version_madeby = MZ_VERSION_MADEBY;
    file_info.compression_method = MZ_COMPRESS_METHOD_STORE;
    file_info.zip64 = MZ_ZIP64_FORCE;
#ifdef MESYTEC_MVLC_PLATFORM_POSIX
    file_info.external_fa = (S_IFREG) | (0644u << 16);
#elif defined MESYTEC_MVLC_PLATFORM_WINDOWS
    file_info.external_fa = (_S_IFREG) | (0644u << 16);
#endif

    mz_zip_writer_set_compress_method(d->mz_zipWriter, MZ_COMPRESS_METHOD_STORE);
    mz_zip_writer_set_compress_level(d->mz_zipWriter, 0);

    if (auto err = mz_zip_writer_entry_open(d->mz_zipWriter, &file_info))
        throw std::runtime_error("mz_zip_writer_entry_open: " + std::to_string(err));

    d->entryInfo = {};
    d->entryInfo.type = ZipEntryInfo::LZ4;
    d->entryInfo.name = entryName;
    d->entryInfo.isOpen = true;

    d->lz4Ctx.begin(compressLevel);

    // write the LZ4 frame header
    size_t lz4BufferBytes = LZ4F_compressBegin(d->lz4Ctx.ctx, d->lz4Ctx.buffer.data(),
                                               d->lz4Ctx.buffer.size(), &d->lz4Ctx.lz4Prefs);

    if (LZ4F_isError(lz4BufferBytes))
        throw std::runtime_error("LZ4F_compressBegin: " + std::to_string(lz4BufferBytes));

    // flush the LZ4 buffer contents to the ZIP
    d->writeToCurrentZIPEntry(d->lz4Ctx.buffer.data(), lz4BufferBytes);

    d->entryInfo.bytesWritten += lz4BufferBytes;
    d->entryInfo.lz4CompressedBytesWritten += lz4BufferBytes;

    // std::make_unique() does not work here because it's not a friend of ZipEntryWriteHandle
    return std::unique_ptr<ZipEntryWriteHandle>(new ZipEntryWriteHandle(this));
}

bool ZipCreator::hasOpenEntry() const { return d->entryInfo.isOpen; }

const ZipEntryInfo &ZipCreator::entryInfo() const { return d->entryInfo; }

size_t ZipCreator::writeToCurrentEntry(const u8 *inputData, size_t inputSize)
{
    if (!hasOpenEntry())
        throw std::runtime_error("ZipCreator has no open archive entry");

    size_t bytesWritten = 0u;

    switch (d->entryInfo.type)
    {
    case ZipEntryInfo::ZIP:
        bytesWritten = d->writeToCurrentZIPEntry(inputData, inputSize);
        d->entryInfo.bytesWritten += bytesWritten;
        break;

    case ZipEntryInfo::LZ4:
        while (bytesWritten < inputSize)
        {
            size_t bytesLeft = inputSize - bytesWritten;
            size_t chunkBytes = std::min(bytesLeft, d->lz4Ctx.buffer.size());

            assert(inputData + bytesWritten + chunkBytes <= inputData + inputSize);
            assert(chunkBytes <= LZ4F_compressBound(d->lz4Ctx.ChunkSize, &d->lz4Ctx.lz4Prefs));

            // compress the chunk into the LZ4WriteContext buffer
            size_t compressedSize =
                LZ4F_compressUpdate(d->lz4Ctx.ctx, d->lz4Ctx.buffer.data(), d->lz4Ctx.buffer.size(),
                                    inputData + bytesWritten, chunkBytes, nullptr);

            if (LZ4F_isError(compressedSize))
                throw std::runtime_error("LZ4F_compressUpdate: " + std::to_string(compressedSize));

            // flush the LZ4 buffer contents to the ZIP
            d->writeToCurrentZIPEntry(d->lz4Ctx.buffer.data(), compressedSize);

            bytesWritten += chunkBytes;
            d->entryInfo.bytesWritten += chunkBytes;
            d->entryInfo.lz4CompressedBytesWritten += compressedSize;
        }
        break;
    };

    return bytesWritten;
}

void ZipCreator::closeCurrentEntry()
{
    if (!hasOpenEntry())
        throw std::runtime_error("ZipCreator has no open archive entry");

    if (d->entryInfo.type == ZipEntryInfo::LZ4)
    {
        // flush whatever remains within internal buffers
        size_t const compressedSize = LZ4F_compressEnd(d->lz4Ctx.ctx, d->lz4Ctx.buffer.data(),
                                                       d->lz4Ctx.buffer.size(), nullptr);

        if (LZ4F_isError(compressedSize))
            throw std::runtime_error("LZ4F_compressEnd: " + std::to_string(compressedSize));

        // flush the LZ4 buffer contents to the ZIP
        size_t bytesWritten = d->writeToCurrentZIPEntry(d->lz4Ctx.buffer.data(), compressedSize);

        d->entryInfo.bytesWritten += bytesWritten;
        d->entryInfo.lz4CompressedBytesWritten += compressedSize;
    }

    if (auto err = mz_zip_writer_entry_close(d->mz_zipWriter))
        throw std::runtime_error("mz_zip_writer_entry_close: " + std::to_string(err));

    d->entryInfo.isOpen = false;
}

void *ZipCreator::getRawWriterHandle() const { return d->mz_zipWriter; }

//
// SplitZipCreator
//

struct SplitZipCreator::Private
{
    SplitZipCreator *q = nullptr;
    ZipCreator zipCreator;
    SplitListfileSetup setup;
    size_t partIndex = 1;
    bool isSplitEntry = false;
    std::chrono::time_point<std::chrono::steady_clock> partCreationTime;

    Private(SplitZipCreator *q_)
        : q(q_)
    {
    }

    void createNextArchive();
};

SplitZipCreator::SplitZipCreator()
    : d(std::make_unique<Private>(this))
{
}

SplitZipCreator::~SplitZipCreator()
{
    if (isOpen())
        closeArchive();
}

void SplitZipCreator::createArchive(const SplitListfileSetup &setup)
{
    if (isOpen())
        throw std::runtime_error("SplitZipCreator has an open archive");

    d->setup = setup;

    if (setup.splitMode == ZipSplitMode::DontSplit)
    {
        auto filename = setup.filenamePrefix + ".zip";
        d->zipCreator.createArchive(filename, setup.overwriteMode);
        if (setup.openArchiveCallback)
            setup.openArchiveCallback(this);
    }
    else
    {
        d->partIndex = 1;
        d->createNextArchive();
    }

    assert(isOpen());
}

void SplitZipCreator::Private::createNextArchive()
{
    assert(!q->isOpen());

    auto filename = fmt::format("{}_part{:03d}.zip", setup.filenamePrefix, partIndex);
    zipCreator.createArchive(filename, setup.overwriteMode);

    assert(q->isOpen());

    if (setup.openArchiveCallback)
        setup.openArchiveCallback(q);
}

void SplitZipCreator::closeArchive()
{
    if (hasOpenEntry())
        closeCurrentEntry();

    if (d->setup.closeArchiveCallback)
        d->setup.closeArchiveCallback(this);

    d->zipCreator.closeArchive();
}

bool SplitZipCreator::isOpen() const { return d->zipCreator.isOpen(); }

std::string SplitZipCreator::archiveName() const { return d->zipCreator.archiveName(); }

std::unique_ptr<WriteHandle> SplitZipCreator::createZIPEntry(const std::string &entryName,
                                                             int compressLevel)
{
    auto ret = d->zipCreator.createZIPEntry(entryName, compressLevel);
    d->isSplitEntry = false;
    return ret;
}

std::unique_ptr<WriteHandle> SplitZipCreator::createLZ4Entry(const std::string &entryName,
                                                             int compressLevel)
{
    auto ret = d->zipCreator.createLZ4Entry(entryName, compressLevel);
    d->isSplitEntry = false;
    return ret;
}

std::unique_ptr<WriteHandle> SplitZipCreator::createListfileEntry()
{
    if (hasOpenEntry())
        throw std::runtime_error("SplitZipCreator has open archive entry");

    std::unique_ptr<WriteHandle> wh;
    std::string memberName;

    if (d->setup.splitMode == ZipSplitMode::DontSplit)
        memberName = d->setup.filenamePrefix + ".mvlclst";
    else
        memberName = fmt::format("{}_part{:03d}.mvlclst", d->setup.filenamePrefix, d->partIndex);

    memberName = util::basename(memberName);

    switch (d->setup.entryType)
    {
    case ZipEntryInfo::ZIP:
        wh = d->zipCreator.createZIPEntry(memberName, d->setup.compressLevel);
        break;

    case ZipEntryInfo::LZ4:
        wh = d->zipCreator.createLZ4Entry(memberName, d->setup.compressLevel);
        break;
    }

    d->isSplitEntry = d->setup.splitMode != ZipSplitMode::DontSplit;
    d->partCreationTime = std::chrono::steady_clock::now();

    // Write the listfile preamble
    wh->write(d->setup.preamble.data(), d->setup.preamble.size());

    // No splitting -> return the plain WriteHandle obtained from the ZipCreator
    if (d->setup.splitMode == ZipSplitMode::DontSplit)
        return wh;

    // Splitting active -> return a pointer to our ZipEntryWriteHandle
    // std::make_unique() does not work here because it's not a friend of SplitZipWriteHandle
    return std::unique_ptr<SplitZipWriteHandle>(new SplitZipWriteHandle(this));
}

size_t SplitZipCreator::writeToCurrentEntry(const u8 *data, size_t size)
{
    if (!d->isSplitEntry)
        return d->zipCreator.writeToCurrentEntry(data, size);

    assert(d->setup.splitMode != ZipSplitMode::DontSplit);

    bool needNewPart = false;

    if (d->setup.splitMode == ZipSplitMode::SplitBySize)
    {
        needNewPart = entryInfo().bytesWrittenToFile() >= d->setup.splitSize;
    }
    else if (d->setup.splitMode == ZipSplitMode::SplitByTime)
    {
        auto partAge = std::chrono::steady_clock::now() - d->partCreationTime;
        needNewPart = partAge >= d->setup.splitTime;
    }

    if (needNewPart)
    {
        closeArchive();
        ++d->partIndex;
        d->createNextArchive();
        createListfileEntry();

        assert(d->isSplitEntry);
    }

    return d->zipCreator.writeToCurrentEntry(data, size);
}

void SplitZipCreator::closeCurrentEntry() { d->zipCreator.closeCurrentEntry(); }

bool SplitZipCreator::hasOpenEntry() const { return d->zipCreator.hasOpenEntry(); }

const ZipEntryInfo &SplitZipCreator::entryInfo() const { return d->zipCreator.entryInfo(); }

bool SplitZipCreator::isSplitEntry() const { return hasOpenEntry() ? d->isSplitEntry : false; }

ZipCreator *SplitZipCreator::getZipCreator() { return &d->zipCreator; }

SplitZipWriteHandle::SplitZipWriteHandle(SplitZipCreator *creator)
    : creator_(creator)
{
}

SplitZipWriteHandle::~SplitZipWriteHandle() {}

size_t SplitZipWriteHandle::write(const u8 *data, size_t size)
{
    // Splitting is handled in the creator, no need to do anything special
    // here.
    return creator_->writeToCurrentEntry(data, size);
}

//
// ZipReader
//

size_t ZipReadHandle::read(u8 *dest, size_t maxSize)
{
    return m_zipReader->readCurrentEntry(dest, maxSize);
}

size_t ZipReadHandle::seek(size_t pos)
{
    std::string currentName = m_zipReader->currentEntryName();
    // rewind by reopening
    m_zipReader->closeCurrentEntry();
    m_zipReader->openEntry(currentName);

    std::vector<u8> buffer(util::Megabytes(1));
    size_t totalBytesRead = 0u;

    while (totalBytesRead < pos)
    {
        auto bytesRead = read(buffer.data(), std::min(buffer.size(), pos - totalBytesRead));
        totalBytesRead += bytesRead;
        if (bytesRead == 0u)
            break;
    }

    return totalBytesRead;
}

struct ZipReader::Private
{
    struct LZ4ReadContext
    {
        static constexpr size_t ChunkSize = util::Megabytes(1);

        LZ4F_decompressionContext_t ctx = {};
        std::vector<u8> compressedBuffer;
        std::vector<u8> decompressedBuffer;
        basic_string_view<u8> compressedView;
        basic_string_view<u8> decompressedView;

        LZ4ReadContext()
            : compressedBuffer(ChunkSize)
        {
            if (auto err = LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION))
                throw std::runtime_error("LZ4F_createDecompressionContext: " + std::to_string(err));
        }

        ~LZ4ReadContext() { LZ4F_freeDecompressionContext(ctx); }

        void clear()
        {
            LZ4F_resetDecompressionContext(ctx);
            compressedBuffer.clear();
            compressedBuffer.resize(ChunkSize);
            decompressedBuffer.clear();
            compressedView = {};
            decompressedView = {};
        }
    };

    explicit Private(ZipReader *q_)
        : entryReadHandle(q_)
    {
    }

    size_t readFromCurrentZipEntry(u8 *dest, size_t maxSize)
    {
        s32 res = mz_zip_reader_entry_read(this->reader, dest, maxSize);

        // cout << PRETTY_FUNCTION << " maxSize=" << maxSize
        //     << ", res=" << res << endl;

        if (res < 0)
            throw std::runtime_error("mz_zip_reader_entry_read: " + std::to_string(res));

        return static_cast<size_t>(res);
    }

    void *reader = nullptr;
    void *osStream = nullptr;
    std::vector<std::string> entryNameCache;
    // IMPORTANT: The variable sized fields filename, extrafield, comment and
    // linkname are set to nullptr for the mz_zip_entry structures stored in
    // the vector.
    std::vector<mz_zip_entry> entryInfoCache;
    ZipReadHandle entryReadHandle{nullptr};
    ZipEntryInfo entryInfo;
    LZ4ReadContext lz4Ctx;
};

ZipReader::ZipReader()
    : d(std::make_unique<Private>(this))
{
    d->reader = mz_zip_reader_create();
    d->osStream = mz_stream_os_create();
}

ZipReader::~ZipReader()
{
    if (!d)
        return;

    try
    {
        closeArchive();
    }
    catch (...)
    {
    }

    mz_zip_reader_delete(&d->reader);
    mz_stream_os_delete(&d->osStream);
}

ZipReader::ZipReader(ZipReader &&o) { std::swap(d, o.d); }

ZipReader &ZipReader::operator=(ZipReader &&o)
{
    std::swap(d, o.d);
    return *this;
}

void ZipReader::openArchive(const std::string &archiveName)
{
    if (auto err = mz_stream_os_open(d->osStream, archiveName.c_str(), MZ_OPEN_MODE_READ))
        throw std::runtime_error("mz_stream_os_open: " + std::to_string(err));

    if (auto err = mz_zip_reader_open(d->reader, d->osStream))
        throw std::runtime_error("mz_zip_reader_open: " + std::to_string(err));

    if (auto err = mz_zip_reader_goto_first_entry(d->reader))
    {
        if (err != MZ_END_OF_LIST)
            throw std::runtime_error("mz_zip_reader_goto_first_entry: " + std::to_string(err));
    }

    d->entryNameCache = {};
    s32 err = MZ_OK;

    do
    {
        mz_zip_file *entryInfo = nullptr;
        if ((err = mz_zip_reader_entry_get_info(d->reader, &entryInfo)))
            throw std::runtime_error("mz_zip_reader_entry_get_info: " + std::to_string(err));

        d->entryNameCache.push_back(entryInfo->filename);

        mz_zip_entry entryCopy = *entryInfo;
        entryCopy.filename = nullptr;
        entryCopy.extrafield = nullptr;
        entryCopy.comment = nullptr;
        entryCopy.linkname = nullptr;
        d->entryInfoCache.push_back(entryCopy);
    }
    while ((err = mz_zip_reader_goto_next_entry(d->reader)) == MZ_OK);

    if (err != MZ_END_OF_LIST)
        throw std::runtime_error("mz_zip_reader_goto_next_entry: " + std::to_string(err));
}

void ZipReader::closeArchive()
{
    if (auto err = mz_zip_reader_close(d->reader))
        throw std::runtime_error("mz_zip_reader_close: " + std::to_string(err));

    if (auto err = mz_stream_os_close(d->osStream))
        throw std::runtime_error("mz_stream_os_close: " + std::to_string(err));

    d->entryNameCache = {};
}

std::vector<std::string> ZipReader::entryNameList() { return d->entryNameCache; }

namespace
{

inline size_t get_block_size(const LZ4F_frameInfo_t *info)
{
    switch (info->blockSizeID)
    {
    case LZ4F_default:
    case LZ4F_max64KB:
        return 1 << 16;
    case LZ4F_max256KB:
        return 1 << 18;
    case LZ4F_max1MB:
        return 1 << 20;
    case LZ4F_max4MB:
        return 1 << 22;
    default:
        throw std::runtime_error(
            "impossible LZ4 block size with current frame specification (<=v1.6.1)");
    }
}

} // namespace

ZipReadHandle *ZipReader::openEntry(const std::string &name)
{
    if (auto err = mz_zip_reader_locate_entry(d->reader, name.c_str(), false))
        throw std::runtime_error("mz_zip_reader_locate_entry: " + std::to_string(err));

    if (auto err = mz_zip_reader_entry_open(d->reader))
        throw std::runtime_error("mz_zip_reader_entry_open: " + std::to_string(err));

    mz_zip_file *mzEntryInfo = nullptr;

    if (auto err = mz_zip_reader_entry_get_info(d->reader, &mzEntryInfo))
        throw std::runtime_error("mz_zip_reader_entry_get_info: " + std::to_string(err));

    d->lz4Ctx.clear();
    d->entryInfo = {};
    d->entryInfo.name = name;
    d->entryInfo.compressedSize = mzEntryInfo->compressed_size;
    d->entryInfo.uncompressedSize = mzEntryInfo->uncompressed_size;

    if (name.size() >= 4)
    {
        auto suffix = string_view(name.data() + (name.length() - 4), 4);

        if (suffix == ".lz4")
        {
            d->entryInfo.type = ZipEntryInfo::LZ4;
        }
    }

    if (d->entryInfo.type == ZipEntryInfo::LZ4)
    {
        size_t bytesRead = d->readFromCurrentZipEntry(d->lz4Ctx.compressedBuffer.data(),
                                                      d->lz4Ctx.compressedBuffer.size());

        // cout << PRETTY_FUNCTION << ": initial read from zip yielded " << bytesRead << " bytes" <<
        // endl;

        d->entryInfo.lz4CompressedBytesRead += bytesRead;

        if (bytesRead == 0)
            throw std::runtime_error(
                "ZipReader::openEntry: not enough data to initialise LZ4 decompression");

        d->lz4Ctx.compressedView = {d->lz4Ctx.compressedBuffer.data(), bytesRead};

        assert(d->lz4Ctx.compressedView.size() == bytesRead);

        LZ4F_frameInfo_t info = {};

        size_t bytesConsumed = bytesRead;

        size_t res = LZ4F_getFrameInfo(d->lz4Ctx.ctx, &info, d->lz4Ctx.compressedView.data(),
                                       &bytesConsumed);

        if (LZ4F_isError(res))
            throw std::runtime_error("LZ4F_getFrameInfo: " + std::to_string(res));

        assert(d->lz4Ctx.compressedView.size() >= bytesConsumed);

        // cout << PRETTY_FUNCTION << " LZ4F_getFrameInfo consumed " << bytesConsumed << " bytes" <<
        // endl; for (size_t i=0; i<bytesConsumed; ++i)
        //     cout << PRETTY_FUNCTION << "   " << i << ": "
        //       << std::hex << static_cast<unsigned>(d->lz4Ctx.compressedView[i]) << std::dec <<
        //       endl;

        d->lz4Ctx.compressedView.remove_prefix(bytesConsumed);

        size_t dstCapacity = get_block_size(&info);

        // cout << PRETTY_FUNCTION << " dstCapacity from LZ4 frameInfo: " << dstCapacity << endl;
        // cout << PRETTY_FUNCTION << " compressedView.size() for first read is " <<
        // d->lz4Ctx.compressedView.size() << endl;

        d->lz4Ctx.decompressedBuffer.resize(dstCapacity);
    }

    d->entryInfo.isOpen = true;

    auto result = &d->entryReadHandle;
    assert(result);
    return result;
}

ZipReadHandle *ZipReader::currentEntry() { return &d->entryReadHandle; }

void ZipReader::closeCurrentEntry()
{
    auto err = mz_zip_reader_entry_close(d->reader);

    if (err != MZ_OK && err != MZ_CRC_ERROR)
        throw std::runtime_error("mz_zip_reader_entry_close: " + std::to_string(err));
}

size_t ZipReader::readCurrentEntry(u8 *dest, size_t maxSize)
{
    if (d->entryInfo.type == ZipEntryInfo::ZIP)
        return d->readFromCurrentZipEntry(dest, maxSize);

    assert(d->entryInfo.type == ZipEntryInfo::LZ4);

    size_t retval = 0u;
    [[maybe_unused]] size_t loop = 0u; // for printf debugging

    while (maxSize - retval > 0)
    {
        if (d->lz4Ctx.decompressedView.empty())
        {
            // cout << PRETTY_FUNCTION << " loop #" << loop << ": decompressedView is empty,
            // decompressing more data" << endl; cout << PRETTY_FUNCTION << " loop #" << loop << ":
            // compressedView.size()=" << d->lz4Ctx.compressedView.size() << endl;

            if (d->lz4Ctx.compressedView.empty())
            {
                // cout << PRETTY_FUNCTION << " loop #" << loop << ": compressedView is empty,
                // reading more data from zip" << endl;

                size_t bytesRead = d->readFromCurrentZipEntry(d->lz4Ctx.compressedBuffer.data(),
                                                              d->lz4Ctx.compressedBuffer.size());

                d->lz4Ctx.compressedView = {d->lz4Ctx.compressedBuffer.data(), bytesRead};

                // cout << PRETTY_FUNCTION << "read " << bytesRead << " bytes of uncompressed data"
                // << endl;

                if (bytesRead == 0)
                    break;
            }

            assert(!d->lz4Ctx.compressedView.empty());

            // decompress from compressedBuffer into decompressedBuffer

            size_t decompressedSize = d->lz4Ctx.decompressedBuffer.size();
            size_t compressedSize = d->lz4Ctx.compressedView.size();

            s32 res = LZ4F_decompress(d->lz4Ctx.ctx, d->lz4Ctx.decompressedBuffer.data(),
                                      &decompressedSize,                                // dest
                                      d->lz4Ctx.compressedView.data(), &compressedSize, // source
                                      nullptr);                                         // options

            // if (res == 0)
            //     cout << PRETTY_FUNCTION << " loop #" << loop << ": LZ4F_decompress returned " <<
            //     res << endl;

            // cout << PRETTY_FUNCTION << " LZ4F_decompress: "
            //     << ", compressedSize=" << compressedSize
            //     << ", decompressedSize=" << decompressedSize
            //     << ", res=" << res
            //     << endl;

            if (LZ4F_isError(res))
                throw std::runtime_error("LZ4F_decompress: " + std::to_string(res));

            d->lz4Ctx.decompressedView = {d->lz4Ctx.decompressedBuffer.data(), decompressedSize};
            d->lz4Ctx.compressedView.remove_prefix(compressedSize);
        }

        size_t toCopy = std::min(d->lz4Ctx.decompressedView.size(), maxSize - retval);
        std::memcpy(dest + retval, d->lz4Ctx.decompressedView.data(), toCopy);
        d->lz4Ctx.decompressedView.remove_prefix(toCopy);
        retval += toCopy;

        /*
        if (toCopy)
        {
            std::cout << PRETTY_FUNCTION << " loop #" << loop
                << ": copied " << toCopy << " bytes from decompressed view into dest buffer"
                << ", dest[0]=" << std::hex << static_cast<unsigned>(dest[0])
                << ", dest[1]=" << std::hex << static_cast<unsigned>(dest[1])
                << ", dest[2]=" << std::hex << static_cast<unsigned>(dest[2])
                << ", dest[3]=" << std::hex << static_cast<unsigned>(dest[3])
                << ", dest[4]=" << std::hex << static_cast<unsigned>(dest[4])
                << std::dec
                << endl;
        }
        */

        ++loop;
    }

    // cout << PRETTY_FUNCTION << "read of maxSize=" << maxSize
    //     << " satisfied after " << loop << " loops" << endl;

    return retval;
}

std::string ZipReader::currentEntryName() const { return d->entryInfo.name; }

const ZipEntryInfo &ZipReader::entryInfo() const { return d->entryInfo; }

std::string ZipReader::firstListfileEntryName()
{
    auto entryNames = entryNameList();

    auto it = std::find_if(std::begin(entryNames), std::end(entryNames),
                           [](const std::string &entryName)
                           {
                               static const std::regex re(R"foo(.+\.mvlclst(\.lz4)?)foo");
                               return std::regex_search(entryName, re);
                           });

    return (it != std::end(entryNames) ? *it : std::string{});
}

void *ZipReader::getRawReaderHandle() const { return d->reader; }

std::string next_archive_name(const std::string currentArchiveName)
{
    static const std::regex reSplitName("^(.+)_part([0-9]+)\\.zip");

    std::smatch matches;
    std::regex_match(currentArchiveName, matches, reSplitName);

    if (matches.size() < 3)
        return {};

    auto partPrefix = matches[1].str();
    auto partNumStr = matches[2].str();
    auto partNumber = std::atoll(partNumStr.c_str());
    auto filename = fmt::format("{}_part{:03d}.zip", partPrefix, partNumber + 1);
    return filename;
}

size_t SplitZipReadHandle::read(u8 *dest, size_t maxSize)
{
    return m_zipReader->readCurrentEntry(dest, maxSize);
}

size_t SplitZipReadHandle::seek(size_t pos) { return m_zipReader->seek(pos); }

struct SplitZipReader::Private
{
    SplitZipReader *q = nullptr;
    ZipReader zipReader;
    SplitZipReadHandle splitReadHandle;
    bool isSplitEntry = false;
    std::string firstArchiveName;
    std::string currentArchiveName;
    SplitZipReader::ArchiveChangedCallback archiveChangedCallback;
    std::shared_ptr<spdlog::logger> logger;

    explicit Private(SplitZipReader *q_)
        : q(q_)
        , splitReadHandle(q)
        , logger(get_logger("SplitZipReader"))
    {
    }

    std::string nextArchiveName() const
    {
        auto result = next_archive_name(currentArchiveName);
        logger->debug("firstArchiveName={}, currentArchvieName={}, nextArchiveName={}",
                      firstArchiveName, currentArchiveName, result);
        return result;
    }
};

SplitZipReader::SplitZipReader()
    : d(std::make_unique<Private>(this))
{
}

SplitZipReader::~SplitZipReader() {}

void SplitZipReader::openArchive(const std::string &archiveName)
{
    d->zipReader.openArchive(archiveName);
    d->firstArchiveName = archiveName;
    d->currentArchiveName = archiveName;
    if (d->archiveChangedCallback)
        d->archiveChangedCallback(this, d->currentArchiveName);
}

void SplitZipReader::closeArchive() { d->zipReader.closeArchive(); }

std::vector<std::string> SplitZipReader::entryNameList() { return d->zipReader.entryNameList(); }

ZipReadHandle *SplitZipReader::openEntry(const std::string &name)
{
    d->isSplitEntry = false;
    return d->zipReader.openEntry(name);
}

ZipReadHandle *SplitZipReader::currentEntry() { return d->zipReader.currentEntry(); }

void SplitZipReader::closeCurrentEntry()
{
    d->zipReader.closeCurrentEntry();
    d->isSplitEntry = false;
}

size_t SplitZipReader::readCurrentEntry(u8 *dest, size_t maxSize)
{
    if (!d->isSplitEntry)
        return d->zipReader.readCurrentEntry(dest, maxSize);

    size_t totalBytesRead = 0u;

    while (totalBytesRead < maxSize)
    {
        auto bytesRead =
            d->zipReader.readCurrentEntry(dest + totalBytesRead, maxSize - totalBytesRead);
        totalBytesRead += bytesRead;

        if (bytesRead == 0)
        {
            auto nextArchiveName = d->nextArchiveName();

            if (!util::file_exists(nextArchiveName))
                break;

            d->zipReader.closeArchive();
            d->zipReader.openArchive(nextArchiveName);
            d->currentArchiveName = nextArchiveName;
            if (d->archiveChangedCallback)
                d->archiveChangedCallback(this, d->currentArchiveName);
            auto readHandle = d->zipReader.openEntry(d->zipReader.firstListfileEntryName());

            // Have to read past the magic bytes at the start to land on the
            // first frame or eth packet.
            auto magic = read_magic_str(*readHandle);

            if (magic != get_filemagic_eth() && magic != get_filemagic_usb())
                d->logger->warn("SplitZipReader: archive={}, entry={}: invalid magic bytes at "
                                "start of file: '{}'!",
                                d->currentArchiveName, d->zipReader.firstListfileEntryName(),
                                magic);
        }
    }

    return totalBytesRead;
}

std::string SplitZipReader::currentEntryName() const { return d->zipReader.currentEntryName(); }

const ZipEntryInfo &SplitZipReader::entryInfo() const { return d->zipReader.entryInfo(); }

std::string SplitZipReader::firstListfileEntryName()
{
    return d->zipReader.firstListfileEntryName();
}

SplitZipReadHandle *SplitZipReader::openFirstListfileEntry()
{
    auto name = d->zipReader.firstListfileEntryName();
    d->zipReader.openEntry(name);
    d->isSplitEntry = true;
    return &d->splitReadHandle;
}

size_t SplitZipReader::seek(size_t pos)
{
    if (!d->isSplitEntry)
        return d->zipReader.currentEntry()->seek(pos);

    // Go to the first archive in the series.
    if (d->currentArchiveName != d->firstArchiveName)
    {
        d->zipReader.closeArchive();
        d->zipReader.openArchive(d->firstArchiveName);
        d->currentArchiveName = d->firstArchiveName;
        if (d->archiveChangedCallback)
            d->archiveChangedCallback(this, d->currentArchiveName);
    }
    d->zipReader.closeCurrentEntry();
    d->zipReader.openEntry(d->zipReader.firstListfileEntryName());

    size_t totalBytesRead = 0;

    while (totalBytesRead < pos)
    {
        auto bytesRead = d->zipReader.currentEntry()->seek(pos - totalBytesRead);
        totalBytesRead += bytesRead;

        if (bytesRead == 0u)
            break;
    }

    return totalBytesRead;
}

void SplitZipReader::setArchiveChangedCallback(ArchiveChangedCallback cb)
{
    d->archiveChangedCallback = cb;
}

std::string get_filename(const ZipOperation &op)
{
    return std::visit([](const auto &operation) { return operation.filename; }, op);
}

std::optional<std::string> get_description(const ZipOperation &op)
{
    return std::visit([](const auto &operation) { return operation.description; }, op);
}

ZipUpdateResult check_free_space(const ZipUpdateConfig &config)
{
    auto spaceInfo = std::filesystem::space(config.input_zip_path);
    auto freeSpace = spaceInfo.available;

    auto inputArchiveSize = std::filesystem::file_size(config.input_zip_path);
    auto requiredSpace = inputArchiveSize * config.min_available_space_ratio;

    if (freeSpace < requiredSpace)
    {
        ZipUpdateResult result;
        result.ec = std::make_error_code(std::errc::no_space_on_device);
        result.error_detail = fmt::format(
            "Not enough free space to update ZIP archive. Required: {} bytes, available: {} bytes",
            requiredSpace, freeSpace);
        return result;
    }
    return ZipUpdateResult{};
}

// some simple validity checks
ZipUpdateResult pre_check(listfile::ZipReader &reader, const ZipUpdateConfig &config)
{
    ZipUpdateResult result;
    auto readerEntries = reader.entryNameList();

    for (const auto &op: config.ops)
    {
        std::visit(
            util::overloaded{
                [&](const AddOperation &op)
                {
                    if (util::contains(readerEntries, op.filename))
                    {
                        result.ec = std::make_error_code(std::errc::file_exists);
                        result.error_detail =
                            fmt::format("AddOperation: file {} already exists in ZIP", op.filename);
                    }
                },
                [&](const UpdateOperation &op)
                {
                    if (!util::contains(readerEntries, op.filename))
                    {
                        result.ec = std::make_error_code(std::errc::no_such_file_or_directory);
                        result.error_detail = fmt::format(
                            "UpdateOperation: file {} does not exist in ZIP", op.filename);
                    }
                },
                [&](const DeleteOperation &op)
                {
                    if (!util::contains(readerEntries, op.filename))
                    {
                        result.ec = std::make_error_code(std::errc::no_such_file_or_directory);
                        result.error_detail = fmt::format(
                            "DeleteOperation: file {} does not exist in ZIP", op.filename);
                    }
                }},
            op);

        if (result.ec)
            return result;
    }

    return result;
}

using OperationsMap = std::unordered_map<std::string, ZipOperation>;

// filename -> op mapping and duplicate check
ZipUpdateResult fill_operations_map(OperationsMap &opsMap, const std::vector<ZipOperation> &ops)
{
    ZipUpdateResult result;

    for (const auto &op: ops)
    {
        auto filename = get_filename(op);

        if (opsMap.find(filename) != opsMap.end())
        {
            result.ec = std::make_error_code(std::errc::invalid_argument);
            result.error_detail = fmt::format("Duplicate operation for file {} in ZIP", filename);
            return result;
        }
        opsMap[filename] = op;
    }

    return result;
}

size_t get_bytes_to_write(const ZipOperation &op)
{
    return std::visit(
        util::overloaded{[&](const AddOperation &addOp) { return addOp.contents.size(); },
                         [&](const UpdateOperation &updateOp) { return updateOp.contents.size(); },
                         [&](const DeleteOperation &) { return std::size_t{0u}; }},
        op);
}

size_t determine_total_bytes_to_write(ZipReader &reader, const OperationsMap &opsMap,
                                      const ZipUpdateConfig &config)
{
    size_t totalBytesToWrite = 0;

    for (const auto &entryName: reader.entryNameList())
    {
        const auto opIt = opsMap.find(entryName);
        const bool hasOperation = opIt != opsMap.end();

        if (hasOperation)
        {
            totalBytesToWrite += get_bytes_to_write(opIt->second);
        }
        else
        {
            // No operation for this entry -> copy it to the new archive.
            // Use compressed size since we'll be doing a raw copy
            reader.openEntry(entryName);
            totalBytesToWrite += reader.entryInfo().compressedSize;
            reader.closeCurrentEntry();
        }
    }

    for (const auto &op: config.ops)
    {
        if (std::holds_alternative<AddOperation>(op))
        {
            totalBytesToWrite += get_bytes_to_write(op);
        }
    }

    return totalBytesToWrite;
}

std::string make_temp_zip_archive_name(const std::string &inputZipPath)
{
    auto targetPath = fs::path(inputZipPath);
    auto targetDir = targetPath.parent_path();
    if (targetDir.empty())
        targetDir = fs::current_path();

    return (targetDir / util::make_tempfile_name("mvlc_zip_updater")).make_preferred().string();
}

// Helper function to copy a zip entry without recompressing it.
// Returns a pair of minizip-ng error code or MZ_OK and the number of bytes
// written. MZ_INTERNAL_ERROR is returned on cancellation.
std::pair<s32, size_t> copy_zip_entry(void *reader_handle, void *writer_handle, const std::string &entryName,
    ProgressCallback on_progress, CancelCallback is_cancelled)
{
    mz_zip_reader_set_raw(reader_handle, 1);
    mz_zip_writer_set_raw(writer_handle, 1);

    BOOST_SCOPE_EXIT(reader_handle, writer_handle)
    {
        mz_zip_reader_set_raw(reader_handle, 0);
        mz_zip_writer_set_raw(writer_handle, 0);
    } BOOST_SCOPE_EXIT_END;

    if (auto err = mz_zip_reader_locate_entry(reader_handle, entryName.c_str(), false))
        return {err, 0};

    mz_zip_file *src_file_info = nullptr;

    if (auto err = mz_zip_reader_entry_get_info(reader_handle, &src_file_info))
        return {err, 0};

    size_t total_bytes_to_write = src_file_info->compressed_size;
    mz_zip_file dst_file_info = *src_file_info;

    if (auto err = mz_zip_reader_entry_open(reader_handle))
        return {err, 0};

    if (auto err = mz_zip_writer_entry_open(writer_handle, &dst_file_info))
        return {err, 0};

    std::vector<u8> buffer(util::Megabytes(1));
    ProgressEvent progress_event;
    progress_event.step = fmt::format("Copying entry '{}'", entryName);
    progress_event.progress = 0.0;
    progress_event.bytes_copied = 0;
    progress_event.bytes_total = total_bytes_to_write;
    s32 bytes_read = 0;

    while ((bytes_read = mz_zip_reader_entry_read(reader_handle, buffer.data(), buffer.size())) > 0)
    {
        s32 bytes_written = mz_zip_writer_entry_write(writer_handle, buffer.data(), bytes_read);

        if (bytes_written > 0)
            *progress_event.bytes_copied += bytes_written;

        if (bytes_written < 0 || (is_cancelled && is_cancelled()))
        {
            mz_zip_reader_entry_close(reader_handle);
            mz_zip_writer_entry_close(writer_handle);

            return {bytes_written < 0 ? bytes_written : MZ_INTERNAL_ERROR, *progress_event.bytes_copied};
        }


        if (on_progress)
        {
            progress_event.progress = static_cast<double>(*progress_event.bytes_copied) /
                                      static_cast<double>(*progress_event.bytes_total);
            on_progress(progress_event);
        }
    }

    mz_zip_reader_entry_close(reader_handle);
    mz_zip_writer_entry_close(writer_handle);

    return { bytes_read < 0 ? bytes_read : MZ_OK, *progress_event.bytes_copied };
}

ZipUpdateResult update_zip_archive(const ZipUpdateConfig &config)
{
    ZipUpdateResult result;

    if (result = check_free_space(config); result.ec)
        return result;

    try
    {
        listfile::ZipReader reader;
        reader.openArchive(config.input_zip_path);

        if (result = pre_check(reader, config); result.ec)
            return result;

        std::unordered_map<std::string, ZipOperation> opsMap;

        if (result = fill_operations_map(opsMap, config.ops); result.ec)
            return result;

        const auto readerEntries = reader.entryNameList();

        // Calculate the total number of bytes we need to write to the output archive.
        size_t totalBytesToWrite = determine_total_bytes_to_write(reader, opsMap, config);
        size_t totalBytesWritten = 0u;

        // Create temp file in the same directory as the target.
        auto tmpName = make_temp_zip_archive_name(config.input_zip_path);

        // Ensures auto cleanup of the temp file on error/exit.
        auto tmpFileDeleter = util::make_fn_deleter(tmpName);

        // Create the new archive
        ZipCreator writer;
        writer.createArchive(tmpName, OverwriteMode::Overwrite);

        // Get the mz_zip_reader/writer handles (mz_zip_rw.h).
        void *mz_reader = reader.getRawReaderHandle();
        void *mz_writer = writer.getRawWriterHandle();
        assert(mz_reader);
        assert(mz_writer);

        for (const auto &entryName: readerEntries)
        {
            const auto opIt = opsMap.find(entryName);
            const bool hasOperation = opIt != opsMap.end();

            if (hasOperation)
            {
                const auto &op = opIt->second;
                // For Delete: skip copying this entry
                // For Update: skip copying, we'll write the new version later
                if (std::holds_alternative<DeleteOperation>(op))
                {
                    result.ops_descriptions.push_back(
                        fmt::format("Deleted file '{}' from ZIP", entryName));
                    continue;
                }
                else if (std::holds_alternative<UpdateOperation>(op))
                {
                    // Skip the old version; we'll write the new one after the loop
                    continue;
                }
            }

            // Copy this entry from reader to writer.
            try
            {
                auto copy_result = copy_zip_entry(mz_reader, mz_writer, entryName, config.on_progress, config.is_cancelled);

                if (auto err = copy_result.first; err != MZ_OK)
                {
                    if (err == MZ_INTERNAL_ERROR)
                    {
                        result.ec = std::make_error_code(std::errc::operation_canceled);
                        result.error_detail = "Operation was cancelled by user";
                        return result;
                    }
                    throw std::runtime_error("copy_zip_entry: " + std::to_string(err));
                }

                result.ops_descriptions.push_back(
                    fmt::format("Copied entry '{}' to new ZIP ({} bytes)", entryName, copy_result.second));
                totalBytesWritten += copy_result.second;
            }
            catch (const std::exception &e)
            {
                result.ex = std::current_exception();
                result.error_detail =
                    fmt::format("Failed to copy entry '{}': {}", entryName, e.what());
                return result;
            }
        }

        // Now process Add and Update operations
        for (const auto &op: config.ops)
        {
            if (config.is_cancelled && config.is_cancelled())
            {
                result.ec = std::make_error_code(std::errc::operation_canceled);
                result.error_detail = "Operation was cancelled by user";
                return result;
            }

            std::visit(
                util::overloaded{
                    [&](const AddOperation &addOp)
                    {
                        try
                        {
                            auto writeHandle = writer.createZIPEntry(addOp.filename, addOp.compression_level);
                            size_t bytesWritten =
                                writeHandle->write(addOp.contents.data(), addOp.contents.size());
                            totalBytesWritten += bytesWritten;
                            writer.closeCurrentEntry();
                            result.ops_descriptions.push_back(
                                fmt::format("Added file '{}' to ZIP", addOp.filename));
                        }
                        catch (const std::exception &e)
                        {
                            result.ex = std::current_exception();
                            result.error_detail = fmt::format("Failed to add file '{}': {}",
                                                              addOp.filename, e.what());
                        }
                    },
                    [&](const UpdateOperation &updateOp)
                    {
                        try
                        {
                            auto writeHandle = writer.createZIPEntry(updateOp.filename, 0);
                            size_t bytesWritten = writeHandle->write(updateOp.contents.data(),
                                                                     updateOp.contents.size());
                            totalBytesWritten += bytesWritten;
                            writer.closeCurrentEntry();
                            result.ops_descriptions.push_back(
                                fmt::format("Updated file '{}' in ZIP", updateOp.filename));
                        }
                        catch (const std::exception &e)
                        {
                            result.ex = std::current_exception();
                            result.error_detail = fmt::format("Failed to update file '{}': {}",
                                                              updateOp.filename, e.what());
                        }
                    },
                    [&](const DeleteOperation &)
                    {
                        // Already handled above
                    }},
                op);

            if (!result.did_succeed())
                return result;

            if (config.on_progress)
            {
                double progress = static_cast<double>(totalBytesWritten) / totalBytesToWrite;
                ProgressEvent event;
                event.step = get_description(op).value_or(get_filename(op));
                event.progress = progress;
                event.bytes_copied = totalBytesWritten;
                event.bytes_total = totalBytesToWrite;

                config.on_progress(event);
            }
        }

        // Close both archives
        try
        {
            writer.closeArchive();
            reader.closeArchive();
        }
        catch (const std::exception &e)
        {
            result.ex = std::current_exception();
            result.error_detail = fmt::format("Failed to close archive: {}", e.what());
            return result;
        }

        // Replace the original file with the new one
        // Try rename first, fall back to copy+delete on Windows if it fails
        std::error_code ec;
        for (int i=0; i<10; ++i)
        {
            // Replace the original file with the new one
            // Try rename first, fall back to copy+delete on Windows if it fails
            fs::rename(tmpName, config.input_zip_path, ec);
            if (!ec)
                break;
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                spdlog::warn("rename failed (from={}, to={}, error={}), retrying ({}/3)",
                             tmpName, config.input_zip_path, ec.message(), i + 1);
            }
        }

        if (ec)
        {
            // Rename failed, try copy + delete (slower but works across filesystems)
            spdlog::warn("rename failed (from={}, to={}, error={}), trying copy+delete fallback",
                         tmpName, config.input_zip_path, ec.message());

            fs::copy_file(tmpName, config.input_zip_path, fs::copy_options::overwrite_existing, ec);

            if (ec)
            {
                result.ec = ec;
                result.error_detail =
                    fmt::format("Failed to replace original ZIP file: {}", ec.message());
                return result;
            }
        }
        else
        {
            spdlog::warn("Rename was ok! {} -> {}", tmpName, config.input_zip_path);
        }
    }
    catch (const std::exception &e)
    {
        result.ec = std::make_error_code(std::errc::io_error);
        result.error_detail = fmt::format("Exception during ZIP update: {}", e.what());
        return result;
    }

    return result;
}

} // namespace mesytec::mvlc::listfile
