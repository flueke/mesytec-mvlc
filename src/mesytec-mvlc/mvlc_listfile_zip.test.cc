#include <chrono>
#include <random>
#include <iostream>
#include <mz.h>
#include <mz_os.h>
#include <mz_strm.h>
#include <mz_strm_os.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

#include "gtest/gtest.h"

#include "mvlc_listfile_zip.h"
#include "util/filesystem.h"
#include "util/fmt.h"
#include "util/io_util.h"
#include "util/storage_sizes.h"

using std::cout;
using std::endl;

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::listfile;

TEST(mvlc_listfile_zip, CreateOverwrite)
{
    std::string archiveName = "mvlc_listfile_CreateOverwrite.zip";

    {
        ZipCreator creator;
        ASSERT_NO_THROW(creator.createArchive(archiveName));
    }

    {
        ZipCreator creator;
        ASSERT_THROW(creator.createArchive(archiveName), std::runtime_error);
        ASSERT_NO_THROW(creator.createArchive(archiveName, OverwriteMode::Overwrite));
    }

    ASSERT_EQ(mz_os_unlink(archiveName.c_str()), MZ_OK);
}

TEST(mvlc_listfile_zip, CreateWriteRead)
{
    const std::vector<u8> outData0 = { 0x12, 0x34, 0x56, 0x78 };
    const std::vector<u8> outData1 = { 0xff, 0xfe, 0xfd, 0xdc };

    std::string archiveName = "mvlc_listfile_zip.test.ListfileCreate3.zip";

    {
        ZipCreator creator;
        creator.createArchive(archiveName, OverwriteMode::Overwrite);

        {
            // Write outData0 two times
            auto &writeHandle = *creator.createZIPEntry("outfile0.data", 1);
            writeHandle.write(outData0.data(), outData0.size());
            writeHandle.write(outData0.data(), outData0.size());
            creator.closeCurrentEntry();
        }

        {
            // Write outData1 one time
            auto &writeHandle = *creator.createLZ4Entry("outfile1.data", 1);
            writeHandle.write(outData1.data(), outData1.size());
            creator.closeCurrentEntry();
        }
    }

    {
        ZipReader reader;
        reader.openArchive(archiveName);

        auto entryNameList = reader.entryNameList();
        std::vector<std::string> expectedEntries = { "outfile0.data", "outfile1.data.lz4" };

        ASSERT_EQ(entryNameList, expectedEntries);

        {
            auto readHandle = reader.openEntry("outfile0.data");

            {
                // Read outData0 two times
                std::vector<u8> inData0(outData0.size());
                size_t bytesRead = readHandle->read(inData0.data(), inData0.size());

                ASSERT_EQ(bytesRead, outData0.size());
                ASSERT_EQ(inData0, outData0);

                bytesRead = readHandle->read(inData0.data(), inData0.size());
                ASSERT_EQ(bytesRead, outData0.size());
                ASSERT_EQ(inData0, outData0);

                // Third read should yield 0 bytes as we're at the end of the entry.
                bytesRead = readHandle->read(inData0.data(), inData0.size());
                ASSERT_EQ(bytesRead, 0);
            }

            {
                // Restart reading from the beginning of the entry.
                readHandle->seek(0);
                std::vector<u8> inData0(outData0.size() * 2);
                size_t bytesRead = readHandle->read(inData0.data(), inData0.size());

                const std::vector<u8> outData0_2 = {
                    0x12, 0x34, 0x56, 0x78,
                    0x12, 0x34, 0x56, 0x78
                };

                ASSERT_EQ(bytesRead, outData0.size() * 2);
                ASSERT_EQ(inData0, outData0_2);
            }
        }

        {
            auto readHandle = reader.openEntry("outfile1.data.lz4");

            {
                // Read outData1 one time
                std::vector<u8> inData1(outData1.size());
                size_t bytesRead = readHandle->read(inData1.data(), inData1.size());

                ASSERT_EQ(bytesRead, outData1.size());
                ASSERT_EQ(inData1, outData1);

                // Next read should yield 0 bytes as we're at the end of the entry.
                bytesRead = readHandle->read(inData1.data(), inData1.size());
                ASSERT_EQ(bytesRead, 0);
            }

            {
                // Restart reading from the beginning of the entry.
                readHandle->seek(0);
                std::vector<u8> inData1(outData0.size());
                size_t bytesRead = readHandle->read(inData1.data(), inData1.size());

                ASSERT_EQ(bytesRead, outData0.size());
                ASSERT_EQ(inData1, outData1);
            }
        }
    }
}

