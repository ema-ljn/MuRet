#include "Platform/WindowEvents.h"

#include "App/WindowState.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <gtest/gtest.h>

#include <optional>

// Client::Platform::WindowEventFor: la tabla de los SDL_EVENT_* a los WindowEvent, con eventos
// armados a mano (sin SDL_Init).
namespace
{
using Client::App::WindowEvent;
using Client::Platform::WindowEventFor;

// Un id cualquiera de "nuestra" ventana y otro de una ventana ajena.
constexpr SDL_WindowID OurWindow = 3;
constexpr SDL_WindowID OtherWindow = 7;

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

TEST(WindowEventsTest, IgnoresTheEventsOfAnotherWindow)
{
    SDL_Event close{};
    close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    close.window.windowID = OtherWindow;
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;

    EXPECT_FALSE(WindowEventFor(close, OurWindow).has_value());
    EXPECT_EQ(WindowEventFor(close, OtherWindow), WindowEvent::CloseRequested);
    // SDL_EVENT_QUIT no tiene ventana: cierra siempre.
    EXPECT_EQ(WindowEventFor(quit, OurWindow), WindowEvent::CloseRequested);
}
} // namespace
