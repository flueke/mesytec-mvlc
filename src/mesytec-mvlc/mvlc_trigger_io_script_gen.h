// mesytec-mvlc - driver library for the Mesytec MVLC VME controller
//
// SPDX-FileCopyrightText: 2025 mesytec GmbH & Co. KG
// SPDX-FileContributor: Florian Lüke <f.lueke@mesytec.com>
//
// SPDX-License-Identifier: BSL-1.0

#ifndef E8D6A3D4_9A71_4B1C_B4E0_55FF88FAC1E6
#define E8D6A3D4_9A71_4B1C_B4E0_55FF88FAC1E6

#include <variant>

#include <mesytec-mvlc/mesytec-mvlc_export.h>
#include <mesytec-mvlc/mvlc_trigger_io_port.h>

namespace mesytec::mvlc::trigger_io
{

// TODO: rename to RegisterWrite
struct Write
{
    // Opt_HexValue indicates that the register value should be printed in
    // hexadecimal instead of decimal.
    static const unsigned Opt_HexValue = 1u << 0;;

    // Opt_BinValue indicates that the register value should be printed in
    // binary (0bxyz literal) instead of decimal.
    static const unsigned Opt_BinValue = 1u << 1;

    // Relative register address. Only the low two bytes are stored.
    u16 address;

    // 16 bit MVLC register value.
    u16 value;

    // Comment written one the same line as the write.
    std::string comment;

    // OR of the Opt_* constants defined above.
    unsigned options = 0u;

    Write() = default;

    Write(u16 address_, u16 value_, const std::string &comment_ = {}, unsigned options_ = 0u)
        : address(address_)
        , value(value_)
        , comment(comment_)
        , options(options_)
    {}

    Write(u16 address_, u16 value_, unsigned options_)
        : address(address_)
        , value(value_)
        , options(options_)
    {}
};

// Basic part of the script: either a register write or a block comment.
using BasicPart = std::variant<Write, std::string>;
using BasicParts = std::vector<BasicPart>;

// Represents a single DSO unit in the script. In the output script this is
// enclosed mvlc_stack_begin/end so that the unit setup is atomic (happening in
// a single stack transaction).
struct UnitBlock
{
    std::string comment;
    std::vector<BasicPart> parts;

    void operator+=(const BasicPart &part)
    {
        parts.push_back(part);
    }

    void operator+=(const BasicParts &parts_)
    {
        std::copy(std::begin(parts_), std::end(parts_),
                  std::back_inserter(parts));
    }
};

// Top level parts of the script: basic parts or unit blocks.
using ScriptPart = std::variant<Write, std::string, UnitBlock>;
using ScriptParts = std::vector<ScriptPart>;

ScriptParts generate_trigger_io_script(const TriggerIO &ioCfg);

struct VisitorInterface
{
    virtual void operator()(const Write &write) = 0;
    virtual void operator()(const std::string &blockComment) = 0;
    virtual void operator()(const UnitBlock &ub) = 0;
    virtual ~VisitorInterface() = default;
};

void visit(const ScriptParts &parts, VisitorInterface &visitor);
void visit(const TriggerIO &ioCfg, VisitorInterface &visitor);

}

#endif /* E8D6A3D4_9A71_4B1C_B4E0_55FF88FAC1E6 */
