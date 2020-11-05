#include <iostream>
#include <mesytec-mvlc/mesytec-mvlc.h>

using namespace mesytec::mvlc;

int main(int argc, char *argv[])
{
    std::cout << "Using mesytec-mvlc library version " << library_version() << std::endl;

    auto mvlc = make_mvlc_usb();

    if (auto ec = mvlc.connect())
    {
        std::cout << "Could not connect to mvlc: " << ec.message() << std::endl;
        return 1;
    }

    std::cout << "Connected to MVLC, " << mvlc.connectionInfo() << std::endl;

    return 0;
}
