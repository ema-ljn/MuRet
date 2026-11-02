#include "Platform/WindowEvents.h"

#include <SDL3/SDL_events.h>

#include <gtest/gtest.h>

#include <optional>

// Client::Platform::WindowEventFor: la tabla de los SDL_EVENT_* a los WindowEvent, con eventos
// armados a mano (sin SDL_Init).
namespace
{
using Client::App::WindowEvent;
using Client::Platform::WindowEventFor;

std::optional<WindowEvent> EventFor(SDL_EventType type)
{
    // Con {} la unión queda en cero entera.
    SDL_Event event{};
    event.type = type;
    return WindowEventFor(event);
}

TEST(WindowEventsTest, TranslatesEachWindowEvent)
{
    EXPECT_EQ(EventFor(SDL_EVENT_QUIT), WindowEvent::CloseRequested);
    EXPECT_EQ(EventFor(SDL_EVENT_WINDOW_CLOSE_REQUESTED), WindowEvent::CloseRequested);
    EXPECT_EQ(EventFor(SDL_EVENT_WINDOW_MINIMIZED), WindowEvent::Minimized);
    EXPECT_EQ(EventFor(SDL_EVENT_WINDOW_RESTORED), WindowEvent::Restored);
    EXPECT_EQ(EventFor(SDL_EVENT_WINDOW_OCCLUDED), WindowEvent::Occluded);
    EXPECT_EQ(EventFor(SDL_EVENT_WINDOW_EXPOSED), WindowEvent::Exposed);
    EXPECT_EQ(EventFor(SDL_EVENT_WINDOW_FOCUS_LOST), WindowEvent::FocusLost);
    EXPECT_EQ(EventFor(SDL_EVENT_WINDOW_FOCUS_GAINED), WindowEvent::FocusGained);
    EXPECT_EQ(EventFor(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED), WindowEvent::PixelSizeChanged);
}

TEST(WindowEventsTest, IgnoresTheOtherEvents)
{
    EXPECT_FALSE(EventFor(SDL_EVENT_KEY_DOWN).has_value());
    EXPECT_FALSE(EventFor(SDL_EVENT_MOUSE_MOTION).has_value());
    // RESIZED trae el tamaño en puntos; el viewport sigue a PIXEL_SIZE_CHANGED.
    EXPECT_FALSE(EventFor(SDL_EVENT_WINDOW_RESIZED).has_value());
}
} // namespace
