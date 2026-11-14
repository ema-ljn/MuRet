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
    // GetAsyncKeyState(0x11), que 0x558bb0 mira en 0x558e7f para atacar a otro jugador desde el clic (CL9 hito 3).
    bool controlDown = false;
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
    // El D7 de pegar al llegar (0x5650c0, CL9 hito 3): la clave del blanco, la acción y el rumbo. Vacío, no manda.
    std::function<Core::Expected<void, std::string>(std::uint16_t, std::uint8_t, std::uint8_t)> sendAttack{};
    // El C1 05 DF X Y del elfo que camina con un arma de tiro y hace clic en un atacable (0x59cf88, CL9 hito 3). Vacío,
    // no manda.
    std::function<Core::Expected<void, std::string>(std::uint8_t x, std::uint8_t y)> sendElfCell{};
    // El C1 18 de sentarse del héroe (0x4f2250, 0x4f2416..0x4f2bea). Vacío: sentarse corta con error.
    ActionSender sendAction{};
    // Vestir al héroe y el envío del 24, para pasar el arma de la izquierda a la derecha (0x59bd3e). nullptr: corta
    // donde los pide.
    const InventoryServices* inventory = nullptr;
    // 0x419900: el C1 03 31 al cerrar las misiones, que caminar cierra (0x56b397..). Sin él, cerrarlas corta.
    std::function<void()> sendWindowClose{};
    // La llegada a un NPC (0x5674c8, Scenes/NpcTalk.h): el C3 A0 sin la clase de las misiones (0x5676a6) y el C3 30
    // (0x567c2d..). Los dos rand() de cada C3 los saca el paso. Sin ellos, la llegada corta donde los manda.
    std::function<void()> requestQuestList{};
    std::function<void(std::int16_t)> sendTalk{};
    // Text.bmd: los textos de los avisos de la llegada a un NPC (el 663 del 0xee, 0x567618, y el 1830 del 0x101,
    // 0x567ad3). nullptr: esos avisos cortan.
    const Data::Text::TextTable* texts = nullptr;
    // 0x555190, para esos avisos: el medidor del texto y la pantalla. Sin medidor, cortan.
    const Render::Text::ITextMeasure* measure = nullptr;
    App::PixelSize screen{640, 480};
};
[[nodiscard]] Core::Expected<void, std::string> StepHeroMovement(const HeroMovementContext& context);
} // namespace Client::Scenes
