#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/SkillTable.h"
#include "Protocol/Outgoing.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
struct Skeleton;
} // namespace Client::Render::Models

// CL9 hito 4, fase 2b: usar una skill desde el juego (0x57cfc0). Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "Usar una skill". MoveHero la llama al final del clic (0x59e255),
// salvo cuando el clic de ataque sale sin flechas (MoveHeroEnd::Leave, 0x59d666).
namespace Client::Scenes
{
class WorldState;
struct ActorState;
struct InventoryServices;

struct HeroSkillContext
{
    WorldState& world;
    Render::Models::CrtRandom& random;
    // Skill.bmd (0x76bd1d8). nullptr: 0x5526f0 corta.
    const Data::Tables::SkillTable* skills = nullptr;
    // Las acciones de Player.bmd, que 0x4ea030 pone a las skills de un jugador, e Item.bmd, que lee 0x4f4a10 con otro
    // (CL9 hito 4, fase 2c). nullptr: la que lo pide corta.
    const Render::Models::Skeleton* playerSkeleton = nullptr;
    const Data::Tables::ItemTable* items = nullptr;
    // GetTickCount (0x6b8144), que leen el freno de 300 ms de los envíos (0x576791c, PassesTheActionBrake). Vacío: el
    // envío que lo mira corta.
    std::function<std::uint32_t()> tickCount{};
    // Lo que 0x561c50 (CheckTheArrows) pide para equipar las flechas del inventario. nullptr: sin servicios.
    const InventoryServices* inventory = nullptr;
    // [0x5767b88], el reloj del mundo en ms.
    float worldTime = 0.0f;
    // GetAsyncKeyState(0x11), que 0x558bb0 mira para atacar a otro jugador.
    bool controlDown = false;
    // 0x57ceea: sin elegido, 0x5fc260(1) y 0x5fac90 sobre el mouse; el punto del terreno (0x7d366e0..0x7d366e8) si
    // acierta. Vacío: ese camino corta.
    std::function<Core::Expected<std::optional<glm::vec3>, std::string>()> pickTerrainPoint{};
    // Lo que las skills mandan por send() (el XOR32 en línea; los C3 con su relleno y su serial ya sacados). Vacío, sin
    // la sesión: no sale, como el original con el socket en −1.
    std::function<void(Protocol::Outgoing)> send{};
};

// 0x57cfc0(héroe): con el modo del editor (0x77f3b18), el mouse de la UI (0x775857c) o 0x1000 en +0x78, nada. Si no:
// el desvanecido de +0x80; la skill del lugar +0x425 (+0x6c del registro propio) y su alcance (0x5526f0); el derecho
// (0x7d36880, 0x7d3685c, el soltado 0x7d36840 y los frames 0x7d368a4, con la 0x28 que se carga como 0x3a) o el ataque
// automático con 0x6c8e38 en 2 y un elegido (0x57d1d9) prenden el uso, y el reloj de 0x77f3b4c/0x77f3b50 lo corta a
// los 3.600 s (con el movimiento trabado, 0x77f3bac, el clic izquierdo). Después: 0x6c8e50 en −1, las órdenes del
// cuervo (0x420f70), las skills del asedio (0x5a5540), Skill.bmd +0x31 en 2 con 0x200, +0x389, la zona segura (solo
// en Blood Castle y Chaos Castle, y las skills de la lista), 0x6c8e38 por la clase, la acción del héroe, y, salvo el
// Wizard, el blanco (0x57ce80), la línea de vista (0x558800) y la skill de cada mano (0x559890 o 0x561f60). Las ramas
// por clase (0x57d790, 0x581894, 0x5897b7) y las skills 0x43..0x48 (0x5808da) cortan con su dirección.
[[nodiscard]] Core::Expected<void, std::string> UseTheHeroSkill(ActorState& hero, const HeroSkillContext& context);
} // namespace Client::Scenes
