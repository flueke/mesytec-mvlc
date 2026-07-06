#ifndef __MESYTEC_MVLC_MVLC_LISTFILE_ZIP_H__
#define __MESYTEC_MVLC_MVLC_LISTFILE_ZIP_H__

#include <functional>
#include <memory>
#include "mesytec-mvlc/mvlc_listfile.h"
#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include <variant>

namespace mesytec::mvlc::listfile
{

struct MESYTEC_MVLC_EXPORT ZipEntryInfo
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

    size_t bytesWrittenToFile() const
    {
        if (type == ZIP)
            return bytesWritten;
        return lz4CompressedBytesWritten;
    }
};

enum class OverwriteMode
{
    DontOverwrite,
    Overwrite
};

//
// ZipCreator
//

// Everything throws on error.
class MESYTEC_MVLC_EXPORT ZipCreator
{
    public:

        ZipCreator();
        ~ZipCreator();

        void createArchive(const std::string &zipFilename,
                           const OverwriteMode &mode = OverwriteMode::DontOverwrite);
        void closeArchive();
        bool isOpen() const;
        std::string archiveName() const;

        std::unique_ptr<WriteHandle> createZIPEntry(const std::string &entryName, int compressLevel);

        std::unique_ptr<WriteHandle> createZIPEntry(const std::string &entryName)
        { return createZIPEntry(entryName, 1); } // 1: "super fast compression", 0: store/no compression

        std::unique_ptr<WriteHandle> createLZ4Entry(const std::string &entryName, int compressLevel);

        std::unique_ptr<WriteHandle> createLZ4Entry(const std::string &entryName)
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

//
// SplitZipCreator
//

class SplitZipCreator;

enum class ZipSplitMode { DontSplit, SplitBySize, SplitByTime };

// Callback invoked after a new archive is created either initially trough
// createArchive() or automatically due to an archive split.
using OpenArchiveCallback = std::function<void (SplitZipCreator *creator)>;

// Callback function prototype invoked prior to closing the current
// archive (either via closeArchive() or due to an archive split).
// Can be used to add additional non-split entries to the archive via
// createZIPEntry() and createLZ4Entry().
// Important: do not call createListfileEntry() from this callback!
using CloseArchiveCallback = std::function<void (SplitZipCreator *creator)>;

struct MESYTEC_MVLC_EXPORT SplitListfileSetup
{
    ZipEntryInfo::Type entryType = ZipEntryInfo::ZIP;
    int compressLevel = 0;
    OverwriteMode overwriteMode = OverwriteMode::DontOverwrite;
    ZipSplitMode splitMode = ZipSplitMode::DontSplit;
    size_t splitSize = util::Gigabytes(1);
    std::chrono::seconds splitTime = std::chrono::seconds(3600);
    // Archive filename and archive member name prefix.
    // The complete resulting filename is: <prefix>_part<NNN>.zip
    // The complete listfile member name is <prefix>_part<NNN>.mvmelst[.lz4]
    std::string filenamePrefix;
    std::string listfileExtension = ".mvlclst";
    // Preamble for the listfile. Will be written to each newly started
    // listfile part.
    std::vector<u8> preamble;

    // Called when an archive is created, either manually via createArchive()
    // or automatically due to the file splitting setup.
    OpenArchiveCallback openArchiveCallback;

    // Called upon closing the current archive either manually via
    // closeArchive() or automatically due to the file splitting setup.
    CloseArchiveCallback closeArchiveCallback;
};

class MESYTEC_MVLC_EXPORT SplitZipCreator
{
    public:
        SplitZipCreator();
        ~SplitZipCreator();

        void createArchive(const SplitListfileSetup &setup);
        void closeArchive();
        bool isOpen() const;
        std::string archiveName() const;

        // Normal archive entries. Writing to these will never cause an archive split.

        std::unique_ptr<WriteHandle> createZIPEntry(const std::string &entryName, int compressLevel);

        std::unique_ptr<WriteHandle> createZIPEntry(const std::string &entryName)
        { return createZIPEntry(entryName, 1); } // 1: "super fast compression", 0: store/no compression

        std::unique_ptr<WriteHandle> createLZ4Entry(const std::string &entryName, int compressLevel);

        std::unique_ptr<WriteHandle> createLZ4Entry(const std::string &entryName)
        { return createLZ4Entry(entryName, 0); }; // 0: lz4 default compression

        // Special method for creating a (split) listfile entry. Uses the
        // information set in createArchive() to make the splitting decision
        // and create new partial archives.
        std::unique_ptr<WriteHandle> createListfileEntry();

        bool hasOpenEntry() const;
        const ZipEntryInfo &entryInfo() const;
        size_t writeToCurrentEntry(const u8 *data, size_t size);
        void closeCurrentEntry();
        bool isSplitEntry() const;

        ZipCreator *getZipCreator();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

template<typename ZipCreator, typename Container>
void add_file_to_archive(ZipCreator *zipCreator, const std::string &filename, const Container &data)
{
    auto writeHandle = zipCreator->createZIPEntry(filename, 0); // uncompressed zip entry
    writeHandle->write(reinterpret_cast<const u8 *>(data.data()), data.size());
    zipCreator->closeCurrentEntry();
}

class MESYTEC_MVLC_EXPORT SplitZipWriteHandle: public WriteHandle
{
    public:
        ~SplitZipWriteHandle() override;
        size_t write(const u8 *data, size_t size) override;

    private:
        friend class SplitZipCreator;
        explicit SplitZipWriteHandle(SplitZipCreator *creator);
        // non-owning pointer to the parent SplitZipCreator
        SplitZipCreator *creator_ = nullptr;
};

class ZipReader;

class MESYTEC_MVLC_EXPORT ZipReadHandle: public ReadHandle
{
    public:
        explicit ZipReadHandle(ZipReader *reader)
            : m_zipReader(reader)
        { }

