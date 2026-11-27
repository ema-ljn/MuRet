#pragma once

#include "Core/Expected.h"
#include "Kernel/PlayerKey.h"
#include "Network/Packet.h"
#include "Scenes/WorldState.h"
#include "Session/ServerSession.h"
#include "Session/WorldEntry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>

// A row's handler that reads its packet with `Read` and calls `Handle` with the value and the parts of the world its
// other parameters name, by type. `Handle` knows neither the session nor the packet.
namespace Client::Session
{
// The start of the warning for a packet that cannot be read ("se ignora el movimiento (D3)"), as a template argument.
template <std::size_t Size> struct Ignored
{
    // Implicit, so the row writes the literal.
    constexpr Ignored(const char (&text)[Size])
    {
        std::copy_n(text, Size, value.begin());
    }

    [[nodiscard]] constexpr std::string_view View() const
    {
        return {value.data(), Size - 1};
    }

    std::array<char, Size> value{};
};

namespace Detail
{
template <typename Part> struct RoutedPart;

// The whole bag, until AR4 splits it into the modules' states.
template <> struct RoutedPart<const Scenes::WorldEntryResources&>
{
    static const Scenes::WorldEntryResources& Of(const Scenes::WorldEntryResources& world, const App::ServerSession&)
    {
        return world;
    }
};

template <> struct RoutedPart<Scenes::WorldState&>
{
    static Scenes::WorldState& Of(const Scenes::WorldEntryResources& world, const App::ServerSession&)
    {
        return world.world;
    }
};

template <> struct RoutedPart<Kernel::PlayerKey>
{
    static Kernel::PlayerKey Of(const Scenes::WorldEntryResources&, const App::ServerSession& session)
    {
        return Kernel::PlayerKey{session.PlayerIndex()};
    }
};

template <typename Handle> struct RoutedHandler;

template <typename Result, typename Received, typename... Parts>
struct RoutedHandler<Result (*)(const Received&, Parts...)>
{
    static_assert(std::is_void_v<Result> || std::is_same_v<Result, Core::Expected<void, std::string>>,
                  "a routed handler returns nothing or Core::Expected<void, std::string>");

    template <auto Handle>
    static Core::Expected<void, std::string> Call(const Received& received, const Scenes::WorldEntryResources& world,
                                                  const App::ServerSession& session)
    {
        if constexpr (std::is_void_v<Result>)
        {
            Handle(received, RoutedPart<Parts>::Of(world, session)...);
            return {};
        }
        else
        {
            return Handle(received, RoutedPart<Parts>::Of(world, session)...);
        }
    }
};
} // namespace Detail

// Without a world (a session whose sink applies nothing), the packet is read and dropped.
template <Ignored What, auto Read, auto Handle> void Route(App::ServerSession& session, const Network::Packet& packet)
{
    const auto received = Read(packet);
    if (!received.HasValue())
    {
        session.IgnoreUnreadable(What.View());
        return;
    }
    const Scenes::WorldEntryResources* const world = session.RoutedWorld();
    if (world == nullptr)
        return;
    session.FinishRoutedReception(
        Detail::RoutedHandler<decltype(Handle)>::template Call<Handle>(received.Value(), *world, session));
}
} // namespace Client::Session
