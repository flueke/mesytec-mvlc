#include <gtest/gtest.h>
#include "mvlc_constants.h"
#include "mvlc_listfile_gen.h"
#include "mvlc_buffer_validators.h"
#include "mvlc_readout_parser.h"
#include "util/io_util.h"
#include "vme_constants.h"

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::listfile;

namespace
{
    std::vector<readout_parser::ModuleData> make_module_data_list(
        const std::vector<std::vector<u32>> &dataStorage)
    {
        std::vector<readout_parser::ModuleData> moduleDataList;

        for (auto &ds: dataStorage)
        {
            readout_parser::ModuleData md;
            md.data.data = ds.data();
            md.data.size = ds.size();
            moduleDataList.emplace_back(md);
        }

        return moduleDataList;
    }

    void log_buffer(const ReadoutBuffer &buffer, bool verbose=true)
    {
        std::cout << "begin buffer" << std::endl;
        for (u32 w: buffer.viewU32())
        {
            if (!verbose)
                std::cout << fmt::format("  0x{:08X}", w) << std::endl;
            else if (is_known_frame_header(w))
                std::cout << fmt::format("  0x{:08X}, // {}", w, decode_frame_header(w)) << std::endl;
            else
                std::cout << fmt::format("  0x{:08X},", w) << std::endl;
        }
        std::cout << "end buffer" << std::endl;
    }

    using BufferView = nonstd::basic_string_view<const u32>;
}

