#include "gtest/gtest.h"
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <fmt/format.h>
#include <iostream>

using namespace mesytec::mvlc;
using std::cout;
using std::endl;


MVLC make_testing_mvlc()
{
    //return make_mvlc_eth("mvlc-0007");
    return make_mvlc_usb();
}

TEST(mvlc_stack_executor, MVLCTestTransactions)
{
    //try
    {
        auto mvlc = make_testing_mvlc();

        mvlc.setDisableTriggersOnConnect(true);

        if (auto ec = mvlc.connect())
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

        if (auto ec = disable_all_triggers(mvlc))
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

        const u32 vmeBase = 0x0;
        const u32 vmeBaseNoModule = 0x10000000u;

        for (int attempt = 0; attempt < 2; ++attempt)
        {
            StackCommandBuilder stack;

            //stack.addVMERead(vmeBase + 0x6008, vme_amods::A32, VMEDataWidth::D16);
            //stack.addVMERead(vmeBase + 0x600E, vme_amods::A32, VMEDataWidth::D16);

            //stack.addVMERead(vmeBaseNoModule + 0x6008, vme_amods::A32, VMEDataWidth::D16);


            for (int i=0; i<5505; i++)
                stack.addVMERead(vmeBase + 0x600E, vme_amods::A32, VMEDataWidth::D16);

            stack.addVMERead(vmeBase + 0x6008, vme_amods::A32, VMEDataWidth::D16);
            stack.addVMERead(vmeBase + 0x600E, vme_amods::A32, VMEDataWidth::D16);

            stack.addVMERead(vmeBaseNoModule + 0x6008, vme_amods::A32, VMEDataWidth::D16);
            stack.addVMERead(vmeBaseNoModule + 0x600E, vme_amods::A32, VMEDataWidth::D16);

            stack.addVMERead(vmeBase + 0x6008, vme_amods::A32, VMEDataWidth::D16);
            stack.addVMERead(vmeBase + 0x600E, vme_amods::A32, VMEDataWidth::D16);

            auto commands = stack.getCommands();
            auto parts = detail::split_commands(commands, {}, stacks::StackMemoryWords);

            cout << "split_commands returned " << parts.size() << " parts:" << endl;

            for (const auto &part: parts)
            {
                cout << " size=" << part.size()
                    << "words, encodedSize=" << get_encoded_stack_size(part) << endl;
            }

            std::vector<u32> response;

            for (const auto &part: parts)
            {
                if (auto ec = detail::stack_transaction(mvlc, part, response))
                {
                    if (ec)
                    {
                        std::cout << "stack_transactions returned: " << ec.message() << std::endl;
                    }

                    if (ec != ErrorType::VMEError)
                    {
                        throw ec;
                    }

                    ASSERT_TRUE(response.size() >= 1);

                    auto itResponse = std::begin(response);
                    const auto endResponse = std::end(response);
                    u32 frameHeader = *itResponse;
                    auto frameInfo = extract_frame_info(frameHeader);

                    ASSERT_TRUE(is_stack_buffer(frameHeader));
                    ASSERT_TRUE(endResponse - itResponse >= frameInfo.len + 1u);

                    while (frameInfo.flags & frame_flags::Continue)
                    {
                        itResponse += frameInfo.len + 1u;

                        ASSERT_TRUE(itResponse < endResponse);

                        frameHeader = *itResponse;
                        frameInfo = extract_frame_info(frameHeader);

                        ASSERT_TRUE(is_stack_buffer_continuation(frameHeader));
                        ASSERT_TRUE(endResponse - itResponse >= frameInfo.len + 1u);
                    }

                    //util::log_buffer(cout, response);
                }
            }
        }
    }
    //catch (const std::error_code &ec)
    //{
    //    cout << "std::error_code thrown: " << ec.message() << endl;
    //    throw;
    //}
}

