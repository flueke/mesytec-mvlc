#include "mvlc_eth_interface.h"
#include <spdlog/fmt/fmt.h>

namespace mesytec::mvlc::eth
{


std::string eth_header0_to_string(u32 header0)
{
    PayloadHeaderInfo info{header0, 0};
    return fmt::format("header0=0x{:08x}, packetChannel={}, packetNumber={}, controllerId={}, dataWordCount={}",
                header0, info.packetChannel(), info.packetNumber(), info.controllerId(), info.dataWordCount());
}

std::string eth_header1_to_string(u32 header1)
{
    PayloadHeaderInfo info{0, header1};
    return fmt::format("header1=0x{:08x}, udpTimestamp={}, nextHeaderPointer={}",
                header1, info.udpTimestamp(), info.nextHeaderPointer());
}

std::string eth_headers_to_string(u32 header0, u32 header1)
{
    return fmt::format("{}, {}",
                eth_header0_to_string(header0),
                eth_header1_to_string(header1));
}

std::string eth_headers_to_string(const PayloadHeaderInfo &info)
{
    return fmt::format("{}, {}",
                eth_header0_to_string(info.header0),
                eth_header1_to_string(info.header1));
}

}
