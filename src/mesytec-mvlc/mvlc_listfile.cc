#include "mvlc_listfile.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>

#include "mvlc_constants.h"
#include "mvlc_util.h"

using std::cout;
using std::endl;

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

WriteHandle::~WriteHandle()
{ }

ReadHandle::~ReadHandle()
{ }

void listfile_write_preamble(WriteHandle &lf_out, const CrateConfig &config)
{
    listfile_write_magic(lf_out, config.connectionType);
    listfile_write_endian_marker(lf_out);
    listfile_write_crate_config(lf_out, config);
}

void listfile_write_magic(WriteHandle &lf_out, ConnectionType ct)
{
    const char *magic = nullptr;

    switch (ct)
    {
        case ConnectionType::ETH:
            magic = get_filemagic_eth();
            break;

        case ConnectionType::USB:
            magic = get_filemagic_usb();
            break;
    }

    listfile_write_raw(lf_out, reinterpret_cast<const u8 *>(magic), std::strlen(magic));
}

void listfile_write_endian_marker(WriteHandle &lf_out)
{
    listfile_write_system_event(
        lf_out, system_event::subtype::EndianMarker,
        &system_event::EndianMarkerValue, 1);
}

void listfile_write_crate_config(WriteHandle &lf_out, const CrateConfig &config)
{
    auto yaml = to_yaml(config);

    // Pad using spaces.
    while (yaml.size() % sizeof(u32))
        yaml += ' ';

    listfile_write_system_event(
            lf_out, system_event::subtype::MVLCCrateConfig,
            reinterpret_cast<const u32 *>(yaml.c_str()),
            yaml.size() / sizeof(u32));
}

void listfile_write_system_event(
    WriteHandle &lf_out, u8 subtype,
    const u32 *buffp, size_t totalWords)
{
    //if (!lf_out.isOpen())
    //    return;

    if (totalWords == 0)
    {
        listfile_write_system_event(lf_out, subtype);
        return;
    }

    if (subtype > system_event::subtype::SubtypeMax)
        throw std::runtime_error("system event subtype out of range");

    const u32 *endp  = buffp + totalWords;

    while (buffp < endp)
    {
        unsigned wordsLeft = endp - buffp;
        unsigned wordsInSection = std::min(
            wordsLeft, static_cast<unsigned>(system_event::LengthMask));

        bool isLastSection = (wordsInSection == wordsLeft);

        u32 sectionHeader = (frame_headers::SystemEvent << frame_headers::TypeShift)
            | ((subtype & system_event::SubtypeMask) << system_event::SubtypeShift);

        if (!isLastSection)
            sectionHeader |= 0b1 << system_event::ContinueShift;

        sectionHeader |= (wordsInSection & system_event::LengthMask) << system_event::LengthShift;

        listfile_write_raw(lf_out, reinterpret_cast<const u8 *>(&sectionHeader),
                           sizeof(sectionHeader));

        listfile_write_raw(lf_out, reinterpret_cast<const u8 *>(buffp),
                           wordsInSection * sizeof(u32));

        buffp += wordsInSection;
    }

    assert(buffp == endp);
}

void listfile_write_system_event(WriteHandle &lf_out, u8 subtype)
{
    if (subtype > system_event::subtype::SubtypeMax)
        throw std::runtime_error("system event subtype out of range");

    u32 sectionHeader = (frame_headers::SystemEvent << frame_headers::TypeShift)
        | ((subtype & system_event::SubtypeMask) << system_event::SubtypeShift);

    listfile_write_raw(lf_out, reinterpret_cast<const u8 *>(&sectionHeader),
                       sizeof(sectionHeader));
}

void MESYTEC_MVLC_EXPORT listfile_write_timestamp_section(
    WriteHandle &lf_out, u8 subtype)
{
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    u64 timestamp = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();

    listfile_write_system_event(
        lf_out, subtype,
        reinterpret_cast<u32 *>(&timestamp),
        sizeof(timestamp) / sizeof(u32));
}

//
// reading
//
template<typename T> T typed_read(ReadHandle &rh)
{
    T dest{};
    rh.read(reinterpret_cast<u8 *>(&dest), sizeof(T));
    return dest;
}

template<typename T> size_t typed_read(ReadHandle &rh, T &dest)
{
    return rh.read(reinterpret_cast<u8 *>(&dest), sizeof(T));
}

Preamble read_preamble(ReadHandle &rh, const size_t preambleMaxSize)
{
    Preamble result;

    // does a seek(0)
    auto magic = read_magic(rh);

    // convert magic bytes to std::string
    std::transform(
        std::begin(magic), std::end(magic), std::back_inserter(result.magic),
        [] (const u8 c) { return static_cast<char>(c); });

    size_t totalContentsSize = 0u;

    while (true)
    {
        auto frameHeader = typed_read<u32>(rh);
        auto frameInfo = extract_frame_info(frameHeader);

        if (frameInfo.type != frame_headers::SystemEvent)
            break; // should not happen for correctly written listfiles

        SystemEvent sysEvent = {};
        sysEvent.type = system_event::extract_subtype(frameHeader);
        auto &buffer = sysEvent.contents;

        while (frameInfo.type == frame_headers::SystemEvent)
        {
            size_t frameBytes = frameInfo.len * sizeof(u32);

            if (totalContentsSize + frameBytes > preambleMaxSize)
                throw std::runtime_error("preambleMaxSize exceeded");

            auto offset = buffer.size();
            buffer.resize(buffer.size() + frameBytes);
            rh.read(buffer.data() + offset, frameBytes);
            totalContentsSize += frameBytes;

            if (!(frameHeader & (1u << system_event::ContinueShift)))
                break;

            frameHeader = typed_read<u32>(rh);
            frameInfo = extract_frame_info(frameHeader);
        }

        result.systemEvents.emplace_back(sysEvent);

        // The BeginRun section marks the end of the preamble.
        if (system_event::extract_subtype(frameHeader) == system_event::subtype::BeginRun)
            break;
    }


    rh.seek(magic.size());

    return result;
}

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec
