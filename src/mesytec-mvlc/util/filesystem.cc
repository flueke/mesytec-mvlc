#include "mesytec-mvlc/util/filesystem.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mz.h>
#include <mz_os.h>
#include <random>
#include <string>

#ifdef MESYTEC_MVLC_PLATFORM_WINDOWS
#include <windows.h>
#endif

#include "fmt.h"

namespace mesytec::mvlc::util
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

bool delete_file(const std::string &filepath) { return mz_os_unlink(filepath.c_str()) == MZ_OK; }

std::string make_tempfile_name(const std::string &prefix)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, 999999);

    std::string filename;
    do
    {
        filename = prefix + std::to_string(dist(gen));
    }
    while (std::filesystem::exists(filename));

    return filename;
}

void rename_file(const std::string &src_, const std::string &dst_)
{
    namespace fs = std::filesystem;

#ifdef MESYTEC_MVLC_PLATFORM_WINDOWS
    auto src = std::wstring(src_.begin(), src_.end());
    auto dst = std::wstring(dst_.begin(), dst_.end());

    // Try Windows atomic replace semantics first.
    if (fs::exists(dst_))
    {
        if (!::ReplaceFileW(dst.c_str(), src.c_str(), nullptr,
                            REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr))
        {
            auto err = GetLastError();
            throw std::runtime_error(fmt::format("ReplaceFileW failed: {}", err));
        }
        fs::remove(src);
        return;
    }

    // If destination does not exist yet, use move/rename.
    if (!::MoveFileExW(src.c_str(), dst.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        auto err = GetLastError();
        throw std::runtime_error(fmt::format("MoveFileExW failed: {}", err));
    }
#else
    std::error_code ec;
    fs::rename(tmp, dst, ec);
    if (ec) throw std::runtime_error("fs::rename failed: " + ec.message());
#endif
}

} // namespace mesytec::mvlc::util
