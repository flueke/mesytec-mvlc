#ifndef MESYTEC_MVLC_SRC_MESYTEC_MVLC_MVLC_CORE_INTERFACE_H
#define MESYTEC_MVLC_SRC_MESYTEC_MVLC_MVLC_CORE_INTERFACE_H

#include <system_error>
#include <vector>

#include "mvlc_constants.h"
#include "mvlc_basic_interface.h"
#include "mvlc_transaction_interface.h"

namespace mesytec::mvlc
{

class MvlcCoreInterface: public MvlcTransactionInterface
{
public:
    virtual ~MvlcCoreInterface() {}

    virtual Locks &getLocks() = 0; // MvlcCoreInterface implementations must be thread-safe for now!
    virtual MvlcTransactionInterface *getTransactionImpl() = 0;
    MvlcBasicInterface *getImpl() { return getTransactionImpl()->getImpl(); }

    // register / internal memory access
    virtual std::error_code readRegister(u16 address, u32 &value) = 0;
    virtual std::error_code writeRegister(u16 address, u32 value) = 0;

    virtual std::error_code uploadStack(u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<u32> &stackContents) = 0;

    virtual std::error_code vmeRead(u32 address, u32 &value, u8 amod, VMEDataWidth dataWidth) = 0;
    virtual std::error_code vmeWrite(u32 address, u32 value, u8 amod, VMEDataWidth dataWidth) = 0;

    // BLT, MBLT
    virtual std::error_code vmeBlockRead(u32 address, u8 amod, u16 maxTransfers, std::vector<u32> &dest) = 0;

    // 2eSST
    virtual std::error_code vmeBlockRead(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers, std::vector<u32> &dest) = 0;

    // Swaps the two 32-bit words for 64-bit reads. amod must be one of the MBLT amods!
    virtual std::error_code vmeBlockReadSwapped(u32 address, u8 amod, u16 maxTransfers,
        std::vector<u32> &dest, bool fifo = true) = 0;

    virtual std::error_code vmeBlockReadSwapped(u32 address, const Blk2eSSTRate &rate, u16 maxTransfers,
        std::vector<u32> &dest, bool fifo = true) = 0;

    // Part of MvlcBasicInterface except for read(), write() and setDisableTriggersOnConnect()
    virtual std::error_code connect() = 0;
    virtual std::error_code disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual ConnectionType connectionType() const = 0;
    virtual std::string connectionInfo() const = 0;

    // MvlcTransactionInterface
    //virtual std::error_code superTransaction(const SuperCommandBuilder &superBuilder, std::vector<u32> &dest) = 0;
    //virtual std::error_code stackTransaction(const StackCommandBuilder &stackBuilder, std::vector<u32> &dest) = 0;
};

}

#endif // MESYTEC_MVLC_SRC_MESYTEC_MVLC_MVLC_CORE_INTERFACE_H
