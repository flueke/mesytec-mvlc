#include <mesytec-mvlc/git_version.h>
#include <mesytec-mvlc/mvlc_trigger_io_serialize_mvlcscript.h>
#include <mesytec-mvlc/mvlc_trigger_io_serialize_util.h>
#include <mesytec-mvlc/mvlc_trigger_io_serialize_vmescript.h>
#include <mesytec-mvlc/util/fmt.h>
#include <mesytec-mvlc/util/string_util.h>

#include <argh.h>
#include <array>
#include <cassert>
#include <fstream>
#include <iostream>
#include <numeric>

using namespace mesytec::mvlc;
using namespace mesytec::mvlc::trigger_io;

// Source: perplexity.ai
std::string escape_underscores(std::string s)
{
    for (std::size_t pos = 0; (pos = s.find('_', pos)) != std::string::npos;)
    {
        s.insert(pos, "\\"); // insert backslash before '_'
        pos += 2;            // skip over the newly inserted "\_"
    }
    return s;
}

std::string make_coldef(size_t columns)
{
    std::string coldef = "|";
    for (int i = 0; i < columns; ++i)
    {
        coldef += "l|";
    }
    return coldef;
}

void print_table_header(std::ostream &oss, const std::string &caption,
                        const std::string &label = {},
                        std::vector<std::string> columnTitles = {"Register", "Description"})
{
    fmt::println(oss, "\\begin{{table}}[H]");
    fmt::println(oss, "    \\centering");
    fmt::println(oss, "    \\caption{{{}}}", escape_underscores(caption));
    if (!label.empty())
    {
        fmt::println(oss, "    \\label{{{}}}", label); // do _not_ escape underscores in labels :)
    }
    fmt::println(oss, "    \\begin{{tabular}}{{{}}}", make_coldef(columnTitles.size()));
    fmt::print(oss, "        \\hline");

    std::vector<std::string> formattedColumnTitles;

    for (const auto &title: columnTitles)
    {
        formattedColumnTitles.push_back(fmt::format("\\textbf{{{}}}", escape_underscores(title)));
    }

    fmt::println(oss, " {} \\\\", fmt::join(formattedColumnTitles, " & "));
}

void print_table_footer(std::ostream &oss)
{
    fmt::println(oss, "        \\hline");
    fmt::println(oss, "    \\end{{tabular}}");
    fmt::println(oss, "\\end{{table}}");
    fmt::println(oss, "");
}

void print_table_contents(std::ostream &oss, const BasicParts &parts)
{
    for (const auto &part: parts)
    {
        if (auto regWrite = std::get_if<RegisterWrite>(&part))
        {
            fmt::println(oss, "        \\hline 0x{:04X} & {}\\\\", regWrite->address,
                         escape_underscores(regWrite->comment));
        }
    }
}

void gen_timer_table(std::ostream &oss)
{
    auto parts = generate(Timer{}, 0);

    print_table_header(oss, "Timer properties", "table:trigger_io_timer");
    print_table_contents(oss, parts);
    print_table_footer(oss);
}

void gen_nim_io_table(std::ostream &oss)
{
    auto parts = generate(IO{}, io_flags::NIM_IO_Flags, 0);

    print_table_header(oss, "NIM I/O properties", "table:trigger_io_nim_io");
    print_table_contents(oss, parts);
    print_table_footer(oss);
}

void gen_ecl_io_table(std::ostream &oss)
{
    auto parts = generate(IO{}, io_flags::ECL_IO_Flags, 0);

    print_table_header(oss, "ECL out properties", "table:trigger_io_ecl_out");
    print_table_contents(oss, parts);
    print_table_footer(oss);
}

void gen_irq_io_table(std::ostream &oss)
{
    auto parts = generate(IO{}, io_flags::None, 0);

    print_table_header(oss, "IRQ out properties", "table:trigger_io_irq_out");
    print_table_contents(oss, parts);
    print_table_footer(oss);
}

