#include "zip_archive_updater.hpp"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <sys/stat.h>

#include <lz4frame.h>
#include <mz.h>
#include <mz_os.h>
#include <mz_strm.h>
#include <mz_strm_buf.h>
#include <mz_strm_os.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>

#include "filesystem.h"
#include "fmt.h"
#include "logging.h"
#include "storage_sizes.h"
#include "string_view.hpp"

namespace mesytec::mvlc::util
{

ZipUpdateResult update_zip_archive(const ZipUpdateConfig &config)
{
    ZipUpdateResult result;

    return result;
}

}
