#include "mvlc_readout.h"

#include <memory>

#include "mvlc_dialog_util.h"
#include "mvlc_factory.h"
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
    std::shared_ptr<listfile::WriteHandle> lfh;
    readout_parser::ReadoutParserCallbacks parserCallbacks;
    listfile::ZipCreator lfZip;

    ReadoutBufferQueues snoopQueues;
    readout_parser::ReadoutParserState readoutParser;
    Protected<readout_parser::ReadoutParserCounters> parserCounters;
    std::thread parserThread;
    std::atomic<bool> parserQuit;

    std::unique_ptr<ReadoutWorker> readoutWorker;

    ReadoutInitResults initResults;

    Private()
        : parserCounters()
        , parserQuit(false)
    {}
};

MVLCReadout::MVLCReadout()
    : d(std::make_unique<Private>())
{
}

MVLCReadout::~MVLCReadout()
{
    if (d && d->parserThread.joinable())
    {
        d->parserQuit = true;
        d->parserThread.join();
    }
}

MVLCReadout::MVLCReadout(MVLCReadout &&other)
{
    d = std::move(other.d);
}

MVLCReadout &MVLCReadout::operator=(MVLCReadout &&other)
{
    d = std::move(other.d);
    return *this;
}

std::error_code MVLCReadout::start(const std::chrono::seconds &timeToRun,
                                   const CommandExecOptions initSequenceOptions)

{
    d->initResults = init_readout(d->mvlc, d->crateConfig, initSequenceOptions);

    if (d->initResults.ec)
        return d->initResults.ec;

    if (d->lfh)
        listfile::listfile_write_preamble(*d->lfh, d->crateConfig);

    return d->readoutWorker->start(timeToRun).get(); // blocks
}

std::error_code MVLCReadout::stop()
{
    if (auto ec = d->readoutWorker->stop())
        return ec;

    while (workerState() != ReadoutWorker::State::Idle)
    {
        waitableState().wait_for(
            std::chrono::milliseconds(1000),
            [] (const ReadoutWorker::State &state)
            {
                return state == ReadoutWorker::State::Idle;
            });
    }

    if (d->lfh)
        listfile_write_system_event(*d->lfh, d->crateConfig.crateId, system_event::subtype::EndOfFile);

    return disable_daq_mode_and_triggers(d->mvlc);
}

std::error_code MVLCReadout::pause()
{
    return d->readoutWorker->pause();
}

std::error_code MVLCReadout::resume()
{
    return d->readoutWorker->resume();
}

bool MVLCReadout::finished()
{
    return (d->readoutWorker->state() == ReadoutWorker::State::Idle
            && d->readoutWorker->snoopQueues()->filledBufferQueue().empty());
}

ReadoutInitResults MVLCReadout::getInitResults() const
{
    return d->initResults;
}

ReadoutWorker::State MVLCReadout::workerState() const
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

readout_parser::ReadoutParserCounters MVLCReadout::parserCounters()
{
    return d->parserCounters.copy();
}

const CrateConfig &MVLCReadout::crateConfig() const
{
    return d->crateConfig;
}

ReadoutWorker &MVLCReadout::readoutWorker()
{
    return *d->readoutWorker;
}

std::thread &MVLCReadout::parserThread()
{
    return d->parserThread;
}

std::atomic<bool> &MVLCReadout::parserQuit()
{
    return d->parserQuit;
}

namespace
{
    std::unique_ptr<listfile::WriteHandle> setup_listfile(listfile::ZipCreator &lfZip, const ListfileParams &lfParams)
    {
        if (lfParams.writeListfile)
        {
            lfZip.createArchive(
                lfParams.filepath,
                lfParams.overwrite ? listfile::OverwriteMode::Overwrite : listfile::OverwriteMode::DontOverwrite);

            switch (lfParams.compression)
            {
                case ListfileParams::Compression::LZ4:
                    return lfZip.createLZ4Entry(lfParams.listfilename + ".mvlclst", lfParams.compressionLevel);

                case ListfileParams::Compression::ZIP:
                    return lfZip.createZIPEntry(lfParams.listfilename + ".mvlclst", lfParams.compressionLevel);
                    break;
            }
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
        r.d->lfh,
        r.d->crateConfig.crateId
        );

    r.d->readoutWorker->setMcstDaqStartCommands(r.d->crateConfig.mcstDaqStart);
    r.d->readoutWorker->setMcstDaqStopCommands(r.d->crateConfig.mcstDaqStop);
}

// listfile params
MVLCReadout make_mvlc_readout(
    const CrateConfig &crateConfig,
    const ListfileParams &lfParams,
    readout_parser::ReadoutParserCallbacks parserCallbacks,
    void *userContext)
{
    MVLCReadout r;
    r.d->mvlc = make_mvlc(crateConfig);
    r.d->crateConfig = crateConfig;
    r.d->lfh = setup_listfile(r.d->lfZip, lfParams);
    r.d->parserCallbacks = parserCallbacks;
    r.d->readoutParser = readout_parser::make_readout_parser(
        crateConfig.stacks, userContext);
    init_common(r);
    return r;
}

// listfile params + custom mvlc
MVLCReadout make_mvlc_readout(
    MVLC &mvlc,
    const CrateConfig &crateConfig,
    const ListfileParams &lfParams,
    readout_parser::ReadoutParserCallbacks parserCallbacks,
    void *userContext)
{
    MVLCReadout r;
    r.d->mvlc = mvlc;
    r.d->crateConfig = crateConfig;
    r.d->lfh = setup_listfile(r.d->lfZip, lfParams);
    r.d->parserCallbacks = parserCallbacks;
    r.d->readoutParser = readout_parser::make_readout_parser(
        crateConfig.stacks, userContext);
    init_common(r);
    return r;
}

// listfile write handle
MVLCReadout make_mvlc_readout(
    const CrateConfig &crateConfig,
    const std::shared_ptr<listfile::WriteHandle> &listfileWriteHandle,
    readout_parser::ReadoutParserCallbacks parserCallbacks,
    void *userContext)
{
    MVLCReadout r;
    r.d->mvlc = make_mvlc(crateConfig);
    r.d->crateConfig = crateConfig;
    r.d->lfh = listfileWriteHandle;
    r.d->parserCallbacks = parserCallbacks;
    r.d->readoutParser = readout_parser::make_readout_parser(
        crateConfig.stacks, userContext);
    init_common(r);
    return r;
}

// listfile write handle + custom mvlc
MVLCReadout make_mvlc_readout(
    MVLC &mvlc,
    const CrateConfig &crateConfig,
    const std::shared_ptr<listfile::WriteHandle> &listfileWriteHandle,
    readout_parser::ReadoutParserCallbacks parserCallbacks,
    void *userContext)
{
    MVLCReadout r;
    r.d->mvlc = mvlc;
    r.d->crateConfig = crateConfig;
    r.d->lfh = listfileWriteHandle;
    r.d->parserCallbacks = parserCallbacks;
    r.d->readoutParser = readout_parser::make_readout_parser(
        crateConfig.stacks, userContext);
    init_common(r);
    return r;
}

}
}
