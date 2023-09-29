#ifndef __MESYTEC_MVLC_VME_CONSTANTS_H__
#define __MESYTEC_MVLC_VME_CONSTANTS_H__

#include "util/int_types.h"

namespace mesytec
{
namespace mvlc
{

//
// The standard VME address modifiers.
//
namespace vme_amods
{
    // a32
    static const u8 a32UserData    = 0x09;
    static const u8 a32UserProgram = 0x0A;
    static const u8 a32UserBlock   = 0x0B;
    static const u8 a32UserBlock64 = 0x08;

    static const u8 a32PrivData    = 0x0D;
    static const u8 a32PrivProgram = 0x0E;
    static const u8 a32PrivBlock   = 0x0F;
    static const u8 a32PrivBlock64 = 0x0C;

    // a24
    static const u8 a24UserData    = 0x39;
    static const u8 a24UserProgram = 0x3A;
    static const u8 a24UserBlock   = 0x3B;

    static const u8 a24PrivData    = 0x3D;
    static const u8 a24PrivProgram = 0x3E;
    static const u8 a24PrivBlock   = 0x3F;

    static const u8 cr             = 0x2F;

    // a16
    static const uint8_t a16User   = 0x29;
    static const uint8_t a16Priv   = 0x2D;

    // default modes
    static const u8 A16         = a16User;
    static const u8 A24         = a24UserData;
    static const u8 A32         = a32UserData;
    static const u8 BLT32       = a32UserBlock;
    static const u8 MBLT64      = a32UserBlock64;
    static const u8 Blk2eSST64  = 0x20;

    // aliases
    static const u8 BLT         = BLT32;
    static const u8 MBLT        = MBLT64;

    // amods are 6 bits wide
    static const unsigned VmeAmodMask = 0b111111u;

    inline bool is_block_mode(u8 amod)
    {
        switch (amod & VmeAmodMask)
        {
            case a32UserBlock:
            case a32UserBlock64:
            case a32PrivBlock:
            case a32PrivBlock64:
            case a24UserBlock:
            case a24PrivBlock:
            case Blk2eSST64:
                return true;
        }

        return false;
    }

    inline bool is_blt_mode(u8 amod)
    {
        switch (amod & VmeAmodMask)
        {
            case a32UserBlock:
            case a32PrivBlock:
            case a24UserBlock:
            case a24PrivBlock:
                return true;
        }

        return false;
    }

    inline bool is_mblt_mode(u8 amod)
    {
        switch (amod & VmeAmodMask)
        {
            case a32UserBlock64:
            case a32PrivBlock64:
                return true;
        }

        return false;
    }

    inline bool is_esst64_mode(u8 amod)
    {
        return ((amod & VmeAmodMask) == Blk2eSST64);
    }
}
}
}

#endif /* __MESYTEC_MVLC_VME_CONSTANTS_H__ */
