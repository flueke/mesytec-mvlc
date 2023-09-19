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
#ifndef __MESYTEC_MVLC_MVLC_FACTORY_H__
#define __MESYTEC_MVLC_MVLC_FACTORY_H__

#include <argh.h>
#include <spdlog/spdlog.h>
#include "mesytec-mvlc/mesytec-mvlc_export.h"
#include "mvlc.h"
#include "mvlc_readout_config.h"

namespace mesytec::mvlc
{

struct MvlcUrl
{
    std::string rawUrl;     // unparsed url string
    std::string scheme;     // scheme, e.g. udp://, eth://, usb://
    std::string host;       // full host including the port if present
};

MvlcUrl MESYTEC_MVLC_EXPORT mvlc_parse_url(const char *url);
inline MvlcUrl mvlc_parse_url(const std::string &url) { return mvlc_parse_url(url.c_str()); }


// USB
MVLC MESYTEC_MVLC_EXPORT make_mvlc_usb();
MVLC MESYTEC_MVLC_EXPORT make_mvlc_usb(unsigned index);
MVLC MESYTEC_MVLC_EXPORT make_mvlc_usb(const std::string &serial);

// ETH
MVLC MESYTEC_MVLC_EXPORT make_mvlc_eth(const std::string &host);

// from crateconfig info
MVLC MESYTEC_MVLC_EXPORT make_mvlc(const CrateConfig &crateConfig);

// URL based factory for MVLC instances. Accepts the following URLs variants:
// usb://                   Use the first USB device
// usb://<serial-string>    USB device matching the given serial number
// usb://@<index>           USB device with the given logical FTDI driver index
// eth://<hostname/ip>      ETH/UDP with a hostname or an ip-address
// udp://<hostname/ip>      ETH/UDP with a hostname or an ip-address
// hostname                 No scheme part -> interpreted as a hostname for ETH/UDP
MVLC MESYTEC_MVLC_EXPORT make_mvlc(const char *url);
inline MVLC make_mvlc(const std::string &url) { return make_mvlc(url.c_str()); }

// Helpers for CLI programs. Uses the 'argh' parser library to parse the
// following arguments: "--mvlc", "--mvlc-usb", "--mvlc-usb-index",
// "--mvlc-usb-serial" and "--mvlc-eth".
// As a last resort the MVLC_ADDRESS env variable is examined and parsed as an
// MVLC URL.
MESYTEC_MVLC_EXPORT const std::vector<std::string> &get_mvlc_standard_params();
void MESYTEC_MVLC_EXPORT add_mvlc_standard_params(argh::parser &parser);

// The parser must have been setup with add_mvlc_standard_params() before
// calling the next function.
MVLC MESYTEC_MVLC_EXPORT make_mvlc_from_standard_params(const argh::parser &parser);

// Creates an internal parser, sets it up using 'add_mvlc_standard_params',
// parses the given command line and creates and MVLC from the result.
MVLC MESYTEC_MVLC_EXPORT make_mvlc_from_standard_params(const char **argv);

// Util to log argh parser info via spdlog
void MESYTEC_MVLC_EXPORT log_parser_info(
    const argh::parser &parser,
    const std::string &context, // context string added to log messages
    const std::shared_ptr<spdlog::logger> &logger,
    const spdlog::level::level_enum &level = spdlog::level::trace);

void MESYTEC_MVLC_EXPORT trace_log_parser_info(
    const argh::parser &parser,
    const std::string context);

}

#endif /* __MESYTEC_MVLC_MVLC_FACTORY_H__ */
