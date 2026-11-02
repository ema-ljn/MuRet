#include "App/WindowState.h"

#include <gtest/gtest.h>

// Client::App::Apply: qué cambia cada evento de la ventana (la tabla del paso 1 de la CL3.1).
namespace
{
using Client::App::Apply;
using Client::App::PixelSize;
using Client::App::WindowEvent;
using Client::App::WindowState;

TEST(WindowStateTest, CloseRequestedStopsRunning)
{
    const WindowState state = Apply(WindowState{}, WindowEvent::CloseRequested);

    EXPECT_FALSE(state.running);
    EXPECT_FALSE(state.minimized);
    EXPECT_TRUE(state.focused);
}

TEST(WindowStateTest, MinimizedAndRestored)
{
    const WindowState minimized = Apply(WindowState{}, WindowEvent::Minimized);
    const WindowState restored = Apply(minimized, WindowEvent::Restored);
    const WindowState restoredTwice = Apply(restored, WindowEvent::Restored);

    EXPECT_TRUE(minimized.minimized);
    EXPECT_TRUE(minimized.running);
    EXPECT_FALSE(restored.minimized);
    // Un Restored sin Minimized antes no cambia nada.
    EXPECT_FALSE(restoredTwice.minimized);
    EXPECT_TRUE(restoredTwice.running);
}

TEST(WindowStateTest, OccludedIsLikeMinimized)
{
    const WindowState occluded = Apply(WindowState{}, WindowEvent::Occluded);
    const WindowState exposed = Apply(occluded, WindowEvent::Exposed);

    EXPECT_TRUE(occluded.minimized);
    EXPECT_TRUE(occluded.running);
    EXPECT_FALSE(exposed.minimized);
}

TEST(WindowStateTest, FocusLostKeepsRunning)
{
    const WindowState lost = Apply(WindowState{}, WindowEvent::FocusLost);
    const WindowState gained = Apply(lost, WindowEvent::FocusGained);

    EXPECT_FALSE(lost.focused);
    EXPECT_TRUE(lost.running);
    EXPECT_FALSE(lost.minimized);
    EXPECT_TRUE(gained.focused);
}

TEST(WindowStateTest, PixelSizeChangedUpdatesTheSize)
{
    const WindowState state = Apply(WindowState{}, WindowEvent::PixelSizeChanged, PixelSize{1280, 960});

    EXPECT_EQ(state.pixelSize.width, 1280);
    EXPECT_EQ(state.pixelSize.height, 960);
    EXPECT_TRUE(state.running);
}

TEST(WindowStateTest, AZeroPixelSizeIsIgnored)
{
    const WindowState sized = Apply(WindowState{}, WindowEvent::PixelSizeChanged, PixelSize{640, 480});
    const WindowState state = Apply(sized, WindowEvent::PixelSizeChanged, PixelSize{0, 0});

    EXPECT_EQ(state.pixelSize.width, 640);
    EXPECT_EQ(state.pixelSize.height, 480);
}

TEST(WindowStateTest, APixelSizeWithOneZeroSideIsIgnored)
{
    const WindowState sized = Apply(WindowState{}, WindowEvent::PixelSizeChanged, PixelSize{640, 480});
    const WindowState noHeight = Apply(sized, WindowEvent::PixelSizeChanged, PixelSize{640, 0});
    const WindowState noWidth = Apply(sized, WindowEvent::PixelSizeChanged, PixelSize{0, 480});

    EXPECT_EQ(noHeight.pixelSize.width, 640);
    EXPECT_EQ(noHeight.pixelSize.height, 480);
    EXPECT_EQ(noWidth.pixelSize.width, 640);
    EXPECT_EQ(noWidth.pixelSize.height, 480);
}

TEST(WindowStateTest, ANegativePixelSizeIsIgnored)
{
    const WindowState sized = Apply(WindowState{}, WindowEvent::PixelSizeChanged, PixelSize{640, 480});
    const WindowState negativeWidth = Apply(sized, WindowEvent::PixelSizeChanged, PixelSize{-1, 480});
    const WindowState negativeHeight = Apply(sized, WindowEvent::PixelSizeChanged, PixelSize{640, -1});

    EXPECT_EQ(negativeWidth.pixelSize.width, 640);
    EXPECT_EQ(negativeHeight.pixelSize.height, 480);
}
} // namespace
