#ifndef B4B9E968_92E1_437E_887E_F3575206ACCC
#define B4B9E968_92E1_437E_887E_F3575206ACCC

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mesytec-mvlc/util/int_types.h"

namespace mesytec::mvlc
{

// clang-format off
enum class FrameType: uint8_t
{
    Config = 0b01,
    Data   = 0b10,
};

struct FrameHeader
{
    static constexpr u32 FrameTypeShift     = 30;
    static constexpr u32 FrameTypeMask      = 0b11;
    static constexpr u32 FrameSeqVerShift   = 15;
    static constexpr u32 FrameSeqVerMask    = 0x7fff;
    static constexpr u32 FrameLenShift      = 0;
    static constexpr u32 FrameLenMask       = 0x7fff;
    // clang-format on

    u32 raw = 0;

    FrameType frameType() const
    {
        return static_cast<FrameType>((raw >> FrameTypeShift) & FrameTypeMask);
    }
    u32 seqOrVersion() const { return (raw >> FrameSeqVerShift) & FrameSeqVerMask; }
    u32 wordsInFrame() const { return (raw >> FrameLenShift) & FrameLenMask; }

    void setFrameType(FrameType type)
    {
        raw =
            (raw & ~(FrameTypeMask << FrameTypeShift)) | (static_cast<u32>(type) << FrameTypeShift);
    }
    void setSeqOrVersion(u32 seqOrVersion)
    {
        raw = (raw & ~(FrameSeqVerMask << FrameSeqVerShift)) |
              ((seqOrVersion & FrameSeqVerMask) << FrameSeqVerShift);
    }
    void setWordsInFrame(u32 words)
    {
        raw = (raw & ~(FrameLenMask << FrameLenShift)) | ((words & FrameLenMask) << FrameLenShift);
    }
};

} // namespace mesytec::mvlc

#endif /* B4B9E968_92E1_437E_887E_F3575206ACCC */
