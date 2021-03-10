#include <condition_variable>
#include <regex>
#include <unordered_set>

#include <mesytec-mvlc/mesytec-mvlc.h>
#include <lyra/lyra.hpp>

/*

mini-daq-replay
================================================
open and init
-------------

ZipReader
  open
  find archive member
  openEntry -> ReadHandle

preamble = read_preamble(ReadHandle)
 -> magic bytes, system events

preamble file format check (magic bytes)

build CrateConfig from preamble systemevent data


prepare reading
----------------

parser callback setup
snoopQueues
thread(run_readout_parser)
ReplayWorker <- does the actual reading from file
connected via snoopQueues


read
-----------------
replayWorker.start()
replayWorker.waitableState().wait()


----
snoopqueues sentinel handling to terminate the readout parser


simpler direct call interface
=============================================

handle = open_listfile(filename)

if (!handle->isOpen())
    print handle->errorCode, handle->errorString

auto crateConfig = handle->getCrateConfig();

while (auto data = read_next_event(handle))
{
    if (data->type == SystemEvent)
    {
        if (data->systemEvent->subtype == TimeTick)
            print "got a timetick"
    }
    else if (data->type == EventData)
    {
        data->eventIndex
        data->eventName
        data->moduleCount
        data->moduleNames
        data->moduleData[moduleIndex].ptr;
        data->moduleData[moduleIndex].size;
    }

    auto stats = handle->getStats()
}

close_listfile(handle);

*/

using std::cout;
using std::cerr;
using std::endl;

using namespace mesytec::mvlc;


using namespace mesytec::mvlc::readout_parser;

struct EventData
{
    enum Type { SystemEvent, ReadoutEvent };

    Type type;
    size_t linearEventNumber = 0u;
    // SystemEvent
    DataBlock systemEventData;
    // ReadoutEvent
    int eventIndex;
    std::vector<ModuleData> moduleData;
};

class Handle
{
    private:
        struct Sync
        {
            bool ready = false;
            bool processed = false;
            std::mutex m;
            std::condition_variable cv;
        };

        const size_t BufferSize = util::Megabytes(1);
        const size_t BufferCount = 10;

        Sync sync_;
        std::atomic<bool> replayDone_;
        CrateConfig crateConfig_;
        EventData currentData_;

        // parser
        ReadoutParserCallbacks parserCallbacks_;
        ReadoutParserState parserState_;
        Protected<readout_parser::ReadoutParserCounters> parserCounters_;
        std::thread parserThread_;

        // reader
        listfile::ZipReader zr_;
        listfile::ReadHandle *rh_;
        std::unique_ptr<ReplayWorker> replayWorker_;
        ReadoutBufferQueues snoopQueues_;

        // terminate and monitor thread
        std::thread monitorThread_;

        void monitor()
        {
            replayWorker_->waitableState().wait(
                [] (const ReplayWorker::State &state)
                {
                    return state == ReplayWorker::State::Idle;
                });

            if (parserThread_.joinable())
            {
                if (auto sentinel = snoopQueues_.emptyBufferQueue().dequeue(std::chrono::seconds(1)))
                {
                    sentinel->clear();
                    snoopQueues_.filledBufferQueue().enqueue(sentinel);
                }

                parserThread_.join();
            }

            replayDone_ = true;
        }