void gen_trigger_resource_table(std::ostream &oss)
{
    auto parts = generate(TriggerResource{}, 0);

    print_table_header(oss, "TriggerResource properties", "table:trigger_io_trigger_resource");
    print_table_contents(oss, parts);
    print_table_footer(oss);
}

void gen_stackbusy_table(std::ostream &oss)
{
    auto parts = generate(StackBusy{}, 0);

    print_table_header(oss, "StackBusy properties", "table:trigger_io_stack_busy");
    print_table_contents(oss, parts);
    print_table_footer(oss);
}

void gen_lut_table(std::ostream &oss)
{
    auto parts = write_lut(LUT{});

    print_table_header(oss, "LUT properties", "table:trigger_io_lut");
    print_table_contents(oss, parts);

    // Strobe gate generator
    fmt::println(oss,
                 "        \\hline \\multicolumn{{2}}{{|c|}}{{\\textbf{{If Has Strobe}}}} \\\\");
    auto strobeParts = generate(IO{}, io_flags::None, LUT::StrobeGGIOOffset);
    strobeParts.push_back(
        write_unit_reg(0x20, 0, "strobed_outputs (3 bit mask)", RegisterWrite::Opt_BinValue));
    print_table_contents(oss, strobeParts);

    // Dynamic inputs
    fmt::println(
        oss, "        \\hline \\multicolumn{{2}}{{|c|}}{{\\textbf{{If Has Input Choices}}}} \\\\");
    BasicParts connectionParts;

    for (size_t input = 0; input < LUT_DynamicInputCount; input++)
    {
        u16 regOffset = input * 2;

        connectionParts.push_back(write_connection(regOffset, 0));
    }

    print_table_contents(oss, connectionParts);

    print_table_footer(oss);
}

void gen_stackstart_table(std::ostream &oss)
{
    auto parts = generate(StackStart{}, 0);

    print_table_header(oss, "StackStart properties", "table:trigger_io_stack_start");
    print_table_contents(oss, parts);
    print_table_footer(oss);
}

// This is for the 'MasterTrigger' structure. It is going to be refactored and
// called SyncTrigger or SyncTriggerGenerator or similar.
void gen_synctrigger_table(std::ostream &oss)
{
    auto parts = generate(MasterTrigger{}, 0);

    print_table_header(oss, "SyncTrigger properties", "table:trigger_io_sync_trigger");
    print_table_contents(oss, parts);
    print_table_footer(oss);
}

void gen_counter_table(std::ostream &oss)
{
    auto parts = generate(Counter{}, 0);

    print_table_header(oss, "Counter properties", "table:trigger_io_counter");
    print_table_contents(oss, parts);
    print_table_footer(oss);
}

void gen_dso_tables(std::ostream &oss)
{
    // The DSO is not part of the TriggerIO structure as it's not used in the
    // same context. Add it manually here.
    // clang-format off
    oss <<
R"(
    \begin{table}[H]
        \centering
        \caption{DSO properties}
        \label{table::trigger_io_dso}
        \begin{tabular}{|l|l|}
            \hline \textbf{Register} & \textbf{Description} \\
            \hline 0x0300 & pre\_trigger\_time [ns]\\
            \hline 0x0302 & post\_trigger\_time [ns]\\
            \hline 0x0304 & nim\_triggers (14 bits, NIM0-NIM13)\\
            \hline 0x0308 & irq\_triggers (6 bits, IRQ1-IRQ6)\\
            \hline 0x030A & util\_triggers (16 bits, see \autoref{table:trigger_io_dso_util_triggers})\\
            \hline 0x0306 & capture\_enable\\
            \hline
        \end{tabular}
    \end{table}

    The DSO FIFO read address is at \texttt{0xFFFF0004}. Readout is terminated by BERR signal.
)";
    // clang-format on

    // bit values for the util_triggers register
    print_table_header(oss, "DSO util_triggers bitmask", "table:trigger_io_dso_util_triggers",
                       {"Bit", "Name"});

    TriggerIO ioCfg; // for lookup of unit names
    for (auto i = 0u; i < Level0::UtilityUnitCount; ++i)
    {
        UnitAddress unit = {0, i, 0};
        auto name = lookup_default_name(ioCfg, unit);
        fmt::println(oss, "        \\hline {} & {}\\\\", i, escape_underscores(name));
    }

    print_table_footer(oss);
}

