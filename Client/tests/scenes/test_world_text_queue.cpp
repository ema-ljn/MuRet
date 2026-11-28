#include "Kernel/WorldTextQueue.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace
{
using Client::Kernel::QueueWorldText;
using Client::Kernel::WorldTextQueue;
using Client::Kernel::WorldTextSize;

// 0x4381f0 copies the 0x10 dwords whatever the text's length: MuRet fills the rest with 0.
TEST(WorldTextQueueTest, AShortTextIsCopiedWithZerosAfterIt)
{
    WorldTextQueue queue;

    QueueWorldText(queue, "Gate", glm::vec3(1.0f, 2.0f, 3.0f));

    ASSERT_EQ(queue.size(), 1U);
    EXPECT_EQ(std::string_view(queue.front().text.data(), 4), "Gate");
    EXPECT_TRUE(std::all_of(queue.front().text.begin() + 4, queue.front().text.end(), [](char c) { return c == 0; }));
    EXPECT_EQ(queue.front().position, glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST(WorldTextQueueTest, ALongTextKeepsItsFirstSixtyFourBytes)
{
    WorldTextQueue queue;
    const std::string longText(WorldTextSize + 8, 'a');

    QueueWorldText(queue, longText, glm::vec3(0.0f));

    ASSERT_EQ(queue.size(), 1U);
    EXPECT_EQ(std::string(queue.front().text.data(), queue.front().text.size()), std::string(WorldTextSize, 'a'));
}

TEST(WorldTextQueueTest, ANewTextGoesToTheEndOfTheQueue)
{
    WorldTextQueue queue;

    QueueWorldText(queue, "first", glm::vec3(0.0f));
    QueueWorldText(queue, "second", glm::vec3(0.0f));

    ASSERT_EQ(queue.size(), 2U);
    EXPECT_EQ(std::string_view(queue.back().text.data()), "second");
}
} // namespace