TEST(listfile_gen, GenModuleDataAndParse)
{
    int crateIndex = 1;
    int eventIndex = 2;

    std::vector<std::vector<u32>> dataStorage
    {
        // module0
        {
            0x10000001,
            0x10000002,
        },
        // module1
        {
            0x20000001,
            0x20000002,
        },
    };

    auto moduleDataList = make_module_data_list(dataStorage);

    StackCommandBuilder readoutStack;
    readoutStack.beginGroup("1st module");
    readoutStack.addVMEBlockRead(0, vme_amods::MBLT64, 0xffff);
    readoutStack.beginGroup("2nd module");
    readoutStack.addVMEBlockRead(0, vme_amods::MBLT64, 0xffff);

    const std::vector<StackCommandBuilder> readoutStacks =
    {
        {},             // eventIndex=0
        {},             // eventIndex=1
        readoutStack    // eventIndex=2
    };

    {
        // Frames of max size 3 -> data from one module fits exactly into a single stack frame.
        u32 frameMaxWords = 3;
        ReadoutBuffer buffer;

        write_module_data(
            buffer, crateIndex, eventIndex,
            moduleDataList.data(), moduleDataList.size(),
            frameMaxWords);

        log_buffer(buffer);
        const std::vector<u32> expected =
        {
            0xF3832003, // StackResultFrame (len=3, stackNum=3, frameFlags=continue)
            0xF5000002, // BlockReadFrame (len=2, frameFlags=none)
            0x10000001,
            0x10000002,
            0xF9032003, // StackResultContinuation Frame (len=3, stackNum=3, frameFlags=none)
            0xF5000002, // BlockReadFrame (len=2, frameFlags=none)
            0x20000001,
            0x20000002,
        };

        ASSERT_EQ(buffer.viewU32(), BufferView(expected.data(), expected.size()));

        auto parserState = readout_parser::make_readout_parser(readoutStacks, crateIndex);
        readout_parser::ReadoutParserCounters parserCounters = {};
        readout_parser::ReadoutParserCallbacks parserCallbacks;

        parserCallbacks.eventData = [&dataStorage] (
            void *, int crateIndex, int eventIndex,
            const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
        {
            std::cout << fmt::format("eventData callback: ci={}, ei={}, moduleCount={}",
                                     crateIndex, eventIndex, moduleCount) << std::endl;

            ASSERT_EQ(crateIndex, 1);
            ASSERT_EQ(eventIndex, 2);
            ASSERT_EQ(moduleCount, 2);

            for (unsigned mi=0; mi<moduleCount; ++mi)
            {
                BufferView modView(moduleDataList[mi].data.data, moduleDataList[mi].data.size);
                BufferView expected(dataStorage[mi].data(), dataStorage[mi].size());

                ASSERT_EQ(modView, expected);
            }
        };

        auto pr = readout_parser::parse_readout_buffer(
            static_cast<ConnectionType>(buffer.type()),
            parserState,
            parserCallbacks,
            parserCounters,
            buffer.bufferNumber(), buffer.viewU32().data(), buffer.viewU32().size()
            );

        std::cout << "parseresult: " << get_parse_result_name(pr) << std::endl;

        ASSERT_EQ(pr, readout_parser::ParseResult::Ok);
    }

    {
        // Shorted possible frames that should still work -> one word of module data per
        // BlockReadFrame
        u32 frameMaxWords = 2;
        ReadoutBuffer buffer;
        write_module_data(
            buffer, crateIndex, eventIndex,
            moduleDataList.data(), moduleDataList.size(),
            frameMaxWords);

        log_buffer(buffer);
        const std::vector<u32> expected =
        {
            0xF3832002, // StackResultFrame (len=2, stackNum=3, frameFlags=continue)
            0xF5800001, // BlockReadFrame (len=1, frameFlags=continue)
            0x10000001,
            0xF9832002, // StackResultContinuation Frame (len=2, stackNum=3, frameFlags=continue)
            0xF5000001, // BlockReadFrame (len=1, frameFlags=none)
            0x10000002,
            0xF9832002, // StackResultContinuation Frame (len=2, stackNum=3, frameFlags=continue)
            0xF5800001, // BlockReadFrame (len=1, frameFlags=continue)
            0x20000001,
            0xF9032002, // StackResultContinuation Frame (len=2, stackNum=3, frameFlags=none)
            0xF5000001, // BlockReadFrame (len=1, frameFlags=none)
            0x20000002,
        };

        ASSERT_EQ(buffer.viewU32(), BufferView(expected.data(), expected.size()));

        auto parserState = readout_parser::make_readout_parser(readoutStacks, crateIndex);
        readout_parser::ReadoutParserCounters parserCounters = {};
        readout_parser::ReadoutParserCallbacks parserCallbacks;

        parserCallbacks.eventData = [&dataStorage] (
            void *, int crateIndex, int eventIndex,
            const readout_parser::ModuleData *moduleDataList, unsigned moduleCount)
        {
            std::cout << fmt::format("eventData callback: ci={}, ei={}, moduleCount={}",
                                     crateIndex, eventIndex, moduleCount) << std::endl;

            ASSERT_EQ(crateIndex, 1);
            ASSERT_EQ(eventIndex, 2);
            ASSERT_EQ(moduleCount, 2);

            for (unsigned mi=0; mi<moduleCount; ++mi)
            {
                BufferView modView(moduleDataList[mi].data.data, moduleDataList[mi].data.size);
                BufferView expected(dataStorage[mi].data(), dataStorage[mi].size());

                ASSERT_EQ(modView, expected);
            }
        };

        auto pr = readout_parser::parse_readout_buffer(
            static_cast<ConnectionType>(buffer.type()),
            parserState,
            parserCallbacks,
            parserCounters,
            buffer.bufferNumber(), buffer.viewU32().data(), buffer.viewU32().size()
            );

        std::cout << "parseresult: " << get_parse_result_name(pr) << std::endl;

        ASSERT_EQ(pr, readout_parser::ParseResult::Ok);
    }
}

TEST(listfile_gen, GenSystemEventAndParse)
{
    int crateIndex = 1;

    // Note: not adding the crateIndex here because currently the SystemEvent producing code
    // (ReadoutWorker) does not know the crateIndex and thus leaves the field cleared.
    u32 systemEventHeader = (frame_headers::SystemEvent << frame_headers::TypeShift);

    std::vector<u32> eventStorage =
    {
        systemEventHeader,
        0x10000001,
        0x10000002,
        0x10000003,
    };

    {
        u32 frameMaxWords = 3;
        ReadoutBuffer buffer;

        write_system_event(
            buffer, crateIndex, eventStorage.data(), eventStorage.size(),
            frameMaxWords);

        log_buffer(buffer);
        const std::vector<u32> expected =
        {
            0xFA100003, // SystemEvent (len=3, subType=0, ctrlId=1, frameFlags=none)
            0x10000001,
            0x10000002,
            0x10000003,
        };
        ASSERT_EQ(buffer.viewU32(), BufferView(expected.data(), expected.size()));
    }

    {
        u32 frameMaxWords = 2;
        ReadoutBuffer buffer;

        write_system_event(
            buffer, crateIndex, eventStorage.data(), eventStorage.size(),
            frameMaxWords);

        log_buffer(buffer);
        const std::vector<u32> expected =
        {
            0xFA900002, // SystemEvent (len=2, subType=0, ctrlId=1, frameFlags=Continue)
            0x10000001,
            0x10000002,
            0xFA100001, // SystemEvent (len=1, subType=0, ctrlId=1, frameFlags=none)
            0x10000003,
        };
        ASSERT_EQ(buffer.viewU32(), BufferView(expected.data(), expected.size()));
    }
}
