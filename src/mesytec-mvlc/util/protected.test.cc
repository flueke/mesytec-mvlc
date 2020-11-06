#include "gtest/gtest.h"
#include <chrono>
#include <future>
#include "util/protected.h"

using namespace mesytec::mvlc;

struct Object
{
    int value = 0u;
};

TEST(util_protected, ProtectedWaitableNotify)
{
    // unlimited wait, immedate async modification of the object
    {
        WaitableProtected<Object> wo({});

        auto f = std::async(
            std::launch::async,
            [&wo] ()
            {
                wo.access()->value = 42;
            });

        auto oa = wo.wait([] (const Object &o) { return o.value != 0u; });

        ASSERT_EQ(oa->value, 42u);
    }

    {
        WaitableProtected<Object> wo({});

        // limited wait, delayed modification of the object
        auto f = std::async(
            std::launch::async,
            [&wo] ()
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                wo.access()->value = 42;
            });

        {
            auto oa = wo.wait_for(
                std::chrono::milliseconds(100),
                [] (const Object &o) { return o.value != 0u; });

            ASSERT_EQ(oa->value, 0u);
        }

        // unlimited wait
        {
            auto oa = wo.wait([] (const Object &o) { return o.value != 0u; });

            ASSERT_EQ(oa->value, 42u);
        }
    }

}