    public:
        Handle(const std::string &filename)
            : replayDone_(false)
            , parserCounters_({})
            , snoopQueues_(BufferSize, BufferCount)
        {
            // open listfile
            zr_.openArchive(filename);

            auto entryNames = zr_.entryNameList();

            auto it = std::find_if(
                std::begin(entryNames), std::end(entryNames),
                [] (const std::string &entryName)
                {
                    static const std::regex re(R"foo(.+\.mvlclst(\.lz4)?)foo");
                    return std::regex_search(entryName, re);
                });

            if (it == std::end(entryNames))
                throw std::runtime_error("No listfile found in archive");

            auto entryName = *it;

            rh_ = zr_.openEntry(entryName);

            auto preamble = listfile::read_preamble(*rh_);

            if (!(preamble.magic == listfile::get_filemagic_eth()
                  || preamble.magic == listfile::get_filemagic_usb()))
                throw std::runtime_error("invalid file format");

            if (auto configSection = preamble.findCrateConfig())
                crateConfig_ = crate_config_from_yaml(configSection->contentsToString());

            // parser
            parserCallbacks_.eventData = [this] (
                int eventIndex, const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
            {
                std::unique_lock<std::mutex> guard(sync_.m);
                sync_.cv.wait(guard, [this] () { return sync_.processed; });

                currentData_.type = EventData::ReadoutEvent;
                currentData_.linearEventNumber++;
                currentData_.systemEventData = {};
                currentData_.eventIndex = eventIndex;
                currentData_.moduleData.clear();
                std::copy(
                    moduleDataList, moduleDataList+moduleCount,
                    std::back_inserter(currentData_.moduleData));

                sync_.ready = true;
                sync_.processed = false;
                guard.unlock();
                sync_.cv.notify_one();
            };

            parserCallbacks_.systemEvent = [this] (
                const u32 *header, u32 size)
            {
                std::unique_lock<std::mutex> guard(sync_.m);
                sync_.cv.wait(guard, [this] () { return sync_.processed; });

                currentData_.type = EventData::SystemEvent;
                currentData_.linearEventNumber++;
                currentData_.systemEventData = { header, size };
                currentData_.eventIndex = -1;
                currentData_.moduleData.clear();

                sync_.ready = true;
                sync_.processed = false;
                guard.unlock();
                sync_.cv.notify_one();
            };

            parserState_ = readout_parser::make_readout_parser(crateConfig_.stacks);

            parserThread_ = std::thread(
                readout_parser::run_readout_parser,
                std::ref(parserState_),
                std::ref(parserCounters_),
                std::ref(snoopQueues_),
                std::ref(parserCallbacks_));

            // reader/replayWorker
            replayWorker_ = std::make_unique<ReplayWorker>(snoopQueues_, rh_);
            auto fRunning = replayWorker_->start();
            fRunning.get();

            // monitor
            monitorThread_ = std::thread(&Handle::monitor, this);
        }

        EventData *readNextEvent()
        {
            // Unblock the callbacks and let the parser thread fill
            // currentData_ with the next event.
            {
                std::unique_lock<std::mutex> guard(sync_.m);
                sync_.ready = false;
                sync_.processed = true;
                guard.unlock();
                sync_.cv.notify_one();
            }

            // Wait for the parser to be done.
            std::unique_lock<std::mutex> guard(sync_.m);
            while (!sync_.cv.wait_for(
                    guard, std::chrono::milliseconds(100),
                    [this] () { return sync_.ready || replayDone_; }));

            if (sync_.ready)
            {
                assert(!sync_.processed);
                return &currentData_;
            }

            return nullptr;
        }

        ~Handle()
        {
            replayWorker_->stop();

            if (monitorThread_.joinable())
                monitorThread_.join();

            assert(!parserThread_.joinable());
            // should not happen
            if (parserThread_.joinable())
                parserThread_.join();
        }
};

std::unique_ptr<Handle> open_listfile(const std::string &filename)
{
    auto ret = std::make_unique<Handle>(filename);
    return ret;
}

EventData *read_next_event(Handle &handle)
{
    return handle.readNextEvent();
}

EventData *read_next_event(std::unique_ptr<Handle> &handle)
{
    return read_next_event(*handle);
}


int main(int argc, char *argv[])
{
    if (argc < 2) return 1;

    if (auto handle = open_listfile(argv[1]))
    {
        size_t expectedEventNumber = 1u;
        size_t systemEvents = 0u;
        size_t readoutEvents = 0u;


        while (auto data = read_next_event(handle))
        {
            //if (data->type == EventData::SystemEvent)
            //    cout << "SystemEvent ";
            //else if (data->type == EventData::ReadoutEvent)
            //    cout << "ReadoutEvent ";
            //cout << data->linearEventNumber << "\n";

            if (data->type == EventData::SystemEvent)
                ++systemEvents;
            else if (data->type == EventData::ReadoutEvent)
                ++readoutEvents;

            assert(data->linearEventNumber == expectedEventNumber);
            ++expectedEventNumber;
        }

        cout << "Read " << expectedEventNumber - 1 << " events" << "\n";

        cout << "systemEvents: " << systemEvents
            << ", readoutEvents: " << readoutEvents
            << "\n";
    }


    return 0;
}
