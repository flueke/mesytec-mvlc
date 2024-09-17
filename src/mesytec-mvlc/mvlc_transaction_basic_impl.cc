#include "mvlc_transaction_basic_impl.h"

#include <cassert>

#include "mvlc_error.h"
#include "mvlc_command_builders.h"

namespace mesytec::mvlc
{

std::error_code MvlcTransactionBasicImpl::superTransaction(const SuperCommandBuilder &superBuilder, std::vector<u32> &dest)
{
    assert(!superBuilder.empty() && superBuilder[0].type == SuperCommandType::ReferenceWord);

    if (superBuilder.empty())
        return make_error_code(MVLCErrorCode::SuperFormatError);

    if (superBuilder[0].type != SuperCommandType::ReferenceWord)
        return make_error_code(MVLCErrorCode::SuperFormatError);

    u16 superRef = superBuilder[0].value;

    //auto guard = d->locks_.lockCmd();
    //return d->resultCheck(d->cmdApi_.superTransaction(superRef, make_command_buffer(superBuilder), dest));
    return {};
}

std::error_code MvlcTransactionBasicImpl::stackTransaction(const StackCommandBuilder &stackBuilder, std::vector<u32> &dest)
{
}

}