        // Reads maxSize bytes into dest. Returns the number of bytes read.
        size_t read(u8 *dest, size_t maxSize) override;

        // Seeks from the beginning of the current entry to the given pos.
        // Returns the position that could be reached before EOF which can be
        // less than the desired position.
        size_t seek(size_t pos) override;

    private:
        ZipReader *m_zipReader = nullptr;
};

class MESYTEC_MVLC_EXPORT ZipReader
{
    public:
        ZipReader();
        ~ZipReader();

        ZipReader(ZipReader &&);
        ZipReader &operator=(ZipReader &&);

        void openArchive(const std::string &archiveName);
        void closeArchive();
        std::vector<std::string> entryNameList();

        ZipReadHandle *openEntry(const std::string &name);
        ZipReadHandle *currentEntry();
        void closeCurrentEntry();
        size_t readCurrentEntry(u8 *dest, size_t maxSize);
        std::string currentEntryName() const;
        const ZipEntryInfo &entryInfo() const;

        std::string firstListfileEntryName();

    private:
        struct Private;
        std::unique_ptr<Private> d;
};

std::string MESYTEC_MVLC_EXPORT next_archive_name(const std::string currentArchiveName);

class SplitZipReader;

class MESYTEC_MVLC_EXPORT SplitZipReadHandle: public ReadHandle
{
    public:
        explicit SplitZipReadHandle(SplitZipReader *reader)
            : m_zipReader(reader)
        { }

        size_t read(u8 *dest, size_t maxSize) override;
        size_t seek(size_t pos) override;

    private:
        SplitZipReader *m_zipReader = nullptr;
};

class MESYTEC_MVLC_EXPORT SplitZipReader
{
    public:
        // Invoked when the currently open archives changes, either due to a
        // call to openArchive() or because the next part has been opened
        // internally.
        using ArchiveChangedCallback = std::function<void (SplitZipReader *, const std::string &archiveName)>;

        SplitZipReader();
        ~SplitZipReader();

        void openArchive(const std::string &archiveName);
        void closeArchive();
        std::vector<std::string> entryNameList();

        // open a non-split entry
        ZipReadHandle *openEntry(const std::string &name);
        ZipReadHandle *currentEntry();
        void closeCurrentEntry();
        size_t readCurrentEntry(u8 *dest, size_t maxSize);
        std::string currentEntryName() const;
        const ZipEntryInfo &entryInfo() const;

        std::string firstListfileEntryName();

        // open the (split) listfile entry
        SplitZipReadHandle *openFirstListfileEntry();

        void setArchiveChangedCallback(ArchiveChangedCallback cb);

    private:
        friend class SplitZipReadHandle;
        size_t seek(size_t pos);
        struct Private;
        std::unique_ptr<Private> d;
};

// A small utility for safely updating files in ZIP archives. Main use case is
// replacing mvme .analysis files in existing listmode ZIPs.

struct MESYTEC_MVLC_EXPORT ZipOperationBase
{
    // Filename inside the ZIP archive.
    std::string filename;
    // Optional human-readable description for logging/UI later:
    std::optional<std::string> description;
};

struct MESYTEC_MVLC_EXPORT AddOperation: public ZipOperationBase { std::vector<uint8_t> contents; };
struct MESYTEC_MVLC_EXPORT UpdateOperation: public ZipOperationBase { std::vector<uint8_t> contents; };
struct MESYTEC_MVLC_EXPORT DeleteOperation: public ZipOperationBase {};

using ZipOperation = std::variant<AddOperation, UpdateOperation, DeleteOperation>;

struct MESYTEC_MVLC_EXPORT ProgressEvent
{
    std::string step; // e.g. "copying_listmode", "adding_analysis", "replacing_file"
    double progress;  // 0.0–1.0 for the current step
    std::optional<uint64_t> bytes_copied;
    std::optional<uint64_t> bytes_total;
};

using ProgressCallback = std::function<void(const ProgressEvent &)>;
using CancelCallback = std::function<bool()>; // returns true if user requested cancel

struct MESYTEC_MVLC_EXPORT ZipUpdateConfig
{
    std::string input_zip_path;             // e.g. "/data/run123/listmode.zip"
    std::vector<ZipOperation> ops;          // 1–N operations on the same ZIP

    // Want a bit more free space than the size of the input archive as we copy
    // the data instead of modifying the input archive directly.
    double min_available_space_ratio = 1.2;

    // Optional callbacks for progress reporting and cancellation. If
    // is_cancelled is not set cancellation is not possible.
    ProgressCallback on_progress;
    CancelCallback is_cancelled;
};

struct MESYTEC_MVLC_EXPORT ZipUpdateResult
{
    std::error_code ec;
    std::exception_ptr ex; // set if an exception was thrown during the update process
    std::string error_detail; // empty if !ec && !ex, otherwise animal-readable context
    std::vector<std::string> ops_descriptions; // what was done, for logging

    bool did_succeed() const { return !ec && !ex; }
    bool was_cancelled() const { return ec == std::errc::operation_canceled; }
    std::string error_message() const
    {
        return error_detail;
    }
};

// FIXME: two ways to report errors: ZipUpdateResult.ec and exceptions. The
// latter are thrown from ZipCreator/ZipReader and friends. Pretty messy to use as is.
MESYTEC_MVLC_EXPORT ZipUpdateResult update_zip_archive(const ZipUpdateConfig &config);

}

#endif /* __MESYTEC_MVLC_MVLC_LISTFILE_ZIP_H__ */
