#include "Core/TeeSink.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// Client::Core::TeeSink: la misma línea a las dos salidas.
namespace
{
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Core::TeeSink;
using Client::Tests::RecordingSink;

TEST(TeeSinkTest, WritesBoth)
{
    RecordingSink first;
    RecordingSink second;
    TeeSink tee(first, second);
    Log log(tee, LogLevel::Debug);

    log.Error("se cortó la conexión ({})", 10054);

    EXPECT_EQ(first.lines, (std::vector<std::string>{"[error] se cortó la conexión (10054)"}));
    EXPECT_EQ(second.lines, first.lines);
    EXPECT_EQ(first.levels, (std::vector<LogLevel>{LogLevel::Error}));
    EXPECT_EQ(second.levels, first.levels);
}
} // namespace
