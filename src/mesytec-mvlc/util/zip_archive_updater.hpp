#ifndef A5CC9288_FD38_45AE_9520_5981110C73DE
#define A5CC9288_FD38_45AE_9520_5981110C73DE

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <variant>
#include <system_error>

namespace mesytec::mvlc::util
{

// A small utility for safely updating files in ZIP archives. Main use case is
// replacing mvme .analysis files in existing listmode ZIPs.

struct ZipOperationBase
{
    // Filename inside the ZIP archive.
    std::string filename;
    // Optional human-readable description for logging/UI later:
    std::optional<std::string> description;
};

struct AddOperation: public ZipOperationBase { std::vector<uint8_t> contents; };
struct UpdateOperation: public ZipOperationBase { std::vector<uint8_t> contents; };
struct DeleteOperation: public ZipOperationBase {};
using ZipOperation = std::variant<AddOperation, UpdateOperation, DeleteOperation>;

struct ProgressEvent
{
    std::string step; // e.g. "copying_listmode", "adding_analysis", "replacing_file"
    double progress;  // 0.0–1.0 for the current step
    std::optional<uint64_t> bytes_copied;
    std::optional<uint64_t> bytes_total;
};

using ProgressCallback = std::function<void(const ProgressEvent &)>;
using CancelCallback = std::function<bool()>; // returns true if user requested cancel

struct ZipUpdateConfig
{
    std::string input_zip_path;             // e.g. "/data/run123/listmode.zip"
    std::vector<ZipOperation> ops;          // 1–N operations on the same ZIP
    bool validate_output = true;            // whether to test the new ZIP before replacing

    // Want a bit more free space than the size of the input archive as we copy
    // the data instead of modifying the input archive directly.
    double min_available_space_ratio = 1.2;

    // Optional callbacks for progress reporting and cancellation. If
    // is_cancelled is not set cancellation is not possible.
    ProgressCallback on_progress;
    CancelCallback is_cancelled;
};

struct ZipUpdateResult
{
    std::error_code ec;
    std::string error_detail; // empty if !ec, otherwise animal-readable context
    std::vector<std::string> ops_descriptions; // what was done, for logging

    bool did_succeed() const { return !ec; }
    bool was_cancelled() const { return ec == std::errc::operation_canceled; }
};

ZipUpdateResult update_zip_archive(const ZipUpdateConfig &config);

} // namespace mesytec::mvlc::util

#endif /* A5CC9288_FD38_45AE_9520_5981110C73DE */
