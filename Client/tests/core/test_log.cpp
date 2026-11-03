#include "Core/Log.h"

#include "Core/ILogSink.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// Client::Core::Log: el prefijo de cada nivel, el mínimo, Write y los formatos rotos.
namespace
{
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Tests::RecordingSink;

TEST(LogTest, WritesTheLevelAndTheMessage)
{
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);

    log.Info("hola {}", 1);

    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_EQ(sink.lines[0], "[info] hola 1");
    EXPECT_EQ(sink.levels[0], LogLevel::Info);
}

TEST(LogTest, BelowTheMinimumIsNotWritten)
{
    RecordingSink sink;
    Log log(sink, LogLevel::Warning);

    log.Debug("no");
    log.Info("tampoco");
    log.Warning("sí");
    log.Error("también");

    EXPECT_EQ(sink.lines, (std::vector<std::string>{"[warning] sí", "[error] también"}));
}

TEST(LogTest, EachLevelHasItsPrefix)
{
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);

    log.Debug("d");
    log.Info("i");
    log.Warning("w");
    log.Error("e");

    EXPECT_EQ(sink.lines, (std::vector<std::string>{"[debug] d", "[info] i", "[warning] w", "[error] e"}));
}

TEST(LogTest, WriteKeepsTheBraces)
{
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);

    log.Write(LogLevel::Info, "{} {:x} }{");

    EXPECT_EQ(sink.lines, (std::vector<std::string>{"[info] {} {:x} }{"}));
}

TEST(LogTest, WriteBelowTheMinimumIsNotWritten)
{
    RecordingSink sink;
    Log log(sink, LogLevel::Warning);

    log.Write(LogLevel::Info, "no");

    EXPECT_TRUE(sink.lines.empty());
}

TEST(LogTest, ABadFormatIsLoggedAsAnError)
{
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);

    // {:c} pasa al compilar, pero 300000 no es una letra: std::vformat tira std::format_error.
    EXPECT_NO_THROW(log.Info("{:c}", 300000));

    EXPECT_EQ(sink.lines, (std::vector<std::string>{"[error] formato de log inválido: {:c}"}));
}

TEST(LogTest, BelowTheMinimumIsNotFormatted)
{
    RecordingSink sink;
    Log quiet(sink, LogLevel::Info);

    EXPECT_NO_THROW(quiet.Debug("{:c}", 300000));

    // Si se formateara antes de mirar el mínimo, el error de formato dejaría una línea.
    EXPECT_TRUE(sink.lines.empty());
}
} // namespace
