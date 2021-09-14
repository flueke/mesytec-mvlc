#include "mvlc_readout.h"

#include <memory>

#include "mvlc_listfile.h"
#include "mvlc_listfile_zip.h"
#include "mvlc_readout_parser.h"
#include "mvlc_readout_parser_util.h"
#include "mvlc_readout_worker.h"


namespace mesytec
{
namespace mvlc
{

struct MVLCReadout::Private
{
    MVLC mvlc;
    CrateConfig crateConfig;
    listfile::WriteHandle *lfh = nullptr;
    readout_parser::ReadoutParserCallbacks parserCallbacks;
    listfile::ZipCreator lfZip;

    ReadoutBufferQueues snoopQueues;
    readout_parser::ReadoutParserState readoutParser;
    Protected<readout_parser::ReadoutParserCounters> parserCounters;
    std::thread parserThread;
    std::atomic<bool> parserQuit;

    std::unique_ptr<ReadoutWorker> readoutWorker;

    Private()
        : parserCounters({})
        , parserQuit(false)
    {}
};

MVLCReadout::MVLCReadout()
    : d(std::make_unique<Private>())
{
}

MVLCReadout::~MVLCReadout()
{
}

std::error_code MVLCReadout::start(const std::chrono::seconds &timeToRun)
{
    return d->readoutWorker->start(timeToRun).get(); // blocks
}

std::error_code MVLCReadout::stop()
{
    return d->readoutWorker->stop();
}

std::error_code MVLCReadout::pause()
{
    return d->readoutWorker->pause();
}

std::error_code MVLCReadout::resume()
{
    return d->readoutWorker->resume();
}

ReadoutWorker::State MVLCReadout::state() const
{
    return d->readoutWorker->state();
}

WaitableProtected<ReadoutWorker::State> &MVLCReadout::waitableState()
{
    return d->readoutWorker->waitableState();
}

ReadoutWorker::Counters MVLCReadout::workerCounters()
{
    return d->readoutWorker->counters();
}

namespace
{
    listfile::WriteHandle *setup_listfile(listfile::ZipCreator &lfZip, const ListfileParams &lfParams)
    {
        lfZip.createArchive(
            lfParams.filepath,
            lfParams.overwrite ? listfile::ZipCreator::Overwrite : listfile::ZipCreator::DontOverwrite);

        switch (lfParams.compression)
        {
            case ListfileParams::Compression::LZ4:
                return lfZip.createLZ4Entry(lfParams.runname + ".mvlclst", lfParams.compressionLevel);

            case ListfileParams::Compression::ZIP:
                return lfZip.createZIPEntry(lfParams.runname + ".mvlclst", lfParams.compressionLevel);
                break;
        }

        return nullptr;
    }

}

void init_common(MVLCReadout &r)
{
    r.d->parserThread = std::thread(
        readout_parser::run_readout_parser,
        std::ref(r.d->readoutParser),
        std::ref(r.d->parserCounters),
        std::ref(r.d->snoopQueues),
        std::ref(r.d->parserCallbacks),
        std::ref(r.d->parserQuit)
        );

    r.d->readoutWorker = std::make_unique<ReadoutWorker>(
        r.d->mvlc,
        r.d->crateConfig.triggers,
        r.d->snoopQueues,
        r.d->lfh);

    r.d->readoutWorker->setMcstDaqStartCommands(r.d->crateConfig.mcstDaqStart);
    r.d->readoutWorker->setMcstDaqStopCommands(r.d->crateConfig.mcstDaqStop);
}

// listfile params
MVLCReadout make_mvlc_readout(
    const CrateConfig &crateConfig,
    const ListfileParams &lfParams,
    readout_parser::ReadoutParserCallbacks parserCallbacks)
{
    MVLCReadout r;
    r.d->mvlc = make_mvlc(crateConfig);
    r.d->crateConfig = crateConfig;
    r.d->lfh = setup_listfile(r.d->lfZip, lfParams);
    r.d->parserCallbacks = parserCallbacks;
    r.d->readoutParser = readout_parser::make_readout_parser(crateConfig.stacks);
    init_common(r);
    return r;
}

// listfile params + custom mvlc
MVLCReadout make_mvlc_readout(
    MVLC &mvlc,
    const CrateConfig &crateConfig,
    const ListfileParams &lfParams,
    readout_parser::ReadoutParserCallbacks parserCallbacks)
{
    MVLCReadout r;
    r.d->mvlc = mvlc;
    r.d->crateConfig = crateConfig;
    r.d->lfh = setup_listfile(r.d->lfZip, lfParams);
    r.d->parserCallbacks = parserCallbacks;
    r.d->readoutParser = readout_parser::make_readout_parser(crateConfig.stacks);
    init_common(r);
    return r;
}

// listfile write handle
MVLCReadout make_mvlc_readout(
    const CrateConfig &crateConfig,
    listfile::WriteHandle *listfileWriteHandle,
    readout_parser::ReadoutParserCallbacks parserCallbacks)
{
    MVLCReadout r;
    r.d->mvlc = make_mvlc(crateConfig);
    r.d->crateConfig = crateConfig;
    r.d->lfh = listfileWriteHandle;
    r.d->parserCallbacks = parserCallbacks;
    r.d->readoutParser = readout_parser::make_readout_parser(crateConfig.stacks);
    init_common(r);
    return r;
}

// listfile write handle + custom mvlc
MVLCReadout make_mvlc_readout(
    MVLC &mvlc,
    const CrateConfig &crateConfig,
    listfile::WriteHandle *listfileWriteHandle,
    readout_parser::ReadoutParserCallbacks parserCallbacks)
{
    MVLCReadout r;
    r.d->mvlc = mvlc;
    r.d->crateConfig = crateConfig;
    r.d->lfh = listfileWriteHandle;
    r.d->parserCallbacks = parserCallbacks;
    r.d->readoutParser = readout_parser::make_readout_parser(crateConfig.stacks);
    init_common(r);
    return r;
}

}
}
