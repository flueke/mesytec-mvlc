#include "mesytec-mvlc/util/filesystem.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <mz.h>
#include <mz_os.h>

namespace mesytec
{
namespace mvlc
{
namespace util
{

std::string basename(const std::string &filepath)
{
    const char *filename = nullptr;

    if (mz_path_get_filename(filepath.c_str(), &filename) == MZ_OK)
        return std::string(filename);

    return filepath;
}

std::string dirname(const std::string &filepath)
{
    auto buffer = std::make_unique<char[]>(filepath.size() + 1u);
    std::copy(std::begin(filepath), std::end(filepath), buffer.get());
    buffer[filepath.size()] = '\0';

    if (mz_path_remove_filename(buffer.get()) == MZ_OK)
        return std::string(buffer.get());

    return {};
}

bool file_exists(const std::string &filepath)
{
    return mz_os_file_exists(filepath.c_str()) == MZ_OK;
}

bool delete_file(const std::string &filepath)
{
    return mz_os_unlink(filepath.c_str()) == MZ_OK;
}

}
}
}
