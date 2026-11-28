#include "Session/ServerAddress.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

using namespace std::string_view_literals;

// Client::Session::ParsePort and ParseHost: what the config and the command line validate (CL3.4).
namespace
{
using Client::Session::MaxHostBytes;
using Client::Session::ParseHost;
using Client::Session::ParsePort;

std::string PortError(std::string_view text)
{
    const auto port = ParsePort(text);
    EXPECT_FALSE(port.HasValue()) << '"' << text << '"';
    return port.HasValue() ? std::string() : port.Error();
}

std::string HostError(std::string_view text)
{
    const auto host = ParseHost(text);
    EXPECT_FALSE(host.HasValue()) << '"' << text << '"';
    return host.HasValue() ? std::string() : host.Error();
}

TEST(ServerAddressTest, AcceptsAPortFrom1To65535)
{
    EXPECT_EQ(ParsePort("1").Value(), 1);
    EXPECT_EQ(ParsePort("44405").Value(), 44405);
    EXPECT_EQ(ParsePort("65535").Value(), 65535);
}

TEST(ServerAddressTest, RejectsAPortOutOfRange)
{
    for (const std::string_view text : {"0", "65536", "99999999999999999999"})
    {
        EXPECT_EQ(PortError(text), "the port must be from 1 to 65535");
    }
}

// from_chars stops at the first letter ("80x" gives 80) and does not accept "+" or spaces: for the message,
// everything that is not digits says the same.
TEST(ServerAddressTest, RejectsAPortThatIsNotDigits)
{
    for (const std::string_view text : {"", "12a", "80x", "-1", "+80", " 80", "0x50"})
    {
        EXPECT_EQ(PortError(text), "the port must be a number");
    }
}

// Q-C37: the original copied the host without checking its length.
TEST(ServerAddressTest, AcceptsAHostOf1To255Bytes)
{
    EXPECT_EQ(ParseHost("a").Value(), "a");
    EXPECT_EQ(ParseHost("mu.example.org").Value(), "mu.example.org");
    EXPECT_EQ(ParseHost(std::string(MaxHostBytes, 'a')).Value().size(), 255u);
}

TEST(ServerAddressTest, RejectsAHostOutOfRange)
{
    EXPECT_EQ(HostError(""), "the host must be 1 to 255 bytes");
    EXPECT_EQ(HostError(std::string(MaxHostBytes + 1, 'a')), "the host must be 1 to 255 bytes");
}

TEST(ServerAddressTest, RejectsAHostThatIsNotVisibleAscii)
{
    EXPECT_EQ(HostError("10.0.0.1 x"), "the host cannot have spaces");
    EXPECT_EQ(HostError("a\tb"), "the host cannot have spaces");
    EXPECT_EQ(HostError("10.0.0.1\0evil.com"sv), "the host cannot have control characters");
    EXPECT_EQ(HostError("a\x7F"), "the host cannot have control characters");
    // In UTF-8 (the config) or in the ANSI page (Windows' argv), the same bytes are not the same letter: the host is
    // ASCII in both.
    EXPECT_EQ(HostError("m\xC3\xBA.org"), "the host must be ASCII (a name with accents goes in punycode)");
    EXPECT_EQ(HostError("m\xFA.org"), "the host must be ASCII (a name with accents goes in punycode)");
}
} // namespace
