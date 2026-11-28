#include "Effects/HandShine.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Effects
{
namespace
{
// 0x53d88d..0x53d89b.
constexpr glm::vec3 HandShineLight(1.0f, 0.5f, 0.2f);
// 0x53d879 and 0x53d8ab: an integer 0 that 0x53fe70 reads as a float.
constexpr float HandShineScale = 0.0f;
// 0x53d81c and 0x53d8a8: each one's subtype (the second argument, 0, and that plus 2).
constexpr std::array<std::int32_t, 2> HandShineSubtypes{0, Particles::HandShineSecondSubtype};
constexpr std::size_t RightHand = 0;

// 0x4ec110(model, own bones + bone · 0x30, (0, 0, 0), point, 1) with the character's scale and position.
glm::vec3 BoneOrigin(const Kernel::ActorState& actor, std::uint8_t bone)
{
    const Math::BoneTransform matrix = bone < actor.bodyPose.size() ? actor.bodyPose[bone] : Math::BoneTransform(0.0f);
    return Math::OriginalTransformPosition(Math::OriginalMatrixOf(matrix), glm::vec3(0.0f), actor.scale,
                                           actor.position);
}
} // namespace

glm::vec3 HandShinePoint(std::span<const Kernel::ActorState> actors, std::size_t hero, std::size_t owner,
                         std::int32_t subtype)
{
    // 0x53fe20..0x53fe42: the and 0x80000001 idiom on the subtype, which is 0 or 2.
    const auto hand = static_cast<std::size_t>(subtype % 2);
    return BoneOrigin(actors[owner], actors[hero].hands[hand].bone);
}

std::function<std::optional<glm::vec3>(std::uint64_t, std::int32_t)>
ParticleActorHandsAmong(std::span<const Kernel::ActorState> actors, std::size_t hero)
{
    return [actors, hero](std::uint64_t slot, std::int32_t subtype) -> std::optional<glm::vec3>
    {
        if (slot >= actors.size() || hero >= actors.size())
            return std::nullopt;
        return HandShinePoint(actors, hero, static_cast<std::size_t>(slot), subtype);
    };
}

Core::Expected<void, std::string> RaiseTheHandShine(std::span<const Kernel::ActorState> actors, std::size_t hero,
                                                    std::size_t slot, Particles::Particles& particles,
                                                    Crt::CrtRandom& random)
{
    const Kernel::ActorState& actor = actors[slot];
    const glm::vec3 point = BoneOrigin(actor, actor.hands[RightHand].bone);
    const Particles::ParticleOwner owner{Particles::ParticleOwnerKind::Actor, slot};
    for (const std::int32_t subtype : HandShineSubtypes)
    {
        // 0x53fe70 reads neither the clock nor the scene for the 0x5fd.
        const auto created =
            particles.CreateOwnedChecked(Particles::HandShineParticle, point, actor.angles, HandShineLight, subtype,
                                         HandShineScale, 0.0f, random, 0, owner);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x53d8a2: la 5fd/{} falla con el error {}", subtype,
                                                static_cast<unsigned>(created.Error())));
        if (const std::optional<std::size_t> index = created.Value())
            particles.All()[*index].position = HandShinePoint(actors, hero, slot, subtype);
    }
    return {};
}
} // namespace Client::Effects
