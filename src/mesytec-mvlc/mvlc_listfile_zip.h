#ifndef __MESYTEC_MVLC_MVLC_LISTFILE_ZIP_H__
#define __MESYTEC_MVLC_MVLC_LISTFILE_ZIP_H__

#include <memory>
#include "mvlc_listfile.h"
#include "mesytec-mvlc_export.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

struct ZipEntryInfo
{
    enum Type { ZIP, LZ4 };

    Type type = ZIP;
    std::string name;
    bool isOpen = false;

    // raw number of bytes written
    size_t bytesWritten = 0u;

    // bytes written after lz4 compression
    size_t lz4CompressedBytesWritten = 0u;

    // total number of bytes read
    size_t bytesRead = 0u;

    // bytes of compressed LZ4 data read
    size_t lz4CompressedBytesRead = 0u;

    size_t compressedSize = 0u;
    size_t uncompressedSize = 0u;
};

class ZipEntryWriteHandle;

class MESYTEC_MVLC_EXPORT ZipCreator
{
    public:
        ZipCreator();
        ~ZipCreator();

        void createArchive(const std::string &zipFilename);
        void closeArchive();
        bool isOpen() const;

        ZipEntryWriteHandle *createZIPEntry(const std::string &entryName, int compressLevel);

        ZipEntryWriteHandle *createZIPEntry(const std::string &entryName)
        { return createZIPEntry(entryName, 1); } // 1: "super fast compression", 0: store/no compression

        ZipEntryWriteHandle *createLZ4Entry(const std::string &entryName, int compressLevel);

        ZipEntryWriteHandle *createLZ4Entry(const std::string &entryName)
        { return createLZ4Entry(entryName, 0); }; // 0: lz4 default compression

        bool hasOpenEntry() const;
        const ZipEntryInfo &entryInfo() const;

        size_t writeToCurrentEntry(const u8 *data, size_t size);

        void closeCurrentEntry();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

class MESYTEC_MVLC_EXPORT ZipEntryWriteHandle: public WriteHandle
{
    public:
        ~ZipEntryWriteHandle() override;
        size_t write(const u8 *data, size_t size) override;

    private:
        friend class ZipCreator;
        explicit ZipEntryWriteHandle(ZipCreator *creator);
        ZipCreator *m_zipCreator = nullptr;
};

class ZipReader;

class MESYTEC_MVLC_EXPORT ZipReadHandle: public ReadHandle
{
    public:
        ZipReadHandle(ZipReader *reader)
            : m_zipReader(reader)
        { }

        size_t read(u8 *dest, size_t maxSize) override;
        void seek(size_t pos) override;

    private:
        ZipReader *m_zipReader = nullptr;
};

class MESYTEC_MVLC_EXPORT ZipReader
{
    public:
        ZipReader();
        ~ZipReader();

        void openArchive(const std::string &archiveName);
        void closeArchive();
        std::vector<std::string> entryNameList();

        ZipReadHandle *openEntry(const std::string &name);
        ZipReadHandle *currentEntry();
        void closeCurrentEntry();
        size_t readCurrentEntry(u8 *dest, size_t maxSize);
        std::string currentEntryName() const;
        const ZipEntryInfo &entryInfo() const;

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_LISTFILE_ZIP_H__ */
