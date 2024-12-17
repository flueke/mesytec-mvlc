
** Incomplete, old and untested **

Using the mesytec-mvlc library {#usage-guide}
=============================================

Usage with cmake
----------------

CMakeLists.txt for locating and linking against the library.

    cmake_minimum_required(VERSION 3.12)
    project(mesytec-mvlc-cmake-example)

    find_package(mesytec-mvlc REQUIRED)

    add_executable(my_tool my_tool.cc)
    target_link_libraries(my_tool PRIVATE mesytec-mvlc::mesytec-mvlc)

If the library was installed in a non-system location set the environment
variable `CMAKE_PREFIX_PATH` to point to the root of the installation prefix.

A small cmake example project can be found in
`share/mesytec-mvlc/cmake-example` under the installation directory.

MVLC objects and direct VME access
----------------------------------

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.cc}
    #include <mesytec-mvlc/mesytec-mvlc.h>

    using namespace mesytec::mvlc;

    int main()
    {
        // These factory functions return MVLC objects by value. These are thread-safe,
        // copyable wrappers over the underlying USB and ETH implementations.
        auto mvlc = make_mvlc_usb();                    // Use the first MVLC USB
        auto mvlc = make_mvlc_eth("mvlc-0007");         // ETH using a hostname
        auto mvlc = make_mvlc_eth("192.168.178.188");   // ETH using an IP address

        // Error reporting is done via std::error_code return values.
        if (auto ec = mvlc.connect())
        {
            cout << "Could not connect: " << ec.message() << endl;
            return 1;
        }

        // Reading from a VME address
        uint32_t vmeBase = 0x01000000u;
        uint16_t regAddr = 0x6008u;
        uint16_t readValue = 0u;
        auto ec = mvlc.vmeRead(vmeBase + regAddr, readValue, vme_amods::A32, VMEDataWidth::D16);

        if (ec) { cout << "VME read error: " << ec.message() << endl; return 1; }

        cout << "readValue=" << readValue << endl;

        // Writing to the same address (0x6008 is "module reset" for mesytec-module)
        mvlc.vmeWrite(vmeBase + regAddr, 1, vme_amods::A32, VMEDataWidth::D16);
    }
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

See the mesytec::mvlc::MVLC page for the full interface.

Building a CrateConfig
----------------------

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~{.cc}
CrateConfig cfg = {};
cfg.connectionType = ConnectionType::ETH;
cfg.ethHost = "mvlc-0007";

cfg.stacks = { readoutStack };
cfg.triggers = { trigger_value(TriggerType::IRQNoIACK, 1); };

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
