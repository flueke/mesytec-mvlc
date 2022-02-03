#include <gtest/gtest.h>
#include "mvlc_listfile_gen.h"
#include "mvlc_buffer_validators.h"
#include "util/io_util.h"

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::listfile;

TEST(listfile_gen, First)
{
    std::vector<std::vector<u32>> dataStorage
    {
        {
            0x10000001,
            0x10000002,
            0x10000003,
            0x10000004,
            0x10000005,
            0x10000006,
            0x10000007,
            0x10000008,
            0x10000009,
        },
        {
            0x20000001,
            0x20000002,
            0x20000003,
            0x20000004,
            0x20000005,
            0x20000006,
            0x20000007,
            0x20000008,
            0x20000009,
        },
    };

    std::vector<readout_parser::ModuleData> moduleDataList;

    for (auto &ds: dataStorage)
    {
        readout_parser::ModuleData md;
        md.data.data = ds.data();
        md.data.size = ds.size();
        moduleDataList.emplace_back(md);
    }

    ReadoutBuffer buffer;
    int crateIndex = 1;
    int eventIndex = 2;

    write_module_data2(buffer, crateIndex, eventIndex, moduleDataList.data(), moduleDataList.size());

    //util::log_buffer(std::cout, buffer.viewU32());

    std::cout << "begin buffer" << std::endl;
    for (u32 w: buffer.viewU32())
    {
        auto ft = get_frame_type(w);
        if (is_known_frame_header(w))
            std::cout << fmt::format("  0x{:08X} -> {}", w, decode_frame_header(w)) << std::endl;
        else
            std::cout << fmt::format("  0x{:08X}", w) << std::endl;
    }
    std::cout << "end buffer" << std::endl;
}