TEST(mvlc_listfile_zip, LZ4Data)
{
    std::vector<u8> outData0(util::Megabytes(50));

    for (size_t i=0; i<outData0.size(); i++)
    {
        outData0[i] = i % 255u;
    }

    std::string archiveName = "mvlc_listfile_zip.test.LZ4Data.zip";

    {
        ZipCreator creator;
        creator.createArchive(archiveName, OverwriteMode::Overwrite);

        auto &writeHandle = *creator.createLZ4Entry("outfile0.data", 0);

        size_t bytesWritten = writeHandle.write(outData0.data(), outData0.size());

        ASSERT_EQ(bytesWritten, outData0.size());
    }

    {
        ZipReader reader;
        reader.openArchive(archiveName);

        auto entryNameList = reader.entryNameList();
        std::vector<std::string> expectedEntries = { "outfile0.data.lz4" };

        ASSERT_EQ(entryNameList, expectedEntries);

        auto ReadChunkSizes =
        {
            static_cast<size_t>(500), util::Kilobytes(1), util::Kilobytes(10), util::Kilobytes(100),
            static_cast<size_t>(10000u), util::Megabytes(1), util::Megabytes(10)
        };

        // Read the data in small chunks to test the decompression + buffering code
        for (const auto ReadChunkSize: ReadChunkSizes)
        {
            std::vector<u8> readBuffer(outData0.size());
            auto readHandle = reader.openEntry("outfile0.data.lz4");
            size_t bytesRead = 0u, totalBytesRead = 0u, readCount = 0u;

            do
            {
                bytesRead = readHandle->read(
                    readBuffer.data() + totalBytesRead,
                    ReadChunkSize);

                totalBytesRead += bytesRead;
                ++readCount;
            } while (bytesRead > 0);

            ASSERT_EQ(outData0.size(), totalBytesRead);
            ASSERT_EQ(outData0, readBuffer);

            //cout << __PRETTY_FUNCTION__ << " ReadChunkSize=" << ReadChunkSize
            //    << ", totalBytesRead=" << totalBytesRead
            //    << ", readCount=" << readCount
            //    << endl;
        }
    }
}

TEST(mvlc_listfile_zip, Split_CreateArchive)
{

    size_t openCallbackCalls = 0u;
    size_t closeCallbackCalls = 0u;

    auto  open_callback = [&openCallbackCalls] (SplitZipCreator *creator)
    {
        ASSERT_TRUE(creator != nullptr);
        ++openCallbackCalls;
    };

    auto  close_callback = [&closeCallbackCalls] (SplitZipCreator *creator)
    {
        ASSERT_TRUE(creator != nullptr);
        ++closeCallbackCalls;
    };

    {
        ASSERT_FALSE(util::file_exists("splitzip_archive.zip"));

        SplitZipCreator creator;
        SplitListfileSetup setup;
        setup.splitMode = ZipSplitMode::DontSplit;
        setup.filenamePrefix = "splitzip_archive";
        setup.openArchiveCallback = open_callback;
        setup.closeArchiveCallback = close_callback;

        openCallbackCalls = 0u;
        closeCallbackCalls = 0u;

        creator.createArchive(setup);

        ASSERT_TRUE(util::file_exists("splitzip_archive.zip"));
        ASSERT_TRUE(util::delete_file("splitzip_archive.zip"));
        ASSERT_EQ(openCallbackCalls, 1);
        ASSERT_EQ(closeCallbackCalls, 0);

        creator.closeArchive();

        ASSERT_EQ(openCallbackCalls, 1);
        ASSERT_EQ(closeCallbackCalls, 1);
    }

    {
        ASSERT_FALSE(util::file_exists("splitzip_archive_part000.zip"));

        SplitZipCreator creator;
        SplitListfileSetup setup;
        setup.splitMode = ZipSplitMode::SplitBySize;
        setup.filenamePrefix = "splitzip_archive";
        setup.openArchiveCallback = open_callback;
        setup.closeArchiveCallback = close_callback;

        openCallbackCalls = 0u;
        closeCallbackCalls = 0u;

        creator.createArchive(setup);

        ASSERT_TRUE(util::file_exists("splitzip_archive_part000.zip"));
        ASSERT_TRUE(util::delete_file("splitzip_archive_part000.zip"));
        ASSERT_EQ(openCallbackCalls, 1);
        ASSERT_EQ(closeCallbackCalls, 0);

        creator.closeArchive();

        ASSERT_EQ(openCallbackCalls, 1);
        ASSERT_EQ(closeCallbackCalls, 1);
    }
}

