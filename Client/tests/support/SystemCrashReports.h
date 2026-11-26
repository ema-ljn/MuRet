#pragma once

// Only in a death test's child: Linux must not send its deliberate crash to systemd-coredump/DrKonqi.
// MuRet's own handler still writes its dump. It does not change the desktop's settings.
namespace Client::Tests
{
[[nodiscard]] bool SuppressSystemCrashReports();
} // namespace Client::Tests
