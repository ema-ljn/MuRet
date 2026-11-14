#include "Scenes/HandShine.h"

#include "Core/Expected.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x53d88d..0x53d89b: la luz de las dos.
constexpr glm::vec3 HandShineLight(1.0f, 0.5f, 0.2f);
// 0x53d879 y 0x53d8ab: la escala, un 0 entero que 0x53fe70 lee como float.
constexpr float HandShineScale = 0.0f;
// 0x53d81c y 0x53d8a8: el subtipo de cada una (el segundo argumento, 0, y ese más 2).
constexpr std::array<std::int32_t, 2> HandShineSubtypes{0, World::HandShineSecondSubtype};
constexpr std::size_t RightHand = 0;

// 0x4ec110(modelo, huesos propios + hueso · 0x30, (0, 0, 0), punto, 1) con la escala y la posición del personaje.
glm::vec3 BoneOrigin(const ActorState& actor, std::uint8_t bone)
{
    const Render::Models::BoneTransform matrix =
        bone < actor.bodyPose.size() ? actor.bodyPose[bone] : Render::Models::BoneTransform(0.0f);
    return Render::Models::OriginalTransformPosition(Render::Models::OriginalMatrixOf(matrix), glm::vec3(0.0f),
                                                     actor.scale, actor.position);
}
} // namespace

glm::vec3 HandShinePoint(std::span<const ActorState> actors, std::size_t hero, std::size_t owner, std::int32_t subtype)
{
    // 0x53fe20..0x53fe42: el idiom de and 0x80000001 sobre el subtipo, que es 0 o 2.
    const auto hand = static_cast<std::size_t>(subtype % 2);
    return BoneOrigin(actors[owner], actors[hero].hands[hand].bone);
}

std::function<std::optional<glm::vec3>(std::uint64_t, std::int32_t)>
ParticleActorHandsAmong(std::span<const ActorState> actors, std::size_t hero)
{
    return [actors, hero](std::uint64_t slot, std::int32_t subtype) -> std::optional<glm::vec3>
    {
        if (slot >= actors.size() || hero >= actors.size())
            return std::nullopt;
        return HandShinePoint(actors, hero, static_cast<std::size_t>(slot), subtype);
    };
}

Core::Expected<void, std::string> RaiseTheHandShine(std::span<const ActorState> actors, std::size_t hero,
                                                    std::size_t slot, World::Particles& particles,
                                                    Render::Models::CrtRandom& random)
{
    const ActorState& actor = actors[slot];
    const glm::vec3 point = BoneOrigin(actor, actor.hands[RightHand].bone);
    const World::ParticleOwner owner{World::ParticleOwnerKind::Actor, slot};
    for (const std::int32_t subtype : HandShineSubtypes)
    {
        // 0x53fe70 no lee el reloj ni la escena para la 5fd.
        const auto created = particles.CreateOwnedChecked(World::HandShineParticle, point, actor.angles, HandShineLight,
                                                          subtype, HandShineScale, 0.0f, random, 0, owner);
        if (!created.HasValue())
            return Core::Unexpected(std::format("0x53d8a2: la 5fd/{} falla con el error {}", subtype,
                                                static_cast<unsigned>(created.Error())));
        if (const std::optional<std::size_t> index = created.Value())
            particles.All()[*index].position = HandShinePoint(actors, hero, slot, subtype);
    }
    return {};
}
} // namespace Client::Scenes
