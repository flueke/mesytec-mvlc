#ifndef B4B9E968_92E1_437E_887E_F3575206ACCC
#define B4B9E968_92E1_437E_887E_F3575206ACCC

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "util/int_types.h"

#include <cassert>

namespace mesytec::mvlc::stream
{

// Protocol definitions for the framed streaming protocol used by
// MvmeStreamServer. If the unframed format is used, none of this applies and
// raw MVLC readout data is streamed unmodified.
//
// In framed format each frame is preceded by two header words in little endian
// order. The first word contains a frame type field and the number of 32-bit
// words following the 2nd header word.
// The 2nd word contains a sequence number for data frames or a version number
// for config frames.

// clang-format off
enum class FrameType: u8
{
    // A configuration frame, e.g. the preamble containing the CrateConfig YAML data.
    // The sequence number field contains the version number of the configuration data.
    Config = 0b01,

    // A data frame containing readout data. The sequence number field contains
    // the source buffer number produced by the readout code. This allows to
    // detect and handle source loss occuring if a best-effort type transport us
    // used.
    Data   = 0b10,
};

struct FrameHeader
{
    // word0
    static constexpr u32 FrameTypeShift     = 28;
    static constexpr u32 FrameTypeMask      = 0b1111;
    static constexpr u32 FrameLenShift      = 0;
    static constexpr u32 FrameLenMask       = 0x0fffffffu;

    // word1
    static constexpr u32 FrameSeqVerShift   = 0;
    static constexpr u32 FrameSeqVerMask    = 0xffffffffu;
// clang-format on

    u32 word0 = 0; // type and len
    u32 word1 = 0; // sequence number or config version, depending on the frame type

    FrameType frameType() const
    {
        return static_cast<FrameType>((word0 >> FrameTypeShift) & FrameTypeMask);
    }
    u32 wordsInFrame() const { return (word0 >> FrameLenShift) & FrameLenMask; }
    u32 seqOrVersion() const { return (word1 >> FrameSeqVerShift) & FrameSeqVerMask; }

    void setFrameType(FrameType type)
    {
        word0 = (word0 & ~(FrameTypeMask << FrameTypeShift)) | (static_cast<u32>(type) << FrameTypeShift);
    }

    void setSeqOrVersion(u32 seqOrVersion)
    {
        word1 = (word1 & ~(FrameSeqVerMask << FrameSeqVerShift)) |
                ((seqOrVersion & FrameSeqVerMask) << FrameSeqVerShift);
    }
    void setWordsInFrame(u32 words)
    {
        assert(words <= FrameLenMask);
        word0 = (word0 & ~(FrameLenMask << FrameLenShift)) | ((words & FrameLenMask) << FrameLenShift);
    }
};

} // namespace mesytec::mvlc

#endif /* B4B9E968_92E1_437E_887E_F3575206ACCC */
