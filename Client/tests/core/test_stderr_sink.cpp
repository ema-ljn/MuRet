#include "Core/StderrSink.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "support/TemporaryFile.h"

#include <gtest/gtest.h>

// Client::Core::StderrSink, contra un archivo temporal en vez de stderr.
namespace
{
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Core::StderrSink;
using Client::Tests::OpenTemporaryFile;
using Client::Tests::ReadAll;

TEST(StderrSinkTest, WritesOneLinePerMessage)
{
    const auto output = OpenTemporaryFile();
    ASSERT_NE(output, nullptr);
    StderrSink sink(output.get());
    Log log(sink, LogLevel::Info);

    log.Info("uno");
    log.Error("dos {}", 2);

    EXPECT_EQ(ReadAll(output.get()), "[info] uno\n[error] dos 2\n");
}
} // namespace
