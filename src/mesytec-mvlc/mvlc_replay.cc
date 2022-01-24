#include "mvlc_replay.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <regex>
#include <thread>

#if __linux__
#include <sys/prctl.h>
#endif

#include "mvlc_readout_parser_util.h"
#include "mvlc_listfile_zip.h"

namespace mesytec
{
namespace mvlc
{

struct MVLCReplay::Private
{
    listfile::ReadHandle *lfh = nullptr;
    readout_parser::ReadoutParserCallbacks parserCallbacks;
    CrateConfig crateConfig;
    listfile::ZipReader lfZip;

    ReadoutBufferQueues snoopQueues;
    readout_parser::ReadoutParserState readoutParser;
    Protected<readout_parser::ReadoutParserCounters> parserCounters;
    std::thread parserThread;
    std::atomic<bool> parserQuit;

    std::unique_ptr<ReplayWorker> replayWorker;

    Private()
        : parserCounters()
        , parserQuit(false)
    {}
};

MVLCReplay::MVLCReplay()
    : d(std::make_unique<Private>())
{
}

MVLCReplay::~MVLCReplay()
{
    if (d && d->parserThread.joinable())
    {
        d->parserQuit = true;
        d->parserThread.join();
    }
}

MVLCReplay::MVLCReplay(MVLCReplay &&other)
{
    d = std::move(other.d);
}

MVLCReplay &MVLCReplay::operator=(MVLCReplay &&other)
{
    d = std::move(other.d);
    return *this;
}

void init_common(MVLCReplay &r, int crateIndex, void *userContext)
{
    auto preamble = listfile::read_preamble(*r.d->lfh);

    if (!(preamble.magic == listfile::get_filemagic_eth()
          || preamble.magic == listfile::get_filemagic_usb()))
        throw std::runtime_error("Invalid listfile file format");

    auto configSection = preamble.findCrateConfig();

    if (!configSection)
        throw std::runtime_error("No MVLC CrateConfig found in listfile");

    r.d->crateConfig = crate_config_from_yaml(configSection->contentsToString());
    r.d->readoutParser = readout_parser::make_readout_parser(
        r.d->crateConfig.stacks, crateIndex, userContext);

    r.d->parserThread = std::thread(
        readout_parser::run_readout_parser,
        std::ref(r.d->readoutParser),
        std::ref(r.d->parserCounters),
        std::ref(r.d->snoopQueues),
        std::ref(r.d->parserCallbacks),
        std::ref(r.d->parserQuit)
        );

    r.d->replayWorker = std::make_unique<ReplayWorker>(
        r.d->snoopQueues,
        r.d->lfh);
}

MVLCReplay make_mvlc_replay(
    const std::string &listfileFilename,
    readout_parser::ReadoutParserCallbacks parserCallbacks,
    int crateIndex,
    void *userContext)
{
    MVLCReplay r;
    r.d->parserCallbacks = parserCallbacks;

    auto &zr = r.d->lfZip;

    zr.openArchive(listfileFilename);

    // Try to find a listfile inside the archive.
    auto entryName = zr.firstListfileEntryName();

    if (entryName.empty())
        throw std::runtime_error("No listfile found in archive");

    r.d->lfh = zr.openEntry(entryName);

    init_common(r, crateIndex, userContext);

    return r;
}

MVLCReplay make_mvlc_replay(
    const std::string &listfileArchiveName,
    const std::string &listfileArchiveMemberName,
    readout_parser::ReadoutParserCallbacks parserCallbacks,
    int crateIndex,
    void *userContext)
{
    if (listfileArchiveMemberName.empty())
        return make_mvlc_replay(listfileArchiveName, parserCallbacks);

    MVLCReplay r;
    r.d->parserCallbacks = parserCallbacks;

    auto &zr = r.d->lfZip;
    zr.openArchive(listfileArchiveName);
    r.d->lfh = zr.openEntry(listfileArchiveMemberName);

    init_common(r, crateIndex, userContext);

    return r;
}

MVLCReplay make_mvlc_replay(
    listfile::ReadHandle *lfh,
    readout_parser::ReadoutParserCallbacks parserCallbacks,
    int crateIndex,
    void *userContext)
{
    MVLCReplay r;
    r.d->lfh = lfh;
    r.d->parserCallbacks = parserCallbacks;

    init_common(r, crateIndex, userContext);

    return r;
}

std::error_code MVLCReplay::start()
{
    return d->replayWorker->start().get();
}

std::error_code MVLCReplay::stop()
{
    if (auto ec = d->replayWorker->stop())
        return ec;

    while (workerState() != ReplayWorker::State::Idle)
    {
        waitableState().wait_for(
            std::chrono::milliseconds(1000),
            [] (const ReplayWorker::State &state)
            {
                return state == ReplayWorker::State::Idle;
            });
    }

    return {};
}

std::error_code MVLCReplay::pause()
{
    return d->replayWorker->pause();
}

std::error_code MVLCReplay::resume()
{
    return d->replayWorker->resume();
}

bool MVLCReplay::finished()
{
    return (d->replayWorker->state() == ReplayWorker::State::Idle
            && d->replayWorker->snoopQueues().filledBufferQueue().empty());
}

ReplayWorker::State MVLCReplay::workerState() const
{
    return d->replayWorker->state();
}

WaitableProtected<ReplayWorker::State> &MVLCReplay::waitableState()
{
    return d->replayWorker->waitableState();
}

ReplayWorker::Counters MVLCReplay::workerCounters()
{
    return d->replayWorker->counters();
}

readout_parser::ReadoutParserCounters MVLCReplay::parserCounters()
{
    return d->parserCounters.copy();
}

const CrateConfig &MVLCReplay::crateConfig() const
{
    return d->crateConfig;
}

ReplayWorker &MVLCReplay::replayWorker()
{
    return *d->replayWorker;
}

std::thread &MVLCReplay::parserThread()
{
    return d->parserThread;
}

std::atomic<bool> &MVLCReplay::parserQuit()
{
    return d->parserQuit;
}

} // end namespace mvlc
} // end namespace mesytec
