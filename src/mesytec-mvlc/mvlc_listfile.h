#ifndef __MESYTEC_MVLC_MVLC_LISTFILE_H__
#define __MESYTEC_MVLC_MVLC_LISTFILE_H__

#include <algorithm>
#include <numeric>

#include "mesytec-mvlc/mesytec-mvlc_export.h"

#include "mvlc_readout_config.h"
#include "util/int_types.h"
#include "util/storage_sizes.h"

namespace mesytec
{
namespace mvlc
{
namespace listfile
{

// Note: most of the classes and functions here do their error reporting by
// throwing std::runtime_error!

class MESYTEC_MVLC_EXPORT WriteHandle
{
    public:
        virtual ~WriteHandle();
        // Write the given data to the listfile. Return the number of bytes
        // written. Error reporting must be done by throwing
        // std::runtime_error.
        virtual size_t write(const u8 *data, size_t size) = 0;
};

class MESYTEC_MVLC_EXPORT ReadHandle
{
    public:
        virtual ~ReadHandle();
        virtual size_t read(u8 *dest, size_t maxSize) = 0;
        virtual void seek(size_t pos) = 0;
};

//
// writing
//

inline size_t listfile_write_raw(WriteHandle &lf_out, const u8 *buffer, size_t size)
{
    return lf_out.write(buffer, size);
}

void MESYTEC_MVLC_EXPORT listfile_write_magic(WriteHandle &lf_out, ConnectionType ct);
void MESYTEC_MVLC_EXPORT listfile_write_endian_marker(WriteHandle &lf_out);
void MESYTEC_MVLC_EXPORT listfile_write_crate_config(WriteHandle &lf_out, const CrateConfig &config);

// Writes the magic bytes, an endian marker and the CrateConfig to the output
// handle. The magic bytes and the endian marker are the required elements. The
// addition of the crate config contents makes the data self-describable.
inline void listfile_write_preamble(WriteHandle &lf_out, const CrateConfig &config)
{
    listfile_write_magic(lf_out, config.connectionType);
    listfile_write_endian_marker(lf_out);
    listfile_write_crate_config(lf_out, config);
}

// Writes a system_event with the given subtype and contents.
//
// This function handles splitting system_events that exceed the maximum
// listfile section size into multiple sections with each section headers
// continue bit set for all but the last section.
void MESYTEC_MVLC_EXPORT listfile_write_system_event(
    WriteHandle &lf_out, u8 subtype,
    const u32 *buffp, size_t totalWords);

// Writes an empty system_event section
void MESYTEC_MVLC_EXPORT listfile_write_system_event(
    WriteHandle &lf_out, u8 subtype);

// Writes a system_event section with the given subtype containing a unix timestamp.
void MESYTEC_MVLC_EXPORT listfile_write_timestamp_section(
    WriteHandle &lf_out, u8 subtype);

//
// reading
//

inline std::vector<u8> read_magic(ReadHandle &rh)
{
    rh.seek(0);
    std::vector<u8> result(get_filemagic_len());
    rh.read(result.data(), result.size());
    return result;
}

struct MESYTEC_MVLC_EXPORT SystemEvent
{
    u8 type;
    std::vector<u8> contents;

    inline std::string contentsToString() const
    {
        if (!contents.empty())
        {
            return std::string(
                reinterpret_cast<const char *>(contents.data()),
                contents.size());
        }

        return {};
    }
};

struct Preamble
{
    // The magic bytes at the start of the file.
    std::string magic;

    // SystemEvent sections as they appear in the listfile.
    std::vector<SystemEvent> systemEvents;

    // Byte offset to seek to so that the next read starts right after the
    // preamble.
    size_t endOffset = 0u;

    // Returns a pointer to the first systemEvent section with the given type
    // or nullptr if no such section exists.
    const SystemEvent *findSystemEvent(u8 type) const
    {
        auto it = std::find_if(
            std::begin(systemEvents), std::end(systemEvents),
            [type] (const auto &sysEvent)
            {
                return sysEvent.type == type;
            });

        return (it != std::end(systemEvents)
                ? &(*it)
                : nullptr);
    }

    const SystemEvent *findCrateConfig() const
    {
        return findSystemEvent(system_event::subtype::MVLCCrateConfig);
    }
};

// An upper limit of the sum of section content sizes for read_preamble().
constexpr size_t PreambleReadMaxSize = util::Megabytes(100);

// Reads up to and including the first system_event::type::BeginRun section or
// a non-SystemEvent frame header (for mvme-1.0.1 files).
//
// Afterwards the ReadHandle is positioned directly after the magic bytes at
// the start of the file. This means the SystemEvent sections making up the
// preamble will be read again and are available for processing by e.g. the
// readout parser.
// throws std::runtime_error
Preamble MESYTEC_MVLC_EXPORT read_preamble(
    ReadHandle &rh, size_t preambleMaxSize = PreambleReadMaxSize);


inline std::vector<u8> get_sysevent_data(const std::vector<SystemEvent> &sysEvents, const u8 sysEventType)
{
    auto begin = std::find_if(std::begin(sysEvents), std::end(sysEvents),
        [sysEventType] (const auto &e) { return e.type == sysEventType; });

    if (begin == std::end(sysEvents))
        return {};

    auto end = std::find_if(begin, std::end(sysEvents),
        [sysEventType] (const auto &e) { return e.type != sysEventType; });

    std::vector<u8> result;

    result.reserve(std::accumulate(begin, end, 0u,
        [] (const auto &accu, const auto &e) { return accu + e.contents.size(); }));

    std::for_each(begin, end,
        [&result] (const auto &e)
        {
            std::copy(std::begin(e.contents), std::end(e.contents),
                std::back_inserter(result));
        });

    return result;
}

// Reading:
// Info from the start of the listfile:
// - FileMagic
//
// - EndianMarker
// - MVMEConfig
// - MVLCConfig
//
// read complete section into buffer (including continuations)

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MESYTEC_MVLC_MVLC_LISTFILE_H__ */
