#include <argh.h>
#include <mesytec-mvlc/mesytec-mvlc.h>
#include <mesytec-mvlc/util/fmt.h>

#include <string>

using namespace mesytec::mvlc;

using std::cerr;
using std::cout;
using std::endl;

// decode_mvlc_eth_packets [--text-input] <file> ...

std::vector<u32> read_binary_file(const std::string &filename)
{
    std::vector<u32> result;
    std::ifstream input(filename, std::ios::in | std::ios::binary);

    while (input.good())
    {
        u32 word = 0;
        input.read(reinterpret_cast<char *>(&word), sizeof(word));
        if (input.good())
            result.push_back(word);
    }

    return result;
}

std::vector<u32> read_text_file(const std::string &filename)
{
    std::vector<u32> result;
    std::ifstream input(filename);
    std::string word;

    while (input >> word)
    {
        u32 value = std::stoull(word, nullptr, 0);
        result.push_back(value);
    }

    return result;
}

void decode_packet_data(std::ostream &os, const std::string &filename,
                        std::basic_string_view<u32> data)
{
    using SuperT = super_commands::SuperCommandType;
    using StackT = stack_commands::StackCommandType;

    size_t wordIndex = 0;
    bool isFromMvlc = false; // set to true if the packet was sent by the MVLC

    // handle the two eth header words first
    if (data.size() >= 2)
    {
        u32 header0 = data[0];
        u32 header1 = data[1];

        if (((header0 >> 30) & 0b11) == 0b00)
        {
            eth::PayloadHeaderInfo headerInfo{header0, header1};

            os << fmt::format("Assuming MVLC packet data with two eth header words:\n");
            os << fmt::format("header0 = 0x{:08x}, header1 = 0x{:08x}\n", header0, header1);
            os << fmt::format(
                "header0: packetChannel={}, packetNumber={}, controllerId={}, dataWordCount={}\n",
                headerInfo.packetChannel(), headerInfo.packetNumber(), headerInfo.controllerId(),
                headerInfo.dataWordCount());
            os << fmt::format("header1: udpTimestamp={}, nextHeaderPointer=0x{:04x}, "
                              "isHeaderPointerPresent={}\n",
                              headerInfo.udpTimestamp(), headerInfo.nextHeaderPointer(),
                              headerInfo.isNextHeaderPointerPresent());

            isFromMvlc = true;
            data.remove_prefix(2);
        }
    }

    if (isFromMvlc)
    {
        while (!data.empty())
        {
            auto word = data[0];
            data.remove_prefix(1);

            if (is_known_frame_header(word))
                os << fmt::format("{:#010x}  {}\n", word, decode_frame_header(word));
            else if (is_super_command(word))
            {
                os << fmt::format("{:#010x}    super command\n", word);
            }
            else if (is_stack_command(word))
            {
                os << fmt::format("{:#010x}    stack command\n", word);
            }
            else
            {
                os << fmt::format("{:#010x}    payload/unknown\n", word);
            }
        }
    }
    else {}
}

int main(int argc, char *argv[])
{
    argh::parser cmdl({"-h", "--help", "--text-input"});
    cmdl.parse(argv);

    fmt::print("pos_args: {}\n", fmt::join(cmdl.pos_args(), ", "));

    const bool isTextInput = cmdl["--text-input"];

    for (auto argIt = cmdl.begin() + 1; argIt != cmdl.end(); ++argIt)
    {
        auto filename = *argIt;
        fmt::print("Processing file in {} mode: {}\n", isTextInput ? "text" : "binary", filename);

        std::vector<u32> data;
        if (!isTextInput)
        {
            data = read_binary_file(filename);
            fmt::print("Read {} words from binary file '{}'\n", data.size(), filename);
        }
        else
        {
            data = read_text_file(filename);
            fmt::print("Read {} words from text file '{}'\n", data.size(), filename);
        }

        decode_packet_data(std::cout, filename,
                           std::basic_string_view<u32>(data.data(), data.size()));
    }

    return 0;
}
