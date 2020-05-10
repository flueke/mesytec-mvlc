#ifndef __MESYTEC_MVLC_MVLC_LISTFILE_H__
#define __MESYTEC_MVLC_MVLC_LISTFILE_H__

#include "mesytec-mvlc_export.h"

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

void MESYTEC_MVLC_EXPORT listfile_write_preamble(WriteHandle &lf_out, const CrateConfig &config);
void MESYTEC_MVLC_EXPORT listfile_write_magic(WriteHandle &lf_out, ConnectionType ct);
void MESYTEC_MVLC_EXPORT listfile_write_endian_marker(WriteHandle &lf_out);
void MESYTEC_MVLC_EXPORT listfile_write_crate_config(WriteHandle &lf_out, const CrateConfig &config);
void MESYTEC_MVLC_EXPORT listfile_write_system_event(
    WriteHandle &lf_out, u8 subtype,
    const u32 *buffp, size_t totalWords);

// Writes an empty system section
void MESYTEC_MVLC_EXPORT listfile_write_system_event(WriteHandle &lf_out, u8 subtype);

void MESYTEC_MVLC_EXPORT listfile_write_timestamp(WriteHandle &lf_out);

inline size_t listfile_write_raw(WriteHandle &lf_out, const u8 *buffer, size_t size)
{
    return lf_out.write(buffer, size);
}

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

    inline std::string toString() const
    {
        return std::string(
            reinterpret_cast<const char *>(contents.data()),
            contents.size());
    }
};

constexpr size_t PreambleMaxSize = Megabytes(100);

// Reads up to and including the first system_event::type::BeginRun section.
std::vector<SystemEvent> MESYTEC_MVLC_EXPORT read_preamble(ReadHandle &rh);

// Reading:
// Info from the start of the listfile:
// - FileMagic
//
// - EndianMarker
// - MVMEConfig
// - MVLCCOnfig
//
// read complete section into buffer (including continuations)
// 

} // end namespace listfile
} // end namespace mvlc
} // end namespace mesytec

#endif /* __MVLC_LISTFILE_H__ */
