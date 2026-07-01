// mesytec-mvlc - driver library for the Mesytec MVLC VME controller
//
// SPDX-FileCopyrightText: 2025 mesytec GmbH & Co. KG
// SPDX-FileContributor: Florian Lüke <f.lueke@mesytec.com>
//
// SPDX-License-Identifier: BSL-1.0

#ifndef E8D6A3D4_9A71_4B1C_B4E0_55FF88FAC1E6
#define E8D6A3D4_9A71_4B1C_B4E0_55FF88FAC1E6

#include <algorithm>
#include <iterator>
#include <variant>

#include <mesytec-mvlc/mesytec-mvlc_export.h>
#include <mesytec-mvlc/mvlc_trigger_io_port.h>

// Abstractions and helpers for serializing the TriggerIO structure.

// This is generic code producing a list of 'ScriptPart' objects that can be
// iterated by concrete serializers, e.g. the one producing mvme VMEScript.
// A helper IScriptParserVisitor interface is provided with the accompanying
// 'visit' function.
// A step that's directly implemented is the serialization of meta info (unit
// names and 'soft activate' flags) to YAML.

namespace mesytec::mvlc::trigger_io
{

struct MESYTEC_MVLC_EXPORT RegisterWrite
{
    // Opt_HexValue indicates that the register value should be printed in
    // hexadecimal instead of decimal.
    static const unsigned Opt_HexValue = 1u << 0;

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

    RegisterWrite() = default;

    RegisterWrite(u16 address_, u16 value_, const std::string &comment_ = {},
                  unsigned options_ = 0u)
        : address(address_)
        , value(value_)
        , comment(comment_)
        , options(options_)
    {
    }

    RegisterWrite(u16 address_, u16 value_, unsigned options_)
        : address(address_)
        , value(value_)
        , options(options_)
    {
    }
};

// Basic part of the script: either a register write or a block comment.
using BasicPart = std::variant<RegisterWrite, std::string>;
using BasicParts = std::vector<BasicPart>;

// Represents a single DSO unit in the script. In the output script this is
// enclosed mvlc_stack_begin/end so that the unit setup is atomic (happening in
// a single stack transaction).
struct MESYTEC_MVLC_EXPORT UnitBlock
{
    std::string comment;
    BasicParts parts;

    void operator+=(const BasicPart &part) { parts.push_back(part); }

    void operator+=(const BasicParts &parts_)
    {
        std::copy(std::begin(parts_), std::end(parts_), std::back_inserter(parts));
    }
};

// Top level parts of the script: basic parts, comment strings or unit blocks.
using ScriptPart = std::variant<RegisterWrite, std::string, UnitBlock>;
using ScriptParts = std::vector<ScriptPart>;

ScriptParts MESYTEC_MVLC_EXPORT generate_trigger_io_parts(const TriggerIO &ioCfg);

struct MESYTEC_MVLC_EXPORT IScriptPartVisitor
{
    virtual void operator()(const RegisterWrite &write) = 0;
    virtual void operator()(const std::string &blockComment) = 0;
    virtual void operator()(const UnitBlock &ub) = 0;
    virtual ~IScriptPartVisitor() = default;
};

// Visits units in the given ScriptParts in order. For each unit the statements
// needed to select and initialize the unit and additional comments are
// generated. Concrete visitors can process these parts and generate appropriate
// output code or structures.
inline void visit(const ScriptParts &parts, IScriptPartVisitor &visitor)
{
    std::for_each(std::begin(parts), std::end(parts), [&visitor](const ScriptPart &part)
                  { std::visit([&visitor](auto &&arg) { visitor(arg); }, part); });
}

// Visits units in the TriggerIO structure in level order. For each unit the
// statements needed to select and initialize the unit and additional comments
// are generated. Concrete visitors can process these parts and generate
// appropriate output code or structures.
inline void visit(const TriggerIO &ioCfg, IScriptPartVisitor &visitor)
{
    visit(generate_trigger_io_parts(ioCfg), visitor);
}

// Serializes the unit names and 'soft activate' flags to a YAML string.
// The reverse is apply_meta_info_yaml() in the deserializer.
std::string MESYTEC_MVLC_EXPORT generate_meta_info_yaml(const TriggerIO &ioCfg);

} // namespace mesytec::mvlc::trigger_io

#endif /* E8D6A3D4_9A71_4B1C_B4E0_55FF88FAC1E6 */
