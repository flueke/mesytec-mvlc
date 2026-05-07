#include "mvlc_readout_parser.h"
#include "util/logging.h"
#include "vme_constants.h"
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include <vector>

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::readout_parser;

namespace
{

ReadoutParserState make_parser_0()
{
    StackCommandBuilder stack1("stack1");
    stack1.beginGroup("module1_0");
    stack1.addVMEBlockRead(0x1, vme_amods::a32UserBlock, 0xffff, true);
    StackCommandBuilder stack2("stack2");
    stack2.beginGroup("module2_0");
    stack2.addVMEBlockRead(0x2, vme_amods::a32UserBlock, 0xffff, true);
    std::vector<StackCommandBuilder> readoutStacks = {stack1, stack2};

    return make_readout_parser(readoutStacks, nullptr);
}

struct ModuleDataStorage
{
    std::vector<u32> data;
};

// Returns a Callback compatible vector of ModuleData structures pointing
// into the moduleTestData storage.
std::vector<ModuleData> to_module_data_list(const std::vector<ModuleDataStorage> &moduleTestData)
{
    std::vector<ModuleData> result(moduleTestData.size());

    for (size_t mi = 0; mi < moduleTestData.size(); ++mi)
    {
        result[mi] = {};
        result[mi].data.data = moduleTestData[mi].data.data();
        result[mi].data.size = moduleTestData[mi].data.size();
        result[mi].dynamicSize = moduleTestData[mi].data.size();
        result[mi].hasDynamic = true;
    }

    return result;
}

} // namespace

TEST(ReadoutParser, MakeReadoutParser)
{
    auto parser = make_parser_0();

    ASSERT_EQ(parser.readoutStructure.size(), 2);

    ASSERT_EQ(parser.readoutStructure[0].size(), 1);
    ASSERT_EQ(parser.readoutStructure[0][0].prefixLen, 0);
    ASSERT_EQ(parser.readoutStructure[0][0].suffixLen, 0);
    ASSERT_TRUE(parser.readoutStructure[0][0].hasDynamic);
    ASSERT_EQ(parser.readoutStructure[0][0].name, "module1_0");
    ASSERT_EQ(parser.readoutStructure[0][0].commands.size(), 1);

    ASSERT_EQ(parser.readoutStructure[1].size(), 1);
    ASSERT_EQ(parser.readoutStructure[1][0].prefixLen, 0);
    ASSERT_EQ(parser.readoutStructure[1][0].suffixLen, 0);
    ASSERT_TRUE(parser.readoutStructure[1][0].hasDynamic);
    ASSERT_EQ(parser.readoutStructure[1][0].name, "module2_0");
    ASSERT_EQ(parser.readoutStructure[1][0].commands.size(), 1);
}
