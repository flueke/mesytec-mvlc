#include "scanbus_support.h"

namespace mesytec::mvlc::scanbus
{

std::vector<u32> scan_vme_bus_for_candidates(
    MVLC &mvlc,
    const u16 scanBaseBegin,
    const u16 scanBaseEnd,
    const u16 probeRegister,
    const u8 probeAmod,
    const VMEDataWidth probeDataWidth,
    const unsigned maxStackSize)
{

    std::vector<u32> response; // 0xF3 stack response optionally followed by 0xF9 continuation frames
    std::vector<u32> responseContents; // raw stack response with the framing removed. makes parsing easy.
    std::vector<u32> result; // collected VME candidate addresses

    const u32 baseMax = scanBaseEnd;
    u32 base = scanBaseBegin;
    size_t nStacks = 0u;
    auto tStart = std::chrono::steady_clock::now();

    do
    {
        StackCommandBuilder sb;
        sb.addWriteMarker(0x13370001u);
        u32 baseStart = base; // first address scanned by this stack execution

        while (get_encoded_stack_size(sb) < maxStackSize - 2 && base <= baseMax)
        {
            u32 readAddress = (base << 16) | (probeRegister & 0xffffu);
            sb.addVMERead(readAddress, probeAmod, probeDataWidth);
            ++base;
        }

        spdlog::trace("Executing stack. size={}, baseStart=0x{:04x}, baseEnd=0x{:04x}, #addresses={}",
            get_encoded_stack_size(sb), baseStart, base, base - baseStart);

        response.clear();

        if (auto ec = mvlc.stackTransaction(sb, response))
            throw std::system_error(ec);

        ++nStacks;

        spdlog::trace("Stack result for baseStart=0x{:04x}, baseEnd=0x{:04x} (#addrs={}), response.size()={}\n",
            baseStart, base, base-baseStart, response.size());
        //spdlog::trace("  response={:#010x}\n", fmt::join(response, ", "));

        if (response.empty())
            throw std::runtime_error("scanbus: got empty stack response while scanning for candidates");

        auto it = std::begin(response);
        u32 respHeader = *it;
        auto headerInfo = extract_frame_info(respHeader);
        spdlog::trace("  responseHeader={:#010x}, decoded: {}", respHeader, decode_frame_header(respHeader));

        if (headerInfo.flags & frame_flags::SyntaxError)
            spdlog::warn("MVLC stack execution returned a syntax error. Scanbus results may be incomplete!");

        responseContents.clear();

        while (it < std::end(response))
        {
            assert(is_known_frame_header(*it));
            assert(is_stack_buffer(*it) || is_stack_buffer_continuation(*it));
            // 'it' is pointing directly at a frame header. If it's the 0xF3
            // StackFrame we have to skip over the stack reference word. For
            // 0xF9 Continuations we just need to skip the header itself.
            auto headerInfo = extract_frame_info(*it);
            auto startOffset = headerInfo.type == frame_headers::StackFrame ? 2 : 1;
            auto endOffset = headerInfo.len + 1;

            assert(it + startOffset <= std::end(response));
            assert(it + endOffset <= std::end(response));

            std::copy(it + startOffset, it + endOffset, std::back_inserter(responseContents));
            it += endOffset;
        }

        spdlog::trace("Stack response contents for baseStart=0x{:04x}, baseEnd=0x{:04x} (#addrs={}), contents.size()={}\n",
            baseStart, base, base-baseStart, responseContents.size());
        //spdlog::trace("  responseContents={:#010x}\n", fmt::join(responseContents, ", "));

        for (auto cit = std::begin(responseContents); cit < std::end(responseContents); ++cit)
        {
            auto index = std::distance(std::begin(responseContents), cit);
            auto value = *cit;
            const u32 addr = (baseStart + index) << 16;

            // In the error case the lowest byte contains the stack error line number, so it
            // needs to be masked out for this test.
            if ((value & 0xffffff00) != 0xffffff00)
            {
                result.push_back(addr);
                spdlog::trace("Found candidate address: index={}, value=0x{:08x}, addr={:#010x}", index, value, addr);
            }
        }

    } while (base <= baseMax);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>((std::chrono::steady_clock::now() - tStart));

    spdlog::info("Scanned {} addresses in {} ms using {} stack transactions (maxStackSize={} words). Found {} candidates.",
        scanBaseEnd - scanBaseBegin + 1, elapsed.count(), nStacks, maxStackSize, result.size());

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