TEST(mvlc_listfile_zip, Split_SplitBySize)
{
    ASSERT_FALSE(util::file_exists("splitzip_archive_part000.zip"));
    ASSERT_FALSE(util::file_exists("splitzip_archive_part001.zip"));
    ASSERT_FALSE(util::file_exists("splitzip_archive_part002.zip"));

    size_t openCallbackCalls = 0u;
    size_t closeCallbackCalls = 0u;

    auto  open_callback = [&openCallbackCalls] (SplitZipCreator *creator)
    {
        ASSERT_TRUE(creator != nullptr);
        ++openCallbackCalls;
    };

    auto  close_callback = [&closeCallbackCalls] (SplitZipCreator *creator)
    {
        ASSERT_TRUE(creator != nullptr);
        ++closeCallbackCalls;
    };

    SplitZipCreator creator;
    SplitListfileSetup setup;
    setup.splitMode = ZipSplitMode::SplitBySize;
    setup.splitSize = 1024;
    setup.entryType = ZipEntryInfo::ZIP;
    setup.compressLevel = 0; // Disable compression to make sure the raw bytes written end up in the file.
    setup.filenamePrefix = "splitzip_archive";
    setup.openArchiveCallback = open_callback;
    setup.closeArchiveCallback = close_callback;

    std::vector<u8> chunk(setup.splitSize);

    for (size_t i=0; i<chunk.size(); i++)
        chunk[i] = i % 255u;

    creator.createArchive(setup);
    auto wh = creator.createListfileEntry();

    ASSERT_TRUE(wh != nullptr);
    ASSERT_TRUE(creator.hasOpenEntry());
    ASSERT_TRUE(creator.isSplitEntry());
    ASSERT_EQ(openCallbackCalls, 1);

    for (size_t cycle=0; cycle<3; ++cycle)
    {
        wh->write(chunk.data(), chunk.size());
        cout << fmt::format("cycle={}, entryInfo.bytesWrittenToFile={}\n",
                            cycle, creator.entryInfo().bytesWrittenToFile());
    }

    creator.closeArchive();

    ASSERT_EQ(openCallbackCalls, 3);
    ASSERT_EQ(closeCallbackCalls, 3);

    ASSERT_TRUE(util::file_exists("splitzip_archive_part000.zip"));
    ASSERT_TRUE(util::file_exists("splitzip_archive_part001.zip"));
    ASSERT_TRUE(util::file_exists("splitzip_archive_part002.zip"));

    ASSERT_TRUE(util::delete_file("splitzip_archive_part000.zip"));
    ASSERT_TRUE(util::delete_file("splitzip_archive_part001.zip"));
    ASSERT_TRUE(util::delete_file("splitzip_archive_part002.zip"));
}

#if 0
TEST(mvlc_listfile_zip, MinizipCreate)
{
    const std::vector<u8> outData0 = { 0x12, 0x34, 0x56, 0x78 };
    const std::vector<u8> outData1 = { 0xff, 0xfe, 0xfd, 0xdc };

    std::string archiveName = "mvlc_listfile_zip.test.MinizipCreateWriteRead.zip";
    void *osStream = nullptr;
    void *zipWriter = nullptr;
    s32 mzMode = MZ_OPEN_MODE_WRITE | MZ_OPEN_MODE_CREATE;

    mz_stream_os_create(&osStream);
    mz_zip_writer_create(&zipWriter);
    mz_zip_writer_set_compress_method(zipWriter, MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(zipWriter, 1);
    mz_zip_writer_set_follow_links(zipWriter, true);

    if (auto err = mz_stream_os_open(osStream, archiveName.c_str(), mzMode))
        throw std::runtime_error("mz_stream_os_open: " + std::to_string(err));

    if (auto err = mz_zip_writer_open(zipWriter, osStream))
        throw std::runtime_error("mz_zip_writer_open: " + std::to_string(err));

    // outfile0
    {
        std::string m_entryName = "outfile0.data";

        mz_zip_file file_info = {};
        file_info.filename = m_entryName.c_str();
        file_info.modified_date = time(nullptr);
        file_info.version_madeby = MZ_VERSION_MADEBY;
        file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
        file_info.zip64 = MZ_ZIP64_FORCE;
        file_info.external_fa = (S_IFREG) | (0644u << 16);

        if (auto err = mz_zip_writer_entry_open(zipWriter, &file_info))
            throw std::runtime_error("mz_zip_writer_entry_open: " + std::to_string(err));

        {
            s32 bytesWritten = mz_zip_writer_entry_write(zipWriter, outData0.data(), outData0.size());

            if (bytesWritten < 0)
                throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));

            if (static_cast<size_t>(bytesWritten) != outData0.size())
                throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));

        }

        {
            s32 bytesWritten = mz_zip_writer_entry_write(zipWriter, outData0.data(), outData0.size());

            if (bytesWritten < 0)
                throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));

            if (static_cast<size_t>(bytesWritten) != outData0.size())
                throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));
        }

        if (auto err = mz_zip_writer_entry_close(zipWriter))
            throw std::runtime_error("mz_zip_writer_entry_close: " + std::to_string(err));
    }

