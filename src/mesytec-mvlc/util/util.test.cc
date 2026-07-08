#include "gtest/gtest.h"

#include "env.hpp"
#include "logging.h"

using namespace mesytec::mvlc;

TEST(util, GetEnv)
{
    auto envPath = util::get_env_variable("PATH");
    ASSERT_TRUE(envPath.has_value());

    auto envFoo = util::get_env_variable("FROBLWOBLFOOBARBLOB");
    ASSERT_FALSE(envFoo.has_value());
}
