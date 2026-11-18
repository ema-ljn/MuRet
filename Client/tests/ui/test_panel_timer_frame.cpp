#include "UI/PanelTimerFrame.h"

#include <gtest/gtest.h>

namespace
{
TEST(PanelTimerFrameTest, HalfTheDurationDrawsHalfTheNative150PixelBar)
{
    const auto frame = Client::UI::PanelTimerFrameAt(3500, 1000, 6000);
    EXPECT_EQ(frame.elapsedSeconds, 2u);
    EXPECT_FLOAT_EQ(frame.width, 75.0f);
    EXPECT_FLOAT_EQ(frame.du, 0.29296875f);
}

TEST(PanelTimerFrameTest, DrawingPastTheDeadlineDoesNotClampTheWidth)
{
    const auto frame = Client::UI::PanelTimerFrameAt(8500, 1000, 6000);
    EXPECT_EQ(frame.elapsedSeconds, 7u);
    EXPECT_FLOAT_EQ(frame.width, 225.0f);
    EXPECT_FLOAT_EQ(frame.du, 0.87890625f);
}

TEST(PanelTimerFrameTest, ElapsedTickWrapsButTheDivisorIsASignedDword)
{
    const auto wrapped = Client::UI::PanelTimerFrameAt(984, 0xfffffff0u, 4984);
    EXPECT_EQ(wrapped.elapsedSeconds, 1u);
    EXPECT_FLOAT_EQ(wrapped.width, 30.0f);
    const auto negative = Client::UI::PanelTimerFrameAt(1, 0, 0xffffffffu);
    EXPECT_FLOAT_EQ(negative.width, -150.0f);
}
} // namespace
