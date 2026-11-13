#pragma once

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/GateTable.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Text/TextTable.h"
#include "GameLogic/PathCell.h"
#include "Protocol/Movement.h"
#include "Scenes/ActionSend.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
struct Skeleton;
} // namespace Client::Render::Models

namespace Client::Render::Text
{
class ITextMeasure;
} // namespace Client::Render::Text

namespace Client::Scenes
{
class WorldState;
struct InventoryServices;
struct HeroMovementInput
{
    std::int32_t mouseX = 0;
    std::int32_t mouseY = 0;
    bool leftPressed = false;
    bool leftDown = false;
    bool shiftDown = false;
    float worldTime = 0.0f;
    std::uint32_t ticksMs = 0;
};
struct HeroMovementContext
{
    WorldState& world;
    Render::Models::CrtRandom& random;
    const Render::Models::Skeleton* playerSkeleton = nullptr;
    const Data::Tables::ItemTable* items = nullptr;
    const Data::Tables::SkillTable* skills = nullptr;
    const Data::Tables::GateTable* gates = nullptr;
    HeroMovementInput input{};
    // El servicio usa el rayo, rango y trapecio del dibujo anterior.
    std::function<Core::Expected<std::optional<GameLogic::PathCell>, std::string>()> pickTarget{};
    std::function<Core::Expected<void, std::string>(std::span<const Protocol::Movement::Cell>, Protocol::Movement::Cell,
                                                    float)>
        sendWalk{};
    std::function<void(std::uint8_t)> requestGate{};
    float fixedHeight = 1200.0f;
    std::function<Core::Expected<void, std::string>(std::uint8_t)> sendFacing{};
    // CL9 hito 11, SendMove con 0x78abb00 (0x56b4db..0x56b78f): el C1 03 97 en claro (sin el servicio, corta), y
    // SetCursorPos ([0x6b84f4]) con el lugar que calcula el original en la pantalla de `screen` ([0x6d5d60] y
    // [0x6d5d64]), que queda pedido en GameMovementState::cursorWarpRequest.
    std::function<Core::Expected<void, std::string>()> sendEventWindowLeave{};
    // El C1 18 de sentarse del héroe (0x4f2250, 0x4f2416..0x4f2bea). Vacío: sentarse corta con error.
    ActionSender sendAction{};
    // Vestir al héroe y el envío del 24, para pasar el arma de la izquierda a la derecha (0x59bd3e). nullptr: corta
    // donde los pide.
    const InventoryServices* inventory = nullptr;
    // 0x419900: el C1 03 31 al cerrar las misiones, que caminar cierra (0x56b397..), y el de cerrar el NPC de evento
    // (0x56b4d6 → 0x65cae0(0x1000), 0x658a18; CL9 hito 11). Sin él, cerrarlos corta.
    std::function<void()> sendWindowClose{};
    // La llegada a un NPC (0x5674c8, Scenes/NpcTalk.h): el C3 A0 sin la clase de las misiones (0x5676a6) y el C3 30
    // (0x567c2d..). Los dos rand() de cada C3 los saca el paso. Sin ellos, la llegada corta donde los manda.
    std::function<void()> requestQuestList{};
    std::function<void(std::int16_t)> sendTalk{};
    // Text.bmd: los textos de los avisos de la llegada a un NPC (el 663 del 0xee, 0x567618, y el 1830 del 0x101,
    // 0x567ad3). nullptr: esos avisos cortan.
    const Data::Text::TextTable* texts = nullptr;
    // 0x555190, para esos avisos: el medidor del texto y la pantalla. Sin medidor, cortan. La pantalla es también la
    // del SetCursorPos de SendMove con 0x78abb00 (CL9 hito 11).
    const Render::Text::ITextMeasure* measure = nullptr;
    App::PixelSize screen{640, 480};
};
[[nodiscard]] Core::Expected<void, std::string> StepHeroMovement(const HeroMovementContext& context);
} // namespace Client::Scenes
