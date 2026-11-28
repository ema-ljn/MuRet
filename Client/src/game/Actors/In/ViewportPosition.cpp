#include "Actors/In/ViewportPosition.h"

#include "Kernel/ActorSearch.h"
#include "Actors/ViewportRecord.h"
#include "Audio/SoundId.h"
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ActorState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Protocol/Actors/Viewport.h"
#include "Particles/ParticleBurst.h"

#include <cstdint>
#include <format>
#include <string>

namespace Client::Actors::In
{
Core::Expected<void, std::string> ApplyViewportPosition(const Protocol::Actors::Position& position,
                                                        Kernel::WorldState& world, Crt::CrtRandom& random)
{
    Kernel::ActorState& actor = world.Actors()[Kernel::ActorWithKey(world, position.key)];
    constexpr std::uint16_t PushedType = 0xf1;
    if (actor.type == PushedType)
    {
        // 0x4c6b80..0x4c6ca3: +0xe4 at 20; the X read, (pkt[5] − X) · 400 at +0xd0; the Y read, (Y − pkt[6]) · 400 at
        // +0xcc (0x6b87b0); sound 0x1b and 0x51fe10 with the position.
        constexpr float PushSpeed = 20.0f;
        constexpr float PushScale = 400.0f;
        constexpr Audio::SoundId PushSound{0x1b};
        actor.floatE4 = PushSpeed;
        const std::uint8_t x = ReadCell(actor.cellX, actor.cellXAccesses, random);
        actor.floatD0 =
            static_cast<float>(static_cast<std::int32_t>(position.x) - static_cast<std::int32_t>(x)) * PushScale;
        const std::uint8_t y = ReadCell(actor.cellY, actor.cellYAccesses, random);
        actor.floatCC =
            static_cast<float>(static_cast<std::int32_t>(y) - static_cast<std::int32_t>(position.y)) * PushScale;
        // 0x41ed10(0x1b, 0, 0).
        world.Runtime().RequestSound(PushSound);
        if (auto burst = World::CreatePushBurst(world.Particles(), actor.position, random); !burst.HasValue())
            return Core::Unexpected(std::format("0x51fe9e: particle 0x5c5/2 of the DF push, error {}",
                                                static_cast<unsigned>(burst.Error())));
        return {};
    }
    // 0x4c6ca8..0x4c6dfe.
    WriteTheCells(actor, position.x, position.y, random);
    actor.byte399 = 1;
    return {};
}
} // namespace Client::Actors::In
