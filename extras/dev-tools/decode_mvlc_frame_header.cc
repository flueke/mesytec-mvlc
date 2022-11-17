#include <mesytec-mvlc/mesytec-mvlc.h>

using namespace mesytec::mvlc;

using std::cout;
using std::cerr;
using std::endl;

int main(int argc, char *argv[])
{
    std::vector<u32> frameHeaders;

    for (int i=1; i<argc;++i)
        frameHeaders.push_back(std::strtoll(argv[i], nullptr, 0));


    for (auto header: frameHeaders)
    {
        auto frameInfo = extract_frame_info(header);

        if (frameInfo.type == frame_headers::BlockRead)
            std::cout << "  ";

        std::cout
            << fmt::format("0x{:08x} -> {}", header, decode_frame_header(header))
            << std::endl;
    }
}