void gen_unit_address_table(std::ostream &oss, const std::vector<RegisterWrite> &unitSelects,
                            std::vector<unsigned> targetLevels)
{
    auto headerFragment = fmt::format("{}", fmt::join(targetLevels, " \\& "));
    auto labelTail = fmt::format("{}", fmt::join(targetLevels, "_"));

    TriggerIO ioCfg; // for lookup of unit names and part generation

    print_table_header(oss, fmt::format("Level {} unit addresses (0x0200)", headerFragment),
                       fmt::format("table:trigger_io_units_level{}", labelTail), {"Value", "Name"});

    for (const auto &part: unitSelects)
    {
        assert(part.address == UnitSelectRegister);
        unsigned level = (part.value >> 8) & 0xFF;
        unsigned unit = part.value & 0xFF;

        if (std::find(targetLevels.begin(), targetLevels.end(), level) == targetLevels.end())
            continue;

        auto name = lookup_default_name(ioCfg, UnitAddress{level, unit});

        if (util::endswith(name, ".OUT0"))
        {
            name = name.substr(0, name.size() - 5);
        }

        fmt::println(oss, "        \\hline 0x{:03X} & {}\\\\", part.value,
                     escape_underscores(name));
    }

    // Special DSO handling as it's not part of the TriggerIO structure.
    if (std::find(targetLevels.begin(), targetLevels.end(), 0) != targetLevels.end())
    {
        fmt::println(oss, "        \\hline 0x030 & DSO\\\\");
    }

    print_table_footer(oss);
}

void gen_unit_address_tables(std::ostream &oss)
{
    auto allParts = generate_trigger_io_parts(TriggerIO{});

    // Collect all the unit select writes. They are the first RegisterWrite in
    // each UnitBlock.
    std::vector<RegisterWrite> unitSelectWrites;

    unitSelectWrites = std::accumulate(
        allParts.begin(), allParts.end(), unitSelectWrites,
        [](auto &acc, const ScriptPart &part)
        {
            if (auto ub = std::get_if<UnitBlock>(&part))
            {
                if (!ub->parts.empty() && std::holds_alternative<RegisterWrite>(ub->parts.front()))
                {
                    acc.push_back(std::get<RegisterWrite>(ub->parts.front()));
                }
            }
            return acc;
        });

    gen_unit_address_table(oss, unitSelectWrites, {0});
    gen_unit_address_table(oss, unitSelectWrites, {1, 2});
    gen_unit_address_table(oss, unitSelectWrites, {3});
}

void gen_connection_map_l1_lut2(std::ostream &oss)
{
    print_table_header(oss, "L1.LUT2 dynamic input choices",
                       "table::trigger_io_l1_lut2_connections",
                       {"Value", "Input0", "Input1", "Input2"});

    using Row = std::array<std::string, 4>;
    std::vector<Row> rows;

    for (size_t input = 0; input < trigger_io::Level1::LUT2DynamicInputChoices.size(); ++input)
    {
        const auto &choices = Level1::LUT2DynamicInputChoices[input];
        rows.resize(std::max(choices.size(), rows.size()));
        for (size_t choice = 0; choice < choices.size(); ++choice)
        {
            rows[choice][0] = std::to_string(choice);
            rows[choice][1 + input] =
                escape_underscores(lookup_default_name(TriggerIO{}, choices[choice]));
        }
    }

    for (const auto &row: rows)
    {
        fmt::println(oss, "        \\hline {} \\\\", fmt::join(row, " & "));
    }

    print_table_footer(oss);
}

