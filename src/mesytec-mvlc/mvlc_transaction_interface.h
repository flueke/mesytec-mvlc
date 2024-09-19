/* mesytec-mvlc - driver library for the Mesytec MVLC VME controller
 *
 * Copyright (c) 2020-2023 mesytec GmbH & Co. KG
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef A6BAF15A_17CD_4AA9_BA7E_6C27081B22A9
#define A6BAF15A_17CD_4AA9_BA7E_6C27081B22A9

#include <system_error>
#include <vector>

#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mvlc_basic_interface.h"
#include "mvlc_stack_errors.h"
#include "util/int_types.h"

namespace mesytec::mvlc
{

struct MESYTEC_MVLC_EXPORT CmdPipeCounters
{
    size_t reads;
    size_t bytesRead;
    size_t timeouts;
    size_t invalidHeaders;
    size_t wordsSkipped;
    size_t errorBuffers;
    size_t superBuffers;
    size_t stackBuffers;
    size_t dsoBuffers;

    size_t shortSuperBuffers;
    size_t superFormatErrors;
    size_t superRefMismatches;
    size_t stackRefMismatches;
};

class SuperCommandBuilder;
class StackCommandBuilder;

class MvlcTransactionInterface
{
    public:
        //MvlcTransactionInterface() = default;
        virtual ~MvlcTransactionInterface() = default;
        //MvlcTransactionInterface(const MvlcTransactionInterface &) = delete;
        //MvlcTransactionInterface &operator=(const MvlcTransactionInterface &) = delete;

        virtual MvlcBasicInterface *getImpl() = 0;
        virtual std::error_code superTransaction(const SuperCommandBuilder &superBuilder, std::vector<u32> &dest) = 0;
        virtual std::error_code stackTransaction(const StackCommandBuilder &stackBuilder, std::vector<u32> &dest) = 0;
        virtual u16 nextSuperReference() = 0;
        virtual u32 nextStackReference() = 0;
        virtual CmdPipeCounters getCmdPipeCounters() const = 0;
        virtual StackErrorCounters getStackErrorCounters() const = 0;
        virtual void resetStackErrorCounters() = 0;
};

std::error_code upload_stack(MvlcTransactionInterface *trxImpl,
    u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<u32> &stackContents);

class MvlcCoreInterface;

std::error_code upload_stack(MvlcCoreInterface *mvlc,
    u8 stackOutputPipe, u16 stackMemoryOffset, const std::vector<u32> &stackContents);

}

#endif /* A6BAF15A_17CD_4AA9_BA7E_6C27081B22A9 */