TEST(mvlc_stack_executor, MVLCTestExecParseWrites)
{
    //try
    {
        auto mvlc = make_testing_mvlc();

        mvlc.setDisableTriggersOnConnect(true);

        if (auto ec = mvlc.connect())
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

        if (auto ec = disable_all_triggers(mvlc))
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

        const u32 base = 0x00000000u;
        const u8 mcstByte = 0xbbu;
        const u32 mcst = mcstByte << 24;
        const u8 irq = 1;
        const auto amod = vme_amods::A32;
        const auto dw = VMEDataWidth::D16;

        {
            StackCommandBuilder stack;
            stack.beginGroup("init");

            // module reset
            stack.addVMEWrite(base + 0x6008, 1, amod, dw);
            stack.addSoftwareDelay(std::chrono::milliseconds(100));

            // mtdc pulser, multi event readout and multicast setup
            stack.addVMEWrite(base + 0x6070, 7, amod, dw); // pulser
            stack.addVMEWrite(base + 0x6010, irq, amod, dw); // irq
            stack.addVMEWrite(base + 0x601c, 0, amod, dw);
            stack.addVMEWrite(base + 0x601e, 100, amod, dw); // irq fifo threshold in events
            stack.addVMEWrite(base + 0x6038, 0, amod, dw); // eoe marker (0: eventCounter)
            stack.addVMEWrite(base + 0x6036, 0xb, amod, dw); // multievent mode
            stack.addVMEWrite(base + 0x601a, 100, amod, dw); // max transfer data
            stack.addVMEWrite(base + 0x6020, 0x80, amod, dw); // enable mcst
            stack.addVMEWrite(base + 0x6024, mcstByte, amod, dw); // mcst address

            // mcst daq start sequence
            stack.addVMEWrite(mcst + 0x603a, 0, amod, dw);
            stack.addVMEWrite(mcst + 0x6090, 3, amod, dw);
            stack.addVMEWrite(mcst + 0x603c, 1, amod, dw);
            stack.addVMEWrite(mcst + 0x603a, 1, amod, dw);
            stack.addVMEWrite(mcst + 0x6034, 1, amod, dw);

            struct CommandExecOptions options;
            options.ignoreDelays = false;
            options.noBatching = false;

            //std::function<bool (const std::error_code &ec)> abortPredicate = is_connection_error;

            std::vector<u32> response;
            auto errors = execute_stack(mvlc, stack, stacks::StackMemoryWords, options, response);
            //util::log_buffer(cout, response, "mtdc init response");

            for (const auto &ec: errors)
                if (ec)
                    throw ec;

            const auto commands = stack.getCommands();

            auto parsedResults = parse_response_list(commands, response);

            ASSERT_EQ(parsedResults.size(), commands.size());

            for (size_t i=0; i<commands.size(); ++i)
            {
                ASSERT_EQ(commands[i], parsedResults[i].cmd);
                ASSERT_TRUE(parsedResults[i].response.empty());
            }

            //cout << "parsedResults.size()=" << parsedResults.size() << endl;
            //for (const auto &result: parsedResults)
            //{
            //    util::log_buffer(cout, result.response, to_string(result.cmd));
            //}
        }
    }
    //catch (const std::error_code &ec)
    //{
    //    cout << "std::error_code thrown: " << ec.message() << endl;
    //    throw;
    //}
}

