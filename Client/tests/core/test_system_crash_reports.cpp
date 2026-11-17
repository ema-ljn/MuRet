#include "support/SystemCrashReports.h"

#include <gtest/gtest.h>

#ifdef __linux__
#include <cstdlib>
#include <linux/prctl.h>
#include <sys/prctl.h>

// El hijo sigue pudiendo terminar por una señal, pero no debe generar avisos del escritorio del usuario.
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
