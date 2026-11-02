// Client::Tests::ParseWireCapture, sobre un log sintético de pocas líneas con la forma del
// muproxy.log del laboratorio.

#include "support/WireCapture.h"

#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>

namespace
{

using Client::Tests::ParseWireCapture;
using Client::Tests::SameBytes;
using Client::Tests::WireCaptureProblem;
using Client::Tests::WireDirection;

// Dos conexiones al 44405 y una al 55901, intercaladas, con la marca del proxy al final de una
// línea y un fin de línea de Windows.
constexpr std::string_view SyntheticLog = "12:00:00 44405 ACC len=3 414243\n"
                                          "12:00:00 44405 C->S len=4 c1042000\n"
                                          "12:00:01 55901 ACC len=3 444546\n"
                                          "12:00:01 44405 S->C len=3 C10300\r\n"
                                          "12:00:02 55901 C->S len=1 c1 03f4\n"
                                          "\n"
                                          "12:00:03 44405 ACC len=3 474849\n"
                                          "12:00:03 44405 C->S len=1 c1 PLAIN='t'\n";

TEST(WireCaptureTest, ReadsConnectionsInTheOrderOfTheirAcc)
{
    const auto parsed = ParseWireCapture(SyntheticLog);

    ASSERT_TRUE(parsed.HasValue());
    const auto& connections = parsed.Value();
    ASSERT_EQ(connections.size(), 3u);
    EXPECT_EQ(connections[0].port, 44405);
    EXPECT_EQ(connections[1].port, 55901);
    EXPECT_EQ(connections[2].port, 44405);
}

TEST(WireCaptureTest, KeepsEachSegmentWithItsDirection)
{
    const auto parsed = ParseWireCapture(SyntheticLog);
    ASSERT_TRUE(parsed.HasValue());
    const auto& first = parsed.Value()[0];

    ASSERT_EQ(first.segments.size(), 2u);
    EXPECT_EQ(first.segments[0].direction, WireDirection::ClientToServer);
    EXPECT_EQ(first.segments[1].direction, WireDirection::ServerToClient);

    const auto toServer = first.Reads(WireDirection::ClientToServer);
    ASSERT_EQ(toServer.size(), 1u);
    constexpr std::array<std::byte, 4> Sent = {std::byte{0xC1}, std::byte{0x04}, std::byte{0x20}, std::byte{0x00}};
    EXPECT_TRUE(SameBytes(toServer[0], Sent));

    const auto toClient = first.Reads(WireDirection::ServerToClient);
    ASSERT_EQ(toClient.size(), 1u);
    constexpr std::array<std::byte, 3> Received = {std::byte{0xC1}, std::byte{0x03}, std::byte{0x00}};
    EXPECT_TRUE(SameBytes(toClient[0], Received));
}

// Lo que sigue a los bytes (la marca del proxy, o una segunda tira de hex) no es del segmento.
TEST(WireCaptureTest, IgnoresWhatFollowsTheBytes)
{
    const auto parsed = ParseWireCapture(SyntheticLog);
    ASSERT_TRUE(parsed.HasValue());
    constexpr std::array<std::byte, 1> OnlyTheType = {std::byte{0xC1}};

    ASSERT_EQ(parsed.Value()[1].segments.size(), 1u);
    EXPECT_TRUE(SameBytes(parsed.Value()[1].segments[0].bytes, OnlyTheType));
    ASSERT_EQ(parsed.Value()[2].segments.size(), 1u);
    EXPECT_TRUE(SameBytes(parsed.Value()[2].segments[0].bytes, OnlyTheType));
}

void ExpectProblem(std::string_view log, std::size_t line, WireCaptureProblem problem)
{
    const auto parsed = ParseWireCapture(log);
    ASSERT_FALSE(parsed.HasValue()) << log;
    EXPECT_EQ(parsed.Error().line, line) << log;
    EXPECT_EQ(parsed.Error().problem, problem) << log;
}

TEST(WireCaptureTest, SaysWhichLineIsWrong)
{
    ExpectProblem("12:00:00 44405 C->S len=1 c1\n", 1, WireCaptureProblem::NoConnection);
    ExpectProblem("12:00:00 44405 ACC len=0\n12:00:00 44405 C->S len=2 c1\n", 2, WireCaptureProblem::LengthMismatch);
    ExpectProblem("12:00:00 44405 ACC len=0\n12:00:00 44405 C->S len=1 zz\n", 2, WireCaptureProblem::BadHex);
    ExpectProblem("12:00:00 44405 ACC len=0\n12:00:00 44405 C->S len=1 c\n", 2, WireCaptureProblem::BadHex);
    ExpectProblem("12:00:00 44405 ACC len=0\n12:00:00 44405 X->Y len=1 c1\n", 2, WireCaptureProblem::MalformedLine);
    ExpectProblem("12:00:00 44405 ACC len=0\n12:00:00 44405 C->S size=1 c1\n", 2, WireCaptureProblem::MalformedLine);
    ExpectProblem("12:00:00 port ACC len=0\n", 1, WireCaptureProblem::MalformedLine);
    ExpectProblem("12:00:00 44405 ACC\n", 1, WireCaptureProblem::MalformedLine);
}

TEST(WireCaptureTest, AnEmptyLogHasNoConnections)
{
    const auto parsed = ParseWireCapture("");

    ASSERT_TRUE(parsed.HasValue());
    EXPECT_TRUE(parsed.Value().empty());
}

} // namespace