#if 1
    // outfile1
    {
        std::string m_entryName = "outfile1.data";

        mz_zip_file file_info = {};
        file_info.filename = m_entryName.c_str();
        file_info.modified_date = time(nullptr);
        file_info.version_madeby = MZ_VERSION_MADEBY;
        file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
        file_info.zip64 = MZ_ZIP64_FORCE;
        file_info.external_fa = (S_IFREG) | (0644u << 16);

        if (auto err = mz_zip_writer_entry_open(zipWriter, &file_info))
            throw std::runtime_error("mz_zip_writer_entry_open: " + std::to_string(err));

        s32 bytesWritten = mz_zip_writer_entry_write(zipWriter, outData1.data(), outData1.size());

        if (bytesWritten < 0)
            throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));

        if (static_cast<size_t>(bytesWritten) != outData1.size())
            throw std::runtime_error("mz_zip_writer_entry_write: " + std::to_string(bytesWritten));

        if (auto err = mz_zip_writer_entry_close(zipWriter))
            throw std::runtime_error("mz_zip_writer_entry_close: " + std::to_string(err));
    }
#endif

    if (auto err = mz_zip_writer_close(zipWriter))
        throw std::runtime_error("mz_zip_writer_close: " + std::to_string(err));

    if (auto err = mz_stream_os_close(osStream))
        throw std::runtime_error("mz_stream_os_close: " + std::to_string(err));

    mz_zip_writer_delete(&zipWriter);
    mz_stream_os_delete(&osStream);

}
#endif

#if 0
namespace
{

std::string to_string(const ZipEntryInfo::Type t)
{
    switch (t)
    {
        case ZipEntryInfo::ZIP:
            return "zip";

        case ZipEntryInfo::LZ4:
            return "lz4";
    }

    return {};
}

}