TEST(mvlc_stack_executor, MVLCTestExecParseReads)
{
    //try
    {
        auto mvlc = make_testing_mvlc();

        mvlc.setDisableTriggersOnConnect(true);

        if (auto ec = mvlc.connect())
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

        if (auto ec = disable_all_triggers(mvlc))
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

        const u32 base = 0x00000000u;
        const u8 mcstByte = 0xbbu;
        const u32 mcst = mcstByte << 24;
        const auto amod = vme_amods::A32;
        const auto dw = VMEDataWidth::D16;

        {
            StackCommandBuilder stack;
            stack.beginGroup("readout_test");

            stack.addVMERead(base + 0x6092, amod, dw); // event counter low
            stack.addVMERead(base + 0x6094, amod, dw); // event counter high

            // block read from mtdc fifo
            //stack.addVMEBlockRead(base, vme_amods::MBLT64, std::numeric_limits<u16>::max());

            stack.addVMERead(base + 0x6092, amod, dw); // event counter low
            stack.addVMERead(base + 0x6094, amod, dw); // event counter high

            stack.addVMEWrite(mcst + 0x6034, 1, amod, dw); // readout reset

            struct CommandExecOptions options;
            options.ignoreDelays = false;
            options.noBatching = false;

            //std::function<bool (const std::error_code &ec)> abortPredicate = is_connection_error;

            size_t szMin = std::numeric_limits<size_t>::max(), szMax = 0, szSum = 0, iterations = 0;
            std::vector<u32> response;

            for (int i=0; i<1; i++)
            {
                response.clear();
                auto errors = execute_stack(mvlc, stack, stacks::StackMemoryWords, options, response);

                for (const auto &ec: errors)
                    if (ec)
                        throw ec;

                size_t size = response.size();
                szMin = std::min(szMin, size);
                szMax = std::max(szMax, size);
                szSum += size;
                ++iterations;
                util::log_buffer(cout, response, "mtdc readout_test response");
                cout << "response.size() = " << response.size() << std::endl;

                const auto commands = stack.getCommands();

                auto parsedResults = parse_response_list(commands, response);

                ASSERT_EQ(parsedResults.size(), commands.size());

                for (size_t i=0; i<commands.size(); ++i)
                {
                    ASSERT_EQ(commands[i], parsedResults[i].cmd);

                    if (commands[i].type == StackCommand::CommandType::VMERead)
                        ASSERT_EQ(parsedResults[i].response.size(), 1u);
                    else
                        ASSERT_TRUE(parsedResults[i].response.empty());
                }

#if 0
                std::cout << "parse_response: results.size() = " << parsedResults.size() << std::endl;

                for (size_t i=0; i < parsedResults.size(); ++i)
                {
                    std::cout << "result #" << i << ", cmd=" << to_string(parsedResults[i].cmd) << std::endl;

                    for (const auto &value: parsedResults[i].response)
                        std::cout << fmt::format("  {:#010x}", value) << std::endl;

                    std::cout << std::endl;
                }
#endif
            }

#if 0
            double szAvg = szSum / (iterations * 1.0);

            cout << endl;
            cout << fmt::format("iterations={}, szSum={}, szMin={}, szMax={}, szAvg={}",
                                iterations, szSum, szMin, szMax, szAvg
                               ) << std::endl;
            cout << endl;
#endif
        }
    }
}

TEST(mvlc_stack_executor, MVLCTestExecParseBlockRead)
{
    //try
    {
        auto mvlc = make_testing_mvlc();

        mvlc.setDisableTriggersOnConnect(true);

        if (auto ec = mvlc.connect())
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

        if (auto ec = disable_all_triggers(mvlc))
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

        const u32 base = 0x00000000u;
        const u8 mcstByte = 0xbbu;
        const u32 mcst = mcstByte << 24;
        const auto amod = vme_amods::A32;
        const auto dw = VMEDataWidth::D16;

        {
            StackCommandBuilder stack;
            stack.beginGroup("readout_test");

            stack.addVMERead(base + 0x6092, amod, dw); // event counter low
            stack.addVMERead(base + 0x6094, amod, dw); // event counter high

            // block read from mtdc fifo
            stack.addVMEBlockRead(base, vme_amods::MBLT64, std::numeric_limits<u16>::max()); // should yield data
            stack.addVMEBlockRead(base, vme_amods::MBLT64, std::numeric_limits<u16>::max()); // should be empty

            stack.addVMERead(base + 0x6092, amod, dw); // event counter low
            stack.addVMERead(base + 0x6094, amod, dw); // event counter high

            stack.addVMEWrite(mcst + 0x6034, 1, amod, dw); // readout reset

            struct CommandExecOptions options;
            options.ignoreDelays = false;
            options.noBatching = false;

            //std::function<bool (const std::error_code &ec)> abortPredicate = is_connection_error;

            size_t szMin = std::numeric_limits<size_t>::max(), szMax = 0, szSum = 0, iterations = 0;
            std::vector<u32> response;

            for (int i=0; i<1; i++)
            {
                response.clear();
                auto errors = execute_stack(mvlc, stack, stacks::StackMemoryWords, options, response);

                for (const auto &ec: errors)
                    if (ec)
                        throw ec;

                size_t size = response.size();
                szMin = std::min(szMin, size);
                szMax = std::max(szMax, size);
                szSum += size;
                ++iterations;
                //util::log_buffer(cout, response, "mtdc readout_test response");
                cout << "response.size() = " << response.size() << std::endl;

                const auto commands = stack.getCommands();

                auto parsedResults = parse_response_list(commands, response);

                ASSERT_EQ(parsedResults.size(), commands.size());

                for (size_t i=0; i<commands.size(); ++i)
                {
                    ASSERT_EQ(commands[i], parsedResults[i].cmd);

                    if (commands[i].type == StackCommand::CommandType::VMERead)
                    {
                        //ASSERT_TRUE(parsedResults[i].response.size() >= 1);

                        //util::log_buffer(cout, parsedResults[i].response,
                        //           "response of '" + to_string(parsedResults[i].cmd) + "'");
                    }
                    else
                    {
                        ASSERT_TRUE(parsedResults[i].response.empty());
                    }
                }

#if 1
                std::cout << "parse_response: results.size() = " << parsedResults.size() << std::endl;

                for (size_t i=0; i < parsedResults.size(); ++i)
                {
                    std::cout << "result #" << i << ", cmd=" << to_string(parsedResults[i].cmd) << std::endl;

                    if (!vme_amods::is_block_mode(parsedResults[i].cmd.amod))
                    {
                        for (const auto &value: parsedResults[i].response)
                            std::cout << fmt::format("  {:#010x}", value) << std::endl;
                    }
                    else
                        std::cout << "size=" << parsedResults[i].response.size() << " words" << endl;

                    std::cout << std::endl;
                }
#endif
            }

#if 1
            double szAvg = szSum / (iterations * 1.0);

            cout << endl;
            cout << fmt::format("iterations={}, szSum={}, szMin={}, szMax={}, szAvg={}",
                                iterations, szSum, szMin, szMax, szAvg
                               ) << std::endl;
            cout << endl;
#endif
        }
    }
}

