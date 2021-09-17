#ifndef __MESYTEC_MVLC_MVLC_READOUT_H__
#define __MESYTEC_MVLC_MVLC_READOUT_H__

#include "mesytec-mvlc/mesytec-mvlc_export.h"

#include <chrono>
#include <future>
#include <string>

#include "mvlc.h"
#include "mvlc_listfile.h"
#include "mvlc_readout_config.h"
#include "mvlc_readout_parser.h"
#include "mvlc_readout_worker.h"

namespace mesytec
{
namespace mvlc
{

struct MESYTEC_MVLC_EXPORT ListfileParams
{
    enum class Compression { LZ4, ZIP };

    bool writeListfile = true;
    // name of the listfile zip archive
    std::string filepath = "./run_001.zip";
    // name of the listfile inside the zip archive
    std::string listfilename = "listfile";
    // if true overwrite existing archives
    bool overwrite = false;
    // compression for the listfile inside the zip archive
    Compression compression = Compression::LZ4;
    // compression level, the higher the better compression but also the slower
    int compressionLevel = 0;
};

class MESYTEC_MVLC_EXPORT MVLCReadout
{
    public:
        MVLCReadout(MVLCReadout &&other);
        MVLCReadout &operator=(MVLCReadout &&other);

        MVLCReadout(MVLCReadout &other) = delete;
        MVLCReadout &operator=(MVLCReadout &other) = delete;

        ~MVLCReadout();

        std::error_code start(const std::chrono::seconds &timeToRun = {});
        std::error_code stop();
        std::error_code pause();
        std::error_code resume();

        ReadoutWorker::State state() const;
        WaitableProtected<ReadoutWorker::State> &waitableState();
        ReadoutWorker::Counters workerCounters();
        readout_parser::ReadoutParserCounters parserCounters();

    private:
        MVLCReadout();

        struct Private;
        std::unique_ptr<Private> d;

        friend MVLCReadout make_mvlc_readout(
            const CrateConfig &crateConfig,
            const ListfileParams &listfileParams,
            readout_parser::ReadoutParserCallbacks parserCallbacks);

        friend MVLCReadout make_mvlc_readout(
            MVLC &mvlc,
            const CrateConfig &crateConfig,
            const ListfileParams &listfileParams,
            readout_parser::ReadoutParserCallbacks parserCallbacks);

        friend MVLCReadout make_mvlc_readout(
            const CrateConfig &crateConfig,
            listfile::WriteHandle *listfileWriteHandle,
            readout_parser::ReadoutParserCallbacks parserCallbacks);

        friend MVLCReadout make_mvlc_readout(
            MVLC &mvlc,
            const CrateConfig &crateConfig,
            listfile::WriteHandle *listfileWriteHandle,
            readout_parser::ReadoutParserCallbacks parserCallbacks);

        friend void init_common(MVLCReadout &rdo);
};

// listfile params
MVLCReadout MESYTEC_MVLC_EXPORT make_mvlc_readout(
    const CrateConfig &crateConfig,
    const ListfileParams &listfileParams,
    readout_parser::ReadoutParserCallbacks parserCallbacks = {});

// listfile params + custom mvlc
MVLCReadout MESYTEC_MVLC_EXPORT make_mvlc_readout(
    MVLC &mvlc,
    const CrateConfig &crateConfig,
    const ListfileParams &listfileParams,
    readout_parser::ReadoutParserCallbacks parserCallbacks = {});

// listfile write handle
MVLCReadout MESYTEC_MVLC_EXPORT make_mvlc_readout(
    const CrateConfig &crateConfig,
    listfile::WriteHandle *listfileWriteHandle,
    readout_parser::ReadoutParserCallbacks parserCallbacks = {});

// listfile write handle + custom mvlc
MVLCReadout MESYTEC_MVLC_EXPORT make_mvlc_readout(
    MVLC &mvlc,
    const CrateConfig &crateConfig,
    listfile::WriteHandle *listfileWriteHandle,
    readout_parser::ReadoutParserCallbacks parserCallbacks = {});

}
}

#endif /* __MESYTEC_MVLC_MVLC_READOUT_H__ */