void gen_connection_map_l2_lut(std::ostream &oss, size_t lutIndex)
{
    print_table_header(oss, fmt::format("L2.LUT{} dynamic input choices", lutIndex),
                       fmt::format("table::trigger_io_l2_lut{}_connections", lutIndex),
                       {"Value", "Input0", "Input1", "Input2", "Strobe"});

    auto allChoices = Level2::DynamicInputChoices[lutIndex];
    using Row = std::array<std::string, 5>;
    std::vector<Row> rows;

    for (size_t input = 0; input < 3; ++input)
    {
        const auto &choices = allChoices.lutChoices[input];
        size_t choiceCount = choices.size();
        if (rows.size() < choiceCount)
        {
            rows.resize(choiceCount);
        }
        for (size_t choice = 0; choice < choiceCount; ++choice)
        {
            rows[choice][0] = std::to_string(choice);
            rows[choice][1 + input] =
                escape_underscores(lookup_default_name(TriggerIO{}, choices[choice]));
        }
    }

    // strobe input
    {
        size_t choiceCount = allChoices.strobeChoices.size();
        if (rows.size() < choiceCount)
        {
            rows.resize(choiceCount);
        }

        for (size_t choice = 0; choice < choiceCount; ++choice)
        {
            rows[choice][0] = std::to_string(choice);
            rows[choice][4] = escape_underscores(
                lookup_default_name(TriggerIO{}, allChoices.strobeChoices[choice]));
        }
    }

    for (const auto &row: rows)
    {
        fmt::println(oss, "        \\hline {} \\\\", fmt::join(row, " & "));
    }

    print_table_footer(oss);
}

void gen_connection_maps_l2_luts(std::ostream &oss)
{
    for (size_t lutIndex = 0; lutIndex < trigger_io::Level2::LUTCount; ++lutIndex)
    {
        gen_connection_map_l2_lut(oss, lutIndex);
    }
}

void gen_connection_maps(std::ostream &oss)
{
    gen_connection_map_l1_lut2(oss);
    gen_connection_maps_l2_luts(oss);
}

static const std::string GeneralHelp = R"~(
usage: mvlc_trigger_io_tool gen_vmescript|gen_mvlcscript|gen_latex_tables [<outputfile>]

    gen_vmescript: Generate a mvme VMEScript for the trigger IO configuration.
    gen_mvlcscript: Generate a mesytec-mvlc MVLCScript for the trigger IO configuration.
    gen_latex_tables: Generate LaTeX tables for the trigger IO configuration.

    Generated scripts and tables contain the default values for the trigger IO configuration.
)~";

int cli_main(const std::string &cmd, std::ostream &out)
{
    if (cmd == "gen_vmescript")
    {
        out << trigger_io::generate_trigger_io_vmescript(TriggerIO{});
    }
    else if (cmd == "gen_mvlcscript")
    {
        out << trigger_io::generate_trigger_io_mvlcscript(TriggerIO{});
    }
    else if (cmd == "gen_latex_tables")
    {
        fmt::println(out,
                     "% Generated by the mvlc_trigger_io_tool from the mesytec-mvlc driver library "
                     "(version {}).",
                     library_version());

        gen_timer_table(out);
        gen_nim_io_table(out);
        gen_ecl_io_table(out);
        gen_irq_io_table(out);
        gen_trigger_resource_table(out);
        gen_stackbusy_table(out);
        gen_lut_table(out);
        gen_stackstart_table(out);
        gen_synctrigger_table(out);
        gen_counter_table(out);
        gen_dso_tables(out);

        gen_unit_address_tables(out);
        gen_connection_maps(out);
    }
    else
    {
        std::cerr << "Error: unknown command '" << cmd << "'\n";
        std::cerr << GeneralHelp;
        return 1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    argh::parser parser(argv);

    if (parser[1].empty())
    {
        std::cerr << GeneralHelp;
        return 1;
    }

    auto cmd = parser[1];
    auto outputFile = parser[2];

    if (outputFile.empty())
    {
        return cli_main(cmd, std::cout);
    }
    else
    {
        std::ofstream oss(outputFile, std::ios::out);
        if (!oss)
        {
            std::cerr << "Error: failed to open output file '" << outputFile << "'\n";
            return 1;
        }

        return cli_main(cmd, oss);
    }

    return 0;
}