TEST(mvlc_listfile_zip, ListfileCreateLarge)
{

#if 0
    cout << "generating unfiformly distributed data" << endl;
    std::vector<u32> outData0(Megabytes(500) / sizeof(u32));

    {
        std::random_device rd;
        std::default_random_engine engine(rd());
        std::uniform_int_distribution<unsigned> dist(0u, 255u);
        for (auto &c: outData0)
            c = static_cast<u32>(dist(engine));
    }
    outData0[0] = 0xaffe;
#elif 0
    cout << "generating sequential data" << endl;
    std::vector<u32> outData0(Megabytes(1) / sizeof(u32));
    {
        for (size_t i=0; i<outData0.size(); i++)
            outData0[i] = i;
    }
    assert(outData0[0] == 0);
    assert(outData0[1] == 1);
    assert(outData0[2] == 2);
    outData0[0] = 0xfff0; // XXX
    outData0[1] = 0xfff1; // XXX
    outData0[2] = 0xfff2; // XXX
    outData0[3] = 0xfff3; // XXX
    outData0[4] = 0xfff4; // XXX
    outData0[5] = 0xfff5; // XXX
#elif 1
    std::vector<u32> outData0(Megabytes(100) / sizeof(u32));

    // generate events
    {
        auto out = std::begin(outData0);
        auto end = std::end(outData0);

        struct end_of_buffer {};

        auto put = [&out, &end] (u32 value)
        {
            if (out >= end)
                throw end_of_buffer();
            *out++ = value;
        };

        cout << "generating simulated data" << endl;

        u32 eventNumber = 0;

        try
        {
            const unsigned ChannelCount = 32;
            const unsigned ChannelDataBits = 13;
            const unsigned ChannelNumberShift = 24;
            const u32 Header = 0xA0000012u; // some thought up header value
            const u32 Footer = 0xC0000000u;

            //std::random_device rd;
            //std::default_random_engine engine(rd());
            std::default_random_engine engine(1337);
            std::uniform_int_distribution<u16> dist(0u, (1u << ChannelDataBits) - 1 );

            while (true)
            {
                put(Header);

                for (unsigned chan=0; chan<ChannelCount; chan++)
                    put((chan << ChannelNumberShift) | dist(engine));

                put(Footer | (eventNumber++ & 0xFFFFFF));
            }
        }
        catch (const end_of_buffer &)
        { }

        cout << "done generating data for " << eventNumber << " events" << endl;
    }
#endif

    const std::string archiveName = "mvlc_listfile_zip.test.ListfileCreateLarge.zip";

    const size_t totalBytesToWrite = outData0.size() * sizeof(u32);
    const size_t totalBytesToRead = outData0.size() * sizeof(u32);
    const bool useLZ4 = true;

    // write
    {
        size_t totalBytes = 0u;
        size_t writeCount = 0;
        ZipEntryInfo entryInfo = {};

        auto tStart = std::chrono::steady_clock::now();

        {
            ZipCreator creator;
            creator.createArchive(archiveName);
            auto &writeHandle = (useLZ4
                                 ? *creator.createLZ4Entry("outfile0.data", 0)
                                 : *creator.createZIPEntry("outfile0.data", 1));

            do
            {
                auto raw = reinterpret_cast<const u8 *>(outData0.data());
                auto bytes = outData0.size() * sizeof(outData0.at(0));
                size_t bytesWritten = writeHandle.write(raw, bytes);
                totalBytes += bytesWritten;
                ++writeCount;
            } while (totalBytes < totalBytesToWrite);

            creator.closeCurrentEntry();

            entryInfo = creator.entryInfo();
        }

        ASSERT_EQ(totalBytes, totalBytesToWrite);

        auto tEnd = std::chrono::steady_clock::now();
        auto elapsed = tEnd - tStart;
        auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
        auto megaBytesPerSecond = (totalBytes / (1024 * 1024)) / seconds;

        cout << "Wrote the listfile in " << writeCount << " iterations, totalBytes=" << totalBytes << endl;
        cout << "Writing took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <<  " ms" << endl;
        cout << "rate: " << megaBytesPerSecond << " MB/s" << endl;
        cout << "EntryInfo: bytesWritten=" << entryInfo.bytesWritten
            << ", lz4CompressedBytesWritten=" << entryInfo.lz4CompressedBytesWritten
            << ", ratio=" << (entryInfo.bytesWritten / static_cast<double>(entryInfo.lz4CompressedBytesWritten))
            << endl;
    }

    // read
    {
        std::vector<u32> readBuffer(outData0.size());
        size_t bytesRead = 0u;
        ZipEntryInfo entryInfo = {};

        auto tStart = std::chrono::steady_clock::now();

        {
            ZipReader reader;
            reader.openArchive(archiveName);

            auto entryNameList = reader.entryNameList();

            ASSERT_EQ(entryNameList.size(), 1u);

            const std::string entryName = (useLZ4
                                           ? "outfile0.data.lz4"
                                           : "outfile0.data");

            cout << "reading from entryName=" << entryName << endl;

            ASSERT_EQ(entryNameList[0], entryName);

            auto &readHandle = *reader.openEntry(entryName);

            bytesRead = readHandle.read(
                reinterpret_cast<u8 *>(readBuffer.data()), readBuffer.size() * sizeof(u32));

            entryInfo = reader.entryInfo();

            ASSERT_EQ(bytesRead, totalBytesToRead);
        }

        auto tEnd = std::chrono::steady_clock::now();
        auto elapsed = tEnd - tStart;
        auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
        auto megaBytesPerSecond = (bytesRead / (1024 * 1024)) / seconds;


        cout << "Read " << bytesRead << " bytes in "
            << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << " ms" << endl;
        cout << "rate: " << megaBytesPerSecond << " MB/s" << endl;
        cout << "EntryInfo: bytesRead=" << entryInfo.bytesRead
            << ", lz4CompressedBytesRead=" << entryInfo.lz4CompressedBytesRead
            << ", ratio=" << (entryInfo.bytesRead / static_cast<double>(entryInfo.lz4CompressedBytesRead))
            << endl;

        ASSERT_EQ(outData0.size(), readBuffer.size());
        ASSERT_EQ(outData0, readBuffer);
    }
}
#endif

