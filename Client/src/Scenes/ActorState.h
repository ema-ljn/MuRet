#pragma once

#include "GameLogic/CharacterAppearance.h"
#include "GameLogic/Path.h"
#include "Protocol/Character/CharacterList.h"
#include "Protocol/Character/Opcodes.h"
#include "Scenes/CharacterAnimation.h"
#include "Scenes/CharacterCloth.h"
#include "Scenes/CharacterHead.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/DarkHorse.h"
#include <optional>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace Client::Scenes
{
struct ActorEquipmentPart
{
    std::uint16_t model = 0;
    std::uint8_t level = 0;
    std::uint8_t flags1b = 0;
    std::uint8_t flags1c = 0;
};

// Registro lógico y recursos propios. 318 empieza en cero en la ventana nativa inicializada.
struct ActorState
{
    ActorState() = default;
    ActorState(const ActorState&) = delete;
    ActorState& operator=(const ActorState&) = delete;
    ActorState(ActorState&&) = default;
    ActorState& operator=(ActorState&&) = default;
    glm::vec3 position{};
    glm::vec3 angles{};
    std::uint32_t cellX = 0;
    std::uint32_t cellY = 0;
    GameLogic::Path path;
    bool moving = false;
    std::uint8_t movementDirection = 0;
    std::uint8_t movementIntent = 0;
    std::uint8_t interactionState = 0;
    std::uint8_t targetX = 0; // +39a, copia inicial y blanco recibido porD3.
    std::uint8_t targetY = 0; // +39b.
    std::uint8_t movementCooldown = 0;
    std::uint16_t baseSpeed = 0;
    std::uint8_t mountVariant = 0;
    float movementPhase = 0.0f;
    bool gateBlocked = false;
    // Entradas persistentes del modelo; no afirman profundidades nativas iniciales.
    std::uint8_t cellXDepth = 0;
    std::uint8_t cellYDepth = 0;
    std::int32_t logicStamp = 0;
    std::uint16_t type = 0;
    std::int32_t subtype = 0; // DWORD+4; input persistente del modelo, sin origen nativo supuesto.
    std::uint16_t state376 = 0;
    std::uint16_t key = 0;
    std::uint8_t pk = 0;
    std::uint8_t controlCode = 0;
    std::uint8_t run = 0;
    bool dying = false;
    std::uint8_t kind = 0;
    std::uint8_t safeZone = 0;
    bool hideBody = false;
    bool seen = false;
    std::uint32_t objectFlags = 0;
    std::uint32_t characterFlags = 0;
    float alpha = 0.0f;
    float targetAlpha = 0.0f;
    std::array<ActorEquipmentPart, 2> hands;
    std::array<ActorEquipmentPart, GameLogic::BodyPartCount> body;
    ActorEquipmentPart wings;
    ActorEquipmentPart pet;
    CharacterHead head;
    float headTargetZ = 0.0f; // +3c, escrito por MoveHero antes de sus dos blancos de cabeza.
    std::optional<DarkSpirit> spirit;
    std::optional<DarkHorse> horse;
    std::uint64_t revision = 0;
    std::uint64_t visualRevision = 0;
    // Caché legacy del aspecto; los lectores de comportamiento usan el WORD318 de pet.model.
    bool previousHad1c15 = false;
    bool active = false;
    std::array<std::byte, Protocol::Character::NameSize + 1> name{};
    std::uint16_t level = 0;
    // Fotografía de recepción para detectar cambios; name/level son los campos vivos del actor.
    Protocol::Character::CharacterEntry entry;
    GameLogic::CharacterAppearance appearance;
    CharacterAnimation animation;
    CharacterCloth cloth;
};
} // namespace Client::Scenes
