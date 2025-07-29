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
#include "mvlc_factory.h"
#include "mvlc_impl_eth.h"
#include "mvlc_impl_usb.h"

namespace mesytec::mvlc
{

MVLC make_mvlc_usb()
{
    return MVLC(std::make_unique<usb::Impl>());
}

MVLC make_mvlc_usb(unsigned index)
{
    return MVLC(std::make_unique<usb::Impl>(index));
}

MVLC make_mvlc_usb(const std::string &serial)
{
    return MVLC(std::make_unique<usb::Impl>(serial));
}

MVLC make_mvlc_eth(const std::string &host)
{
    return MVLC(std::make_unique<eth::Impl>(host));
}

MVLC make_mvlc(const CrateConfig &crateConfig)
{
    switch (crateConfig.connectionType)
    {
        case ConnectionType::USB:
            if (crateConfig.usbIndex >= 0)
                return make_mvlc_usb(crateConfig.usbIndex);

            if (!crateConfig.usbSerial.empty())
                return make_mvlc_usb(crateConfig.usbSerial);

            return make_mvlc_usb();

        case ConnectionType::ETH:
            return make_mvlc_eth(crateConfig.ethHost);
    }

    throw std::runtime_error("unknown CrateConfig::connectionType");
}

MvlcUrl mvlc_parse_url(const char *url)
{
    MvlcUrl result;
    result.rawUrl = url;

    if (auto schemeStartPos = result.rawUrl.find("://");
        schemeStartPos != std::string::npos)
    {
        result.scheme = result.rawUrl.substr(0, schemeStartPos);
        result.host = result.rawUrl.substr(schemeStartPos + 3);
    }
    else
    {
        result.host = result.rawUrl;
    }

    return result;
}

MVLC make_mvlc(const char *urlStr)
{
    auto url = mvlc_parse_url(urlStr);

    if ((url.scheme.empty() || url.scheme == "eth" || url.scheme == "udp") && !url.host.empty())
        return make_mvlc_eth(url.host);

    if (url.scheme == "usb")
    {
        if (url.host.empty())
            return make_mvlc_usb();

        if (url.host.at(0) == '@')
        {
            unsigned index = std::strtoul(url.host.c_str() + 1, nullptr, 0);
            return make_mvlc_usb(index);
        }

        return make_mvlc_usb(url.host); // interpret host part as a serial string
    }

    return MVLC{};
}

const std::vector<std::string> &get_mvlc_standard_params()
{
    // Note: --mvlc-usb and others are not present here as they do not take an
    // argument.
    static const std::vector<std::string> MvlcStandardParams =
        {"--mvlc", "--mvlc-usb-index", "--mvlc-usb-serial", "--mvlc-eth"};

    return MvlcStandardParams;
}

void add_mvlc_standard_params(argh::parser &parser)
{
    for (const auto &p: get_mvlc_standard_params())
        parser.add_param(p);
}

MVLC make_mvlc_from_standard_params(const argh::parser &parser)
{
    std::string arg;

    if (parser("--mvlc") >> arg)
        return make_mvlc(arg); // mvlc URI

    if (parser["--mvlc-usb"])
        return make_mvlc_usb();

    unsigned usbIndex = 0;
    if (parser("--mvlc-usb-index") >> usbIndex)
        return make_mvlc_usb(usbIndex);

    if (parser("--mvlc-usb-serial") >> arg)
        return make_mvlc_usb(arg);

    if (parser("--mvlc-eth") >> arg)
        return make_mvlc_eth(arg);

    if (char *envAddr = std::getenv("MVLC_ADDRESS"))
        return make_mvlc(envAddr);

    return MVLC{};
}

MVLC make_mvlc_from_standard_params(const char **argv)
{
    argh::parser parser;
    add_mvlc_standard_params(parser);
    parser.parse(argv);
    return make_mvlc_from_standard_params(parser);
}

void log_parser_info(
    const argh::parser &parser,
    const std::string &context,
    const std::shared_ptr<spdlog::logger> &logger,
    const spdlog::level::level_enum &level)
{
    if (auto params = parser.params(); !params.empty())
    {
        for (const auto &param: params)
            logger->log(level, "argh-parse {} parameter: {}={}", context, param.first, param.second);
    }

    if (auto flags = parser.flags(); !flags.empty())
        logger->log(level, "argh-parse {} flags: {}", context, fmt::join(flags, ", "));

    if (auto pos_args = parser.pos_args(); !pos_args.empty())
        logger->log(level, "argh-parse {} pos args: {}", context, fmt::join(pos_args, ", "));
}

void trace_log_parser_info(
    const argh::parser &parser,
    const std::string context)
{
    log_parser_info(parser, context, spdlog::default_logger(), spdlog::level::trace);
}

}
