#ifndef __MESYTEC_MVLC_MVLC_LISTFILE_H__
#define __MESYTEC_MVLC_MVLC_LISTFILE_H__

#include <algorithm>

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

class MESYTEC_MVLC_EXPORT WriteHandle
{
    public:
        virtual ~WriteHandle();
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

// Writes the magic bytes, an endian marker and the CrateConfig to the write
// handle.
void MESYTEC_MVLC_EXPORT listfile_write_preamble(WriteHandle &lf_out, const CrateConfig &config);

void MESYTEC_MVLC_EXPORT listfile_write_magic(WriteHandle &lf_out, ConnectionType ct);
void MESYTEC_MVLC_EXPORT listfile_write_endian_marker(WriteHandle &lf_out);
void MESYTEC_MVLC_EXPORT listfile_write_crate_config(WriteHandle &lf_out, const CrateConfig &config);

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
        return std::string(
            reinterpret_cast<const char *>(contents.data()),
            contents.size());
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
Preamble MESYTEC_MVLC_EXPORT read_preamble(
    ReadHandle &rh, size_t preambleMaxSize = PreambleReadMaxSize);

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
