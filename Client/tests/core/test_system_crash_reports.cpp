#include "support/SystemCrashReports.h"

#include <gtest/gtest.h>

#ifdef __linux__
#include <cstdlib>
#include <linux/prctl.h>
#include <sys/prctl.h>

// The child can still end by a signal, but must not raise desktop notices for the user.
TEST(SystemCrashReportsDeathTest, SuppressesTheChildWithoutChangingTheParent)
{
    const int parentDumpable = prctl(PR_GET_DUMPABLE);
    ASSERT_GE(parentDumpable, 0);
    EXPECT_EXIT(
        {
            const bool suppressed = Client::Tests::SuppressSystemCrashReports();
            std::_Exit(suppressed && prctl(PR_GET_DUMPABLE) == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
        },
        testing::ExitedWithCode(EXIT_SUCCESS), "");
    EXPECT_EQ(prctl(PR_GET_DUMPABLE), parentDumpable);
}
#endif
