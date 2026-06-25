#include <argh.h>
#include <string>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <spdlog/fmt/fmt.h>

// For now just prints a side-by-side representation of the commands parsed from
// file and their binary representation as u32 words.
// Commands are in the mvlc text format, one command per line. No interwoven
// stack upload or similar.

using namespace mesytec;

int main(int argc, char *argv[])
{
    argh::parser parser;
    parser.add_param("--stack-upload-offset");
    parser.parse(argc, argv);

    std::ifstream inputFile;
    std::istream *input = nullptr;

    if (parser.pos_args().size() == 1)
    {
        // no file name given, read from stdin
        input = &std::cin;
    }
    else
    {
        // read from a single input file
        auto inputFileName = parser[1];
        inputFile.open(inputFileName, std::ios::in);
        if (!inputFile.is_open())
        {
            std::cerr << "Error: could not open input file '" << inputFileName << "'\n";
            return 1;
        }
        input = &inputFile;
    }

    std::string tmpStr;
    const bool withStackStartEnd = parser["--with-stack-start-end"];
    const bool withUploadCommands = parser["--with-upload-commands"];
    parser("--stack-upload-offset") >> tmpStr;

    auto uploadOffset = mvlc::parse_unsigned<mvlc::u16>(tmpStr).value_or(0);



    std::vector<mvlc::StackCommand> commands;

    if (withStackStartEnd)
    {
        mvlc::StackCommand cmd;
        cmd.type = mvlc::StackCommand::CommandType::StackStart;
        commands.push_back(cmd);
    }

    std::string line;
    while (std::getline(*input, line))
    {
        try
        {
            auto cmd = mvlc::stack_command_from_string(line);
            commands.push_back(cmd);
        }
        catch (const std::exception &e)
        {
            if (!line.empty())
                std::cerr << "Error: could not parse command list line: " << e.what() << "\n";
            continue;
        }
    }

    if (withStackStartEnd)
    {
        mvlc::StackCommand cmd;
        cmd.type = mvlc::StackCommand::CommandType::StackEnd;
        commands.push_back(cmd);
    }

    const int Col1Width = 30;
    const int Col2Width = 20;
    fmt::println("Parsed {} commands:", commands.size());

    mvlc::StackCommandBuilder stackBuilder(commands);

    // Note: have to convert each command individually so we know which
    // words belong to which command. This information is never needed when
    // just uploading and executing commands.

    if (withUploadCommands)
    {
        using SuperCT = mvlc::super_commands::SuperCommandType;

        fmt::println("{:#010x}\t# {:50}",
                static_cast<mvlc::u32>(SuperCT::CmdBufferStart) << mvlc::super_commands::SuperCmdShift,
                mvlc::to_string(mvlc::SuperCommand({SuperCT::CmdBufferStart})));

        mvlc::u16 destAddress = mvlc::stacks::StackMemoryBegin + uploadOffset;

        for (const auto &cmd: commands)
        {
            auto stackBuffer = mvlc::make_stack_buffer(std::vector<mvlc::StackCommand>{cmd});
            std::vector<mvlc::SuperCommand> uploadCommands;

            for (const auto &stackWord: stackBuffer)
            {
                mvlc::SuperCommand uploadCmd;
                uploadCmd.type = mvlc::super_commands::SuperCommandType::WriteLocal;
                uploadCmd.value = stackWord;
                uploadCmd.address = destAddress;
                uploadCommands.push_back(uploadCmd);
                destAddress += mvlc::AddressIncrement;
            }

            if (uploadCommands.empty())
            {
                spdlog::warn("make_stack_upload_commands() returned empty command list for command: {}", mvlc::to_string(cmd));
                continue;
            }

            for (const auto &uploadCmd: uploadCommands)
            {
                auto buffer = mvlc::make_command_buffer(std::vector<mvlc::SuperCommand>{uploadCmd});
                if (buffer.size() < 2)
                {
                    spdlog::warn("make_command_buffer() returned empty buffer for upload command: {}", mvlc::to_string(uploadCmd));
                    continue;
                }

                fmt::println("{:#010x}\t# {:50}", buffer[1], mvlc::to_string(uploadCmd));
                // output additional words of the command buffer if any
                for (size_t i=2; i<buffer.size()-1; ++i)
                {
                    fmt::println("{:#010x}", buffer[i]);
                }

            }
        }

        fmt::println("{:#010x}\t# {:50}",
                static_cast<mvlc::u32>(SuperCT::CmdBufferEnd) << mvlc::super_commands::SuperCmdShift,
                mvlc::to_string(mvlc::SuperCommand({SuperCT::CmdBufferEnd})));
    }
    else
    {
        for (const auto &cmd: commands)
        {
            auto buffer = mvlc::make_stack_buffer(std::vector<mvlc::StackCommand>{cmd});
            if (buffer.empty())
                continue;
            fmt::println("{:#010x}\t# {:50}", buffer[0], mvlc::to_string(cmd));

            // output additional words of the command buffer if any
            for (size_t i=1; i<buffer.size(); ++i)
            {
                fmt::println("{:#010x}", buffer[i]);
            }
        }
    }
}
