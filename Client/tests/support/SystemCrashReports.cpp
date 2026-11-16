#include "support/SystemCrashReports.h"

#ifdef __linux__
#include <linux/prctl.h>
#include <sys/prctl.h>
#endif

namespace Client::Tests
{
bool SuppressSystemCrashReports()
{
#ifdef __linux__
    return prctl(PR_SET_DUMPABLE, 0) == 0 && prctl(PR_GET_DUMPABLE) == 0;
#else
    return true;
#endif
}
} // namespace Client::Tests
