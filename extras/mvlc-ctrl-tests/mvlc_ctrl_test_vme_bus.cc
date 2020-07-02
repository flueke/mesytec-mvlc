#include <chrono>
#include <thread>
#include <iostream>
#include <vector>

#include <mesytec-mvlc/mvlc_error.h>
#include <mesytec-mvlc/mvlc_factory.h>
#include <mesytec-mvlc/vme_constants.h>
#include <fmt/format.h>

using namespace mesytec::mvlc;
using std::cout;
using std::endl;

// I think this test needs an MDPP on address 0 with the special test firmware
// active. Figure this out and document it, then reactivate the test in
// CMakeLists.txt.

int main(int argc, char *argv[])
{
    try
    {

        auto mvlc = make_mvlc_usb();
        mvlc.setDisableTriggersOnConnect(true);

        if (auto ec = mvlc.connect())
            throw ec;

#if 0
        const auto NibbleValues = { 0xAu, 0x5u };
        const unsigned NibbleMaxShift = 64-4;

        for (unsigned shift = 0; shift <= NibbleMaxShift; shift+=4)
        {
            for (const auto nibbleValue: NibbleValues)
            {
                u64 combined = static_cast<u64>(nibbleValue) << shift;

                u32 address = (combined >> 32u);
                u32 value   = (combined & 0xffffffffu);

                cout << fmt::format("write 0x{:008x} 0x{:008x} 0x{:016x} {}",
                                    address, value, combined, shift)
                    << endl;

                for (unsigned i=0; i<1000; i++)
                {

                    auto ec = mvlc.vmeWrite(address, value, vme_amods::A32, VMEDataWidth::D32);

                    if (ec && ec != ErrorType::VMEError)
                        throw ec;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
        }
#else
        for (int i = 0; i < 10; ++i)
        {
            const std::vector<std::pair<u32, u32>> Patterns = { { 0xAAAAAAAAu, 0xAAAAAAAAu }, { 0x55555555u, 0x55555555u } };

            for (const auto &av: Patterns)
            {
                auto tStart = std::chrono::steady_clock::now();

                auto address = av.first;
                auto value = av.second;
                size_t repetitions = 0u;

                cout << fmt::format("write 0x{:008x} 0x{:008x}", address, value) << " ...";

                while (true)
                {
                    auto ec = mvlc.vmeWrite(av.first, av.second, vme_amods::A32, VMEDataWidth::D32);

                    if (ec /* && ec != ErrorType::VMEError */)
                        throw ec;

                    ++repetitions;

                    auto elapsed = std::chrono::steady_clock::now() - tStart;

                    if (elapsed >= std::chrono::milliseconds(1000))
                        break;
                }

                cout << " repeated " << repetitions << " times." << endl;
            }
        }
#endif
    } catch (const std::error_code &ec)
    {
        cout << ec.message() << endl;
        throw;
    }

    return 0;
}
