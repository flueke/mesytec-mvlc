#include <mesytec-mvlc/mvlc_trigger_io_serialize_internal.h>
#include <mesytec-mvlc/util/fmt.h>
#include <mesytec-mvlc/util/string_util.h>

#include <cassert>
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
    fmt::println(oss, "    \\caption{{{}}}", caption);
    if (!label.empty())
    {
        fmt::println(oss, "    \\label{{{}}}", label); // do _not_ escape underscores in labels :)
    }
    fmt::println(oss, "    \\begin{{tabular}}{{{}}}", make_coldef(columnTitles.size()));
    fmt::print(oss, "        \\hline");

    std::vector<std::string> formattedColumnTitles;

    for (const auto &title: columnTitles)
    {
        formattedColumnTitles.push_back(fmt::format("\\textbf{{{}}}", title));
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

void gen_unit_address_table(std::ostream &oss, const std::vector<RegisterWrite> &unitSelects,
    std::vector<unsigned> targetLevels)
{
    auto headerFragment = fmt::format("{}", fmt::join(targetLevels, " \\& "));
    auto labelTail = fmt::format("{}", fmt::join(targetLevels, "_"));

    TriggerIO ioCfg; // for lookup of unit names and part generation

    print_table_header(oss, fmt::format("Level {} unit addresses (0x0200)", headerFragment),
                       fmt::format("table:trigger_io_units_level{}", labelTail),
                       {"Value", "Name"});

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

    print_table_footer(oss);
}

void gen_unit_address_tables(std::ostream &oss)
{
    auto allParts = generate_trigger_io_parts(TriggerIO{});

    // The unit selection is always the first part of each UnitBlock. First step
    // is to collect all of these.
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

int main(int argc, char **argv)
{
    std::ofstream oss("/home/florian/src/mesytec-docs/nuclear_physics/mvlc/trigger_io_tables.tex");

    fmt::println(oss,
                 "% Generated by the mvlc_trigger_io_tool from the mesytec-mvlc driver library.");

    gen_timer_table(oss);
    gen_nim_io_table(oss);
    gen_ecl_io_table(oss);
    gen_irq_io_table(oss);
    gen_trigger_resource_table(oss);
    gen_stackbusy_table(oss);
    gen_lut_table(oss);
    gen_stackstart_table(oss);
    gen_synctrigger_table(oss);
    gen_counter_table(oss);

    gen_unit_address_tables(oss);

    return 0;
}
