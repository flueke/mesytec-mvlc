#include "scanbus_support.h"

namespace mesytec::mvlc::scanbus
{

std::vector<u32> scan_vme_bus_for_candidates(
    MVLC &mvlc,
    const u16 scanBaseBegin,
    const u16 scanBaseEnd,
    const u16 probeRegister,
    const u8 probeAmod,
    const VMEDataWidth probeDataWidth)
{
    std::vector<u32> response;
    std::vector<u32> result;

    const u32 baseMax = scanBaseEnd;
    u32 base = scanBaseBegin;
    size_t nStacks = 0u;
    auto tStart = std::chrono::steady_clock::now();

    do
    {
        StackCommandBuilder sb;
        sb.addWriteMarker(0x13370001u);
        u32 baseStart = base;

        // TODO: can optimize this now that stackTransaction is not limited by
        // MirrorTransactionMaxContentsWords anymore.
        while (get_encoded_stack_size(sb) < MirrorTransactionMaxContentsWords / 2 - 2
                && base <= baseMax)
        {
            u32 readAddress = (base << 16) | (probeRegister & 0xffffu);
            sb.addVMERead(readAddress, probeAmod, probeDataWidth);
            ++base;
        }

        spdlog::trace("Executing stack. size={}, baseStart=0x{:04x}, baseEnd=0x{:04x}, #addresses={}",
            get_encoded_stack_size(sb), baseStart, base, base - baseStart);

        if (auto ec = mvlc.stackTransaction(sb, response))
            throw std::system_error(ec);

        ++nStacks;

        spdlog::trace("Stack result for baseStart=0x{:04x}, baseEnd=0x{:04x} (#addrs={}), response.size()={}\n",
            baseStart, base, base-baseStart, response.size());
        spdlog::trace("  response={:#010x}\n", fmt::join(response, ", "));

        if (!response.empty())
        {
            u32 respHeader = response[0];
            spdlog::trace("  responseHeader={:#010x}, decoded: {}", respHeader, decode_frame_header(respHeader));
            auto headerInfo = extract_frame_info(respHeader);

            if (headerInfo.flags & frame_flags::SyntaxError)
            {
                spdlog::warn("MVLC stack execution returned a syntax error. Scanbus results may be incomplete!");
            }
        }

        // +2 to skip over 0xF3 and the marker
        for (auto it = std::begin(response) + 2; it < std::end(response); ++it)
        {
            auto index = std::distance(std::begin(response) + 2, it);
            auto value = *it;
            const u32 addr = (baseStart + index) << 16;
            //spdlog::debug("index={}, addr=0x{:08x} value=0x{:08x}", index, addr, value);

            // In the error case the lowest byte contains the stack error line number, so it
            // needs to be masked out for this test.
            if ((value & 0xffffff00) != 0xffffff00)
            {
                result.push_back(addr);
                spdlog::trace("Found candidate address: index={}, value=0x{:08x}, addr={:#010x}", index, value, addr);
            }
        }

        response.clear();

    } while (base <= baseMax);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>((std::chrono::steady_clock::now() - tStart));

    spdlog::info("Scanned {} addresses in {} ms using {} stack transactions",
        scanBaseEnd - scanBaseBegin + 1, elapsed.count(), nStacks);

    return result;
}

std::error_code read_module_info(MVLC &mvlc, u32 vmeAddress, VMEModuleInfo &dest)
{
    if (auto ec = mvlc.vmeRead(vmeAddress + FirmwareRegister, dest.fwId, vme_amods::A32, VMEDataWidth::D16))
        return ec;

    if (auto ec = mvlc.vmeRead(vmeAddress + HardwareIdRegister, dest.hwId, vme_amods::A32, VMEDataWidth::D16))
        return ec;

    // special case for the MVHV4
    if (dest.hwId == 0 && dest.fwId == 0)
    {
        if (auto ec = mvlc.vmeRead(vmeAddress + MVHV4FirmwareRegister, dest.fwId, vme_amods::A32, VMEDataWidth::D16))
            return ec;

        if (auto ec = mvlc.vmeRead(vmeAddress + MVHV4HardwareIdRegister, dest.hwId, vme_amods::A32, VMEDataWidth::D16))
            return ec;
    }

    return {};
}

}
