#include "Scenes/WorldState.h"

#include "Core/Expected.h"
#include "Core/IMillisecondClock32.h"
#include "Scenes/WorldRuntime.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <optional>
#include <system_error>

namespace
{
class TickSource final : public Client::Core::IMillisecondClock32
{
public:
    Client::Core::Expected<std::uint32_t, std::error_code> Sample() override
    {
        ++calls;
        if (fail)
            return Client::Core::Unexpected(std::make_error_code(std::errc::io_error));
        return sample;
    }
    std::uint32_t sample = 0x80000001u;
    int calls = 0;
    bool fail = false;
};
// Detecta reloj relativo/signed32, muestras extra, origen restado y compensación artificial de wrap.
TEST(LeafTimeBindingTest, PublishesOneUnsignedRawSampleAndAllowsRepeatsAndWrap)
{
    Client::Scenes::WorldState world;
    TickSource ticks;
    EXPECT_FALSE(world.Runtime().worldTime);
    ASSERT_TRUE(world.SampleWorldTime(ticks).HasValue());
    ASSERT_TRUE(world.Runtime().worldTime);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(*world.Runtime().worldTime), 0x4f000000u);
    EXPECT_EQ(ticks.calls, 1);
    ASSERT_TRUE(world.SampleWorldTime(ticks).HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(*world.Runtime().worldTime), 0x4f000000u);
    ticks.sample = 0xffffffffu;
    ASSERT_TRUE(world.SampleWorldTime(ticks).HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(*world.Runtime().worldTime), 0x4f800000u);
    ticks.sample = 0;
    ASSERT_TRUE(world.SampleWorldTime(ticks).HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(*world.Runtime().worldTime), 0u);
    EXPECT_EQ(ticks.calls, 4);
}
TEST(LeafTimeBindingTest, SamplingFailurePreservesThePublishedTimeAndPropagatesTheExactError)
{
    Client::Scenes::WorldState world;
    TickSource ticks;
    ticks.fail = true;
    auto result = world.SampleWorldTime(ticks);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::make_error_code(std::errc::io_error));
    EXPECT_FALSE(world.Runtime().worldTime);
    world.Runtime().worldTime = 80;
    result = world.SampleWorldTime(ticks);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::make_error_code(std::errc::io_error));
    EXPECT_EQ(world.Runtime().worldTime, std::optional<float>(80));
    EXPECT_EQ(ticks.calls, 2);
}
} // namespace
