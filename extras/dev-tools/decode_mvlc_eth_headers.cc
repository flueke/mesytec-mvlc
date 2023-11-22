#include <argh.h>
#include <mesytec-mvlc/mesytec-mvlc.h>

using namespace mesytec::mvlc;

using std::cout;
using std::cerr;
using std::endl;

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "Error: missing eth packet header arguments.\n";
        std::cerr << "Usage: decode_mvlc_eth_headers header0 header1.\n";
        return 1;
    }

    u32 header0 = 0, header1 = 0;

    try
    {
        argh::parser parser;
        parser.parse(argv);
        std::string str;

        parser(1) >> str;
        header0 = std::stoul(str, nullptr, 0);

        parser(2) >> str;
        header1 = std::stoul(str, nullptr, 0);
    }
    catch (const std::exception &e)
    {
        std::cerr << fmt::format("Error parsing header words: {}\n", e.what());
        return 1;
    }

    eth::PayloadHeaderInfo headerInfo{ header0, header1 };

    std::cout << fmt::format("header0 = 0x{:08x}, header1 = 0x{:08x}\n", header0, header1);
    std::cout << fmt::format("header0: packetChannel={}, packetNumber={}, controllerId={}, dataWordCount={}\n",
        headerInfo.packetChannel(), headerInfo.packetNumber(), headerInfo.controllerId(), headerInfo.dataWordCount());
    std::cout << fmt::format("header1: udpTimestamp={}, nextHeaderPointer=0x{:04x}, isHeaderPointerPresent={}\n",
        headerInfo.udpTimestamp(), headerInfo.nextHeaderPointer(), headerInfo.isNextHeaderPointerPresent());

    return 0;
}