#if 0
TEST(mvlc_listfile_zip, CompressionTests)
{
    struct CompressionConfig
    {
        ZipEntryInfo::Type type = ZipEntryInfo::ZIP;
        int level = 0;
    };

    struct CompressResult
    {
        CompressionConfig cfg;
        std::string archiveName;
        size_t totalBytesWritten = 0u;
        size_t writeCount = 0u;
        size_t lz4CompressedBytes = 0u;
        std::chrono::steady_clock::time_point tStart;
        std::chrono::steady_clock::time_point tEnd;
    };

    struct DecompressResult
    {
        CompressionConfig cfg;
        std::string archiveName;
        size_t totalBytesRead = 0u;
        size_t readCount = 0u;
        size_t zipCompressedBytes = 0u;
        size_t zipUncompressedBytes = 0u;
        size_t lz4CompressedBytes = 0u;
        std::chrono::steady_clock::time_point tStart;
        std::chrono::steady_clock::time_point tEnd;
    };

    // Compression configs to test
    const std::vector<CompressionConfig> configs =
    {
        { ZipEntryInfo::ZIP, 0 }, // store
        { ZipEntryInfo::ZIP, 1 }, // superfast
        { ZipEntryInfo::ZIP, 2 },

        { ZipEntryInfo::LZ4, 0 }, // lz4 default
        { ZipEntryInfo::LZ4, 1 },
        { ZipEntryInfo::LZ4, 2 },
        { ZipEntryInfo::LZ4, 3 },
        { ZipEntryInfo::LZ4, 4 },
        { ZipEntryInfo::LZ4, 5 },
        { ZipEntryInfo::LZ4, 6 },
        { ZipEntryInfo::LZ4, 7 },
        { ZipEntryInfo::LZ4, 8 },
        { ZipEntryInfo::LZ4, 9 },

        { ZipEntryInfo::LZ4, -1 }, // accelerated
        { ZipEntryInfo::LZ4, -2 }, // accelerated
        { ZipEntryInfo::LZ4, -3 }, // accelerated
    };

    // Space available to fill with randomized events.
    static const size_t EventBufferSize = Megabytes(100);
    // Total bytes that should be written to the output listfile
    static const size_t ListfileDataSize = Megabytes(256);

    std::vector<u32> eventData(Megabytes(100) / sizeof(u32));
    std::vector<u32> readBuffer(eventData.size());

    // generate events
    {
        auto out = std::begin(eventData);
        auto end = std::end(eventData);

        struct end_of_buffer {};

        auto put = [&out, &end] (u32 value)
        {
            if (out >= end)
                throw end_of_buffer();
            *out++ = value;
        };

        cout << "generating simulated data" << endl;

        u32 eventNumber = 0;

        try
        {
            const unsigned ChannelCount = 16;
            const unsigned ChannelDataBits = 13;
            const unsigned ChannelNumberShift = 24;
            const u32 Header = 0xA0000012u; // some thought up header value
            const u32 Footer = 0xC0000000u;

            //std::random_device rd;
            //std::default_random_engine engine(rd());
            std::default_random_engine engine(1337);
            std::uniform_int_distribution<u16> dist(0u, (1u << ChannelDataBits) - 1 );

            while (true)
            {
                put(Header);

                for (unsigned chan=0; chan<ChannelCount; chan++)
                    put((chan << ChannelNumberShift) | dist(engine));

                put(Footer | (eventNumber++ & 0xFFFFFF));
            }
        }
        catch (const end_of_buffer &)
        { }

        cout << "done generating data for " << eventNumber << " events. first 50 words:" << endl;
        util::log_buffer(cout, basic_string_view<const u32>(
                eventData.data(), std::min(eventData.size(), (size_t)50u)));
    }

    // ----- write -----
    std::vector<CompressResult> compressResults;

    for (const auto &cfg: configs)
    {
        const std::string archiveName = fmt::format(
            "compression-test-{}_{}.zip", to_string(cfg.type), cfg.level);

        cout << "writing archiveName=" << archiveName << endl;

        // write listfile
        size_t totalBytesWritten = 0u;
        size_t writeCount = 0u;
        ZipEntryInfo entryInfo = {};

        auto tStart = std::chrono::steady_clock::now();
        {
            ZipCreator creator;
            creator.createArchive(archiveName);
            auto &writeHandle = *(cfg.type == ZipEntryInfo::LZ4
                                 ? creator.createLZ4Entry("testdata.data", cfg.level)
                                 : creator.createZIPEntry("testdata.data", cfg.level));

            while (totalBytesWritten < ListfileDataSize)
            {
                auto raw = reinterpret_cast<const u8 *>(eventData.data());
                auto bytes = eventData.size() * sizeof(eventData.at(0));
                size_t bytesWritten = writeHandle.write(raw, bytes);
                totalBytesWritten += bytesWritten;
                ++writeCount;
            }

            entryInfo = creator.entryInfo();
        }
        auto tEnd = std::chrono::steady_clock::now();

#if 0
        auto elapsed = tEnd - tStart;
        auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
        auto megaBytesPerSecond = (totalBytesWritten / (1024 * 1024)) / seconds;

        cout << "Wrote " << archiveName << " in " << writeCount << " iterations, totalBytesWritten=" << totalBytesWritten << endl;
        cout << "Writing took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <<  " ms" << endl;
        cout << "rate: " << megaBytesPerSecond << " MB/s" << endl;
        cout << "EntryInfo: bytesWritten=" << entryInfo.bytesWritten
            << ", lz4CompressedBytesWritten=" << entryInfo.lz4CompressedBytesWritten
            << ", ratio=" << (entryInfo.bytesWritten / static_cast<double>(entryInfo.lz4CompressedBytesWritten))
            << endl;
#endif

        CompressResult result;
        result.cfg = cfg;
        result.archiveName = archiveName;
        result.totalBytesWritten = totalBytesWritten;
        result.writeCount = writeCount;
        // TODO: zipCompressedBytes FIXME can't currently get the zip uncompressed_bytes and compressed_bytes fields from the creator
        result.lz4CompressedBytes = entryInfo.lz4CompressedBytesWritten;
        result.tStart = tStart;
        result.tEnd = tEnd;

        compressResults.emplace_back(result);
    }

    cout << endl;

    std::vector<DecompressResult> decompressResults;

    // ----- read -----
    for (const auto &cr: compressResults)
    {
        DecompressResult dcr;
        dcr.archiveName = cr.archiveName;
        dcr.cfg = cr.cfg;

        cout << "reading archiveName=" << dcr.archiveName << endl;

        size_t totalBytesRead = 0u;
        size_t readCount = 0u;
        ZipEntryInfo zipEntryInfo = {};

        auto tStart = std::chrono::steady_clock::now();
        {
            ZipReader reader;
            reader.openArchive(dcr.archiveName);

            auto entryNameList = reader.entryNameList();
            ASSERT_EQ(entryNameList.size(), 1u);

            const std::string entryName = (dcr.cfg.type == ZipEntryInfo::LZ4
                                           ? "testdata.data.lz4"
                                           : "testdata.data");

            ASSERT_EQ(entryNameList[0], entryName);

            auto &readHandle = *reader.openEntry(entryName);
            zipEntryInfo = reader.entryInfo();
            size_t bytesRead = 0u;

            do
            {
                bytesRead = readHandle.read(
                    reinterpret_cast<u8 *>(readBuffer.data()), readBuffer.size() * sizeof(u32));
                totalBytesRead += bytesRead;
                ++readCount;
            } while (bytesRead > 0);
        }
        auto tEnd = std::chrono::steady_clock::now();

        dcr.totalBytesRead = totalBytesRead;
        dcr.readCount = readCount;
        dcr.zipCompressedBytes = zipEntryInfo.compressedSize;
        dcr.zipUncompressedBytes = zipEntryInfo.uncompressedSize;
        dcr.tStart = tStart;
        dcr.tEnd = tEnd;

        decompressResults.emplace_back(dcr);
    }

    // ----- output -----
    auto rate_MBps = [] (const auto &tStart, const auto &tEnd, size_t totalBytes) -> double
    {
        auto elapsed = tEnd - tStart;
        auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
        return (totalBytes / (1024 * 1024)) / seconds;
    };

    const char *const headerFormat = "{:30} {:8} {:<5} {:15} {:15} {:20} {:20} {:20} {:20} {:10} {:10} {:10}";
    const char *const format       = "{:30} {:8} {:<5} {:<15.2f} {:<15.2f} {:<20} {:<20} {:<20} {:<20} {:<10.2f} {:<10} {:<10}";
    cout << fmt::format(headerFormat,
                        "archive", "type", "level", "writeRate[MB/s]", "readRate[MB/s]",
                        "bytesWritten", "bytesRead", "zipCompressed", "zipUncompressed", "ratio", "writeCount", "readCount") << endl;

    auto itcr = std::begin(compressResults);
    auto itdcr = std::begin(decompressResults);

    auto endcr = std::end(compressResults);
    auto enddcr = std::end(decompressResults);

    while (itcr != endcr && itdcr != enddcr)
    {
        const CompressResult &cr = *itcr;
        const DecompressResult &dcr = *itdcr;

        auto writeRate = rate_MBps(cr.tStart, cr.tEnd, cr.totalBytesWritten);
        auto readRate = rate_MBps(dcr.tStart, dcr.tEnd, dcr.totalBytesRead);

        cout << fmt::format(format,
                            cr.archiveName,
                            to_string(cr.cfg.type),
                            cr.cfg.level,
                            writeRate,
                            readRate,
                            cr.totalBytesWritten,
                            dcr.totalBytesRead,
                            dcr.zipCompressedBytes,
                            dcr.zipUncompressedBytes,
                            // Ratio of input size to what's actually stored in the archive. This
                            // includes LZ4 framing overhead.
                            static_cast<double>(dcr.zipCompressedBytes) / cr.totalBytesWritten,
                            cr.writeCount,
                            dcr.readCount
                            )
            << endl;

        itcr++;
        itdcr++;
    }

    return;

#if 0
    const std::string archiveName = "mvlc_listfile_zip.test.ListfileCreateLarge.zip";

    const size_t totalBytesToWrite = eventData.size() * sizeof(u32);
    const size_t totalBytesToRead = eventData.size() * sizeof(u32);
    const bool useLZ4 = true;

    // write
    {
        size_t totalBytes = 0u;
        size_t writeCount = 0;
        ZipEntryInfo entryInfo = {};

        auto tStart = std::chrono::steady_clock::now();

        {
            ZipCreator creator;
            creator.createArchive(archiveName);
            auto &writeHandle = (useLZ4
                                 ? *creator.createLZ4Entry("outfile0.data", 0)
                                 : *creator.createZIPEntry("outfile0.data", 1));

            do
            {
                auto raw = reinterpret_cast<const u8 *>(eventData.data());
                auto bytes = eventData.size() * sizeof(eventData.at(0));
                size_t bytesWritten = writeHandle.write(raw, bytes);
                totalBytes += bytesWritten;
                ++writeCount;
            } while (totalBytes < totalBytesToWrite);

            creator.closeCurrentEntry();

            entryInfo = creator.entryInfo();
        }

        ASSERT_EQ(totalBytes, totalBytesToWrite);

        auto tEnd = std::chrono::steady_clock::now();
        auto elapsed = tEnd - tStart;
        auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
        auto megaBytesPerSecond = (totalBytes / (1024 * 1024)) / seconds;

        cout << "Wrote the listfile in " << writeCount << " iterations, totalBytes=" << totalBytes << endl;
        cout << "Writing took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() <<  " ms" << endl;
        cout << "rate: " << megaBytesPerSecond << " MB/s" << endl;
        cout << "EntryInfo: bytesWritten=" << entryInfo.bytesWritten
            << ", lz4CompressedBytesWritten=" << entryInfo.lz4CompressedBytesWritten
            << ", ratio=" << (entryInfo.bytesWritten / static_cast<double>(entryInfo.lz4CompressedBytesWritten))
            << endl;
    }

    // read
    {
        std::vector<u32> readBuffer(eventData.size());
        size_t bytesRead = 0u;
        ZipEntryInfo entryInfo = {};

        auto tStart = std::chrono::steady_clock::now();

        {
            ZipReader reader;
            reader.openArchive(archiveName);

            auto entryNameList = reader.entryNameList();

            ASSERT_EQ(entryNameList.size(), 1u);

            const std::string entryName = (useLZ4
                                           ? "outfile0.data.lz4"
                                           : "outfile0.data");

            cout << "reading from entryName=" << entryName << endl;

            ASSERT_EQ(entryNameList[0], entryName);

            auto &readHandle = *reader.openEntry(entryName);

            bytesRead = readHandle.read(
                reinterpret_cast<u8 *>(readBuffer.data()), readBuffer.size() * sizeof(u32));

            entryInfo = reader.entryInfo();

            ASSERT_EQ(bytesRead, totalBytesToRead);
        }

        auto tEnd = std::chrono::steady_clock::now();
        auto elapsed = tEnd - tStart;
        auto seconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 1000.0;
        auto megaBytesPerSecond = (bytesRead / (1024 * 1024)) / seconds;


        cout << "Read " << bytesRead << " bytes in "
            << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << " ms" << endl;
        cout << "rate: " << megaBytesPerSecond << " MB/s" << endl;
        cout << "EntryInfo: bytesRead=" << entryInfo.bytesRead
            << ", lz4CompressedBytesRead=" << entryInfo.lz4CompressedBytesRead
            << ", ratio=" << (entryInfo.bytesRead / static_cast<double>(entryInfo.lz4CompressedBytesRead))
            << endl;

        ASSERT_EQ(eventData.size(), readBuffer.size());
        ASSERT_EQ(eventData, readBuffer);
    }
#endif
}
#endif
