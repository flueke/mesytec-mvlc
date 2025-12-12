#include <gtest/gtest.h>
#include "mvlc_trigger_io_serialize_vmescript.h"

static const char *ReferenceVMEScript =
#include "data/trigger_io_vme_script_reference_file.vmescript"
;

using namespace mesytec::mvlc;

TEST(TriggerIOSerialize, SerializeToVMEScript)
{
    auto generated = trigger_io::generate_trigger_io_vmescript(
        mesytec::mvlc::trigger_io::TriggerIO{});

        ASSERT_EQ(ReferenceVMEScript, generated);
}
