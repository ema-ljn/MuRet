#pragma once

#include "Core/Expected.h"
#include "Kernel/PlayerKey.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Scenes/ActionSend.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/ChatReceive.h"
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
// other parameters name, by type. `Handle` knows neither the session nor the packet, except the session's own handlers
// (Session/SessionHandlers.h).
namespace Client::Session
{
// The start of a warning ("se ignora el movimiento (D3)"), as a template argument.
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

// A case that does not look at the encryption flag.
struct AnyEncryption
{
    static bool Reported(App::ServerSession&, const Network::Packet&)
    {
        return false;
    }
};

// A case that looks at the flag before reading (0x4c6e32, 0x4cf200, 0x4cfce0): in clear it logs `Warning` and sends
// Report()'s F1 03, and the packet is not read.
template <Ignored Warning, auto Report> struct InClear
{
    static bool Reported(App::ServerSession& session, const Network::Packet& packet)
    {
        if (packet.WasDecrypted())
            return false;
        session.ReportInClear(Warning.View(), Report());
        return true;
    }
};

// The packet Write(Arguments...) builds, as InClear's Report.
template <auto Write, auto... Arguments> Protocol::Outgoing Written()
{
    return Write(Arguments...);
}

namespace Detail
{
template <typename Part> struct RoutedPart;

// The whole bag, until AR4 splits it into the modules' states.
template <> struct RoutedPart<const Scenes::WorldEntryResources&>
{
    static const Scenes::WorldEntryResources& Of(const Scenes::WorldEntryResources& world, App::ServerSession&)
    {
        return world;
    }
};

template <> struct RoutedPart<Scenes::WorldState&>
{
    static Scenes::WorldState& Of(const Scenes::WorldEntryResources& world, App::ServerSession&)
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

// The chat's context, with the session's chosen character, chat rooms and GameServer for the guild's window.
template <> struct RoutedPart<const Scenes::ChatReceiveContext&>
{
    static Scenes::ChatReceiveContext Of(const Scenes::WorldEntryResources& world, App::ServerSession& session);
};

template <> struct RoutedPart<const Scenes::ActorCombatContext&>
{
    static Scenes::ActorCombatContext Of(const Scenes::WorldEntryResources& world, App::ServerSession&);
};

// The C1 18 through the session's GameServer (App::ServerSession::SendAction).
template <> struct RoutedPart<const Scenes::ActionSender&>
{
    static Scenes::ActionSender Of(const Scenes::WorldEntryResources&, App::ServerSession& session);
};

// Only the session's own handlers name it.
template <> struct RoutedPart<App::ServerSession&>
{
    static App::ServerSession& Of(const Scenes::WorldEntryResources&, App::ServerSession& session)
    {
        return session;
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
                                                  App::ServerSession& session)
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
template <Ignored What, auto Read, auto Handle, typename Encryption = AnyEncryption>
void Route(App::ServerSession& session, const Network::Packet& packet)
{
    if (Encryption::Reported(session, packet))
        return;
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
