#include "mvlc_trigger_io_serialize_mvlcscript.h"

#include <sstream>

#include <mesytec-mvlc/git_version.h>
#include <mesytec-mvlc/mesytec-mvlc_export.h>
#include <mesytec-mvlc/mvlc_command_builders.h>
#include <mesytec-mvlc/mvlc_readout_config.h> // stack_command_builder_from_yaml
#include <mesytec-mvlc/mvlc_trigger_io_deserialize.h>
#include <mesytec-mvlc/mvlc_trigger_io_port.h>
#include <mesytec-mvlc/mvlc_trigger_io_serialize.h>
#include <mesytec-mvlc/util/logging.h>
#include <mesytec-mvlc/util/overloaded.h>
#include <mesytec-mvlc/util/string_util.h>

using namespace mesytec::mvlc;

namespace mesytec::mvlc::trigger_io
{

struct StackCommandBuilderGenerator: public IScriptPartVisitor
{
    StackCommandBuilder builder;

    StackCommandBuilderGenerator() { builder.setName("init_trigger_io"); }

    void operator()(const RegisterWrite &write) override
    {
        builder.addVMEWrite(SelfVMEAddress + write.address, write.value, 0x0d, VMEDataWidth::D16);
        if (auto cmd = builder.getLastCommand())
            cmd->get().comment = write.comment;
    }

    void operator()(const std::string & /*blockComment*/) override
    {
        // Ignoring these. They are things like "Level 0" which are copied to
        // the generated VMEScript.
    }

    void operator()(const UnitBlock &ub) override
    {
        builder.beginGroup(ub.comment);
        for (const auto &part: ub.parts)
        {
            // clang-format off
            std::visit(mesytec::mvlc::util::overloaded
            {
                [this](const RegisterWrite &write)
                {
                    this->operator()(write);
                },
                [](const std::string &/*comment*/)
                {
                    // Ignore comments that are parts of unit blocks. These are
                    // used to generate the standalone comments like
                    // "  # L2.LUT0 strobe gate generator". We don't need these
                    // here.
                }
            }, part);
            // clang-format on
        }
    }
};

StackCommandBuilder generate_trigger_io_command_builder(const TriggerIO &ioCfg)
{
    StackCommandBuilderGenerator gen;

    visit(generate_trigger_io_parts(ioCfg), gen);
    gen.builder.setYamlMeta("trigger_io_meta", generate_meta_info_yaml(ioCfg));
    return gen.builder;
}

TriggerIO parse_trigger_io_command_builder(const StackCommandBuilder &builder)
{
    std::vector<WriteCommand> writes;

    // Don't care at all about any internal group structure. So a flat list of
    // writes contained in a single group does work, aswell as the new
    // unit-per-group style.
    for (const auto &cmd: builder.getCommands())
    {
        if (cmd.type != StackCommand::CommandType::VMEWrite)
            continue;

        writes.push_back(WriteCommand{static_cast<u32>(cmd.address), static_cast<u32>(cmd.value)});
    }

    return deserialize_trigger_io(writes, builder.getYamlMeta("trigger_io_meta"));
}

TriggerIO parse_trigger_io_command_builder(const std::string &yamlText)
{
    return parse_trigger_io_command_builder(stack_command_builder_from_yaml(yamlText));
}

// Note: could reuse the StackCommandBuilderGenerator from above, but this way
// provided more control over comment output.
struct MvlcScriptGenerator: public IScriptPartVisitor
{
    std::ostringstream oss;

    void operator()(const RegisterWrite &write) override
    {
        oss << fmt::format("vme_write 0x0d d16 {:#010x} {:#010x} {}",
                           SelfVMEAddress + write.address, write.value,
                           write.comment.empty() ? "" : fmt::format("# {}", write.comment))
            << "\n";
    }

    void operator()(const std::string &blockComment) override
    {
        oss << "# " << blockComment << "\n";
    }

    void operator()(const UnitBlock &ub) override
    {
        oss << "# " << ub.comment << "\n";
        for (const auto &part: ub.parts)
        {
            // clang-format off
            std::visit(mesytec::mvlc::util::overloaded
            {
                [this](const RegisterWrite &write)
                {
                    this->operator()(write);
                },
                [](const std::string &/*comment*/)
                {
                    // Ignore comments that are parts of unit blocks. These are
                    // used to generate the standalone comments like
                    // "  # L2.LUT0 strobe gate generator". We don't need these
                    // here.
                }
            }, part);
            // clang-format on
        }

        oss << "\n";
    }
};

static const std::string MvlcScriptMetaBegin = "# TriggerIO Meta Info:";

std::string generate_trigger_io_mvlcscript(const TriggerIO &ioCfg)
{
    // clang-format off
    std::vector<std::string> intro
    {
        "##############################################################",
        "# MVLC Trigger I/O init using mvlcscript commands.           #",
        "##############################################################",
        "",
        fmt::format("# Generated by mesytec-mvlc driver version {}", library_version()),
        "",
        "# Note: the write commands needed to select and initialize",
        "# each unit are not atomic! If another process is touching",
        "# the Trigger I/O system (e.g. a DSO readout) at the same",
        "# time this script is run things will break!",
        "",
        "# The commented out Meta Info block after the init commands",
        "# is YAML formatted. The same structure is used in mvme.",
        "",
    };

    // clang-format on
    MvlcScriptGenerator gen;
    std::copy(intro.begin(), intro.end(), std::ostream_iterator<std::string>(gen.oss, "\n"));

    visit(generate_trigger_io_parts(ioCfg), gen);

    gen.oss << MvlcScriptMetaBegin << "\n";
    std::istringstream iss(generate_meta_info_yaml(ioCfg));
    std::string line;
    while (std::getline(iss, line))
    {
        gen.oss << "# " << line << "\n";
    }

    return gen.oss.str();
}

inline void strip_comment(std::string &line)
{
    if (line.size() < 2 || line[0] != '#')
        return;

    // Strip leading "# "
    if (line.size() >= 2 && line[1] == ' ')
        line = line.substr(2);
    else
        line = line.substr(1);
}

TriggerIO parse_trigger_io_mvlcscript(const std::string &text)
{
    std::vector<WriteCommand> writes;
    std::string metaYaml;
    std::istringstream iss(text);
    std::string line;

    while (std::getline(iss, line))
    {
        // check for the beginning of the meta info block
        if (util::startswith(line, MvlcScriptMetaBegin))
        {
            // Read to eof or whatever and interpret the stripped contents as
            // YAML.
            while (std::getline(iss, line))
            {
                if (line.size() < 2 || line[0] != '#')
                    continue;

                strip_comment(line);
                metaYaml += line + "\n";
            }
        }
        // Ignore non-meta, comment lines
        else if (line.empty() || line[0] == '#')
        {
            continue;
        }
        // Handle lines containing VME write commands
        else
        {
            auto stackCmd = stack_command_from_string(line);

            if (stackCmd.type != StackCommand::CommandType::VMEWrite)
                continue;

            writes.push_back({stackCmd.address, stackCmd.value});

            strip_comment(line);
        }
    }

    return deserialize_trigger_io(writes, metaYaml);
}

} // namespace mesytec::mvlc::trigger_io