TEST(mvlc_stack_executor, MVLCTestParseStackGroups)
{
    {
        auto mvlc = make_testing_mvlc();

        mvlc.setDisableTriggersOnConnect(true);

        if (auto ec = mvlc.connect())
        {
            std::cout << ec.message() << std::endl;
            throw ec;
        }

        const u32 base = 0x00000000u;
        const u32 noModuleBase = 0x10000000u;
        const u8 mcstByte = 0xbbu;
        const u32 mcst = mcstByte << 24;
        const auto amod = vme_amods::A32;
        const auto dw = VMEDataWidth::D16;

        {
            StackCommandBuilder stack;

            stack.beginGroup("group0");
            stack.addVMERead(base + 0x6008, amod, dw);
            stack.addVMERead(base + 0x600E, amod, dw);

            stack.beginGroup("group1");
            stack.addVMERead(noModuleBase + 0x6008, amod, dw);
            stack.addVMERead(noModuleBase + 0x600E, amod, dw);

            stack.beginGroup("group2");
            // block read from mtdc fifo
            stack.addVMEBlockRead(base, vme_amods::MBLT64, std::numeric_limits<u16>::max());
            stack.addVMERead(base + 0x6096, amod, dw); // event counter low
            stack.addVMERead(base + 0x6098, amod, dw); // event counter high

            stack.beginGroup("group3");
            stack.addVMEWrite(mcst + 0x6034, 1, amod, dw); // readout reset

            ASSERT_EQ(stack.getGroupCount(), 4u);

            struct CommandExecOptions options;
            options.ignoreDelays = false;
            options.noBatching = false;
            options.continueOnVMEError = true;

            std::vector<u32> response;

            auto errors = execute_stack(mvlc, stack, stacks::StackMemoryWords, options, response);

            for (const auto &ec: errors)
            {
                if (ec && ec != ErrorType::VMEError)
                {
                    cout << "Error from execute_stack: " << ec.message() << endl;
                    throw ec;
                }
            }

            auto groupedResults = parse_stack_exec_response(stack, response, errors);

            ASSERT_EQ(groupedResults.groups.size(), stack.getGroupCount());

            for (size_t groupIndex = 0; groupIndex < groupedResults.groups.size(); ++groupIndex)
            {
                const auto &stackGroup = stack.getGroup(groupIndex);
                const auto &resultsGroup = groupedResults.groups[groupIndex];

                ASSERT_EQ(stackGroup.commands.size(), resultsGroup.results.size());
                ASSERT_EQ(stackGroup.name, resultsGroup.name);

                for (size_t cmdIndex = 0; cmdIndex < resultsGroup.results.size(); ++cmdIndex)
                {
                    ASSERT_EQ(stackGroup.commands[cmdIndex], resultsGroup.results[cmdIndex].cmd);
                }
            }

            cout << groupedResults << endl;
        }
    }
}
