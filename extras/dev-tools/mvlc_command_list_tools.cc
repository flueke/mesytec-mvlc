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

    std::vector<mvlc::StackCommand> commands;

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
            std::cerr << "Error: could not parse command list line: " << e.what() << "\n";
            continue;
        }
    }

    const int Col1Width = 30;
    const int Col2Width = 20;
    //fmt::println("Parsed {} commands:", commands.size());

    for (const auto &cmd: commands)
    {
        auto buffer = mvlc::make_stack_buffer(std::vector<mvlc::StackCommand>{cmd});
        if (buffer.empty())
            continue;
        fmt::println("{:#010x}\t{:50}", buffer[0], mvlc::to_string(cmd));

        for (size_t i=1; i<buffer.size(); ++i)
        {
            fmt::println("{:#010x}", buffer[i]);
        }
    }
}
