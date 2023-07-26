#ifndef MESYTEC_MVLC_SRC_MESYTEC_MVLC_MVLC_CORE_INTERFACE_H
#define MESYTEC_MVLC_SRC_MESYTEC_MVLC_MVLC_CORE_INTERFACE_H

namespace mesytec::mvlc
{

class MvlcCoreInterface
{
public:
    virtual ~MvlcCoreInterface() {}

    std::error_code readRegister(u16 address, u32 &value);
    std::error_code writeRegister(u16 address, u32 value);

    std::error_code vmeRead(u32 address, u32 &value, u8 amod, VMEDataWidth dataWidth);
    std::error_code vmeWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth);

    std::error_code vmeBlockRead(u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest); // BLT, MBLT
    std::error_code vmeBlockRead(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest); // 2eSST
};

}

#endif // MESYTEC_MVLC_SRC_MESYTEC_MVLC_MVLC_CORE_INTERFACE_H
