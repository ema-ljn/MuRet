#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Client::Render::Models
{
struct ClothWorld;
}

// El alta y la baja de un personaje del pool del mundo por su clave (CL9 hito 1): 0x512870 y 0x510ff0. Las llaman el
// 0x12 y el 0x14 (ViewportPlayers.h) y, desde la hito 2, el alta de los monstruos (0x5141e0). Contrato:
// Client/docs/research/06-render-escenas-y-ui.md, "Los otros jugadores en el viewport". *Estático*.
namespace Client::Scenes
{
class WorldState;
struct WorldRuntime;
struct ActorConstructionContext;
struct ActorPlacement;

// 0x512870 y 0x510ff0 recorren 400 registros, uno menos que el pool: el 400 es el "ninguno".
inline constexpr std::size_t CreatedActorSearch = 400;

// 0x512870(clave, tipo, X, Y, ángulo). Primero busca un vivo con la clave (MOVSX de +0x1f4 contra el int): lo
// construye otra vez (0x511290) sin tocar la clave y pone +0x78 y +0x7c en 0. Si no hay, el primero sin el byte vivo:
// suelta el cuervo (0x421d60) y la capa (0x414340), lo construye, le pone la clave (el WORD de abajo) y las dos
// banderas en 0. Con los 400 vivos devuelve el 400 sin construir ni tocar nada. 0x401040 saca los huesos con nombre
// del registro (ForgetNamedBones); 0x4fb1a0 (las telas) no tiene nada que soltar en el mundo de MuRet: las telas de los
// personajes del juego viven en la escena (GameScene), que el handler no ve (08, P-C161). Devuelve el lugar; el error
// de 0x511290 (otro tipo) corta con lo de antes hecho.
[[nodiscard]] Core::Expected<std::size_t, std::string> CreateActor(WorldState& world, std::int32_t key,
                                                                   std::uint16_t type, const ActorPlacement& placement,
                                                                   const ActorConstructionContext& context,
                                                                   Render::Models::ClothWorld& cloth);

// 0x510ff0(clave): el primero vivo con la clave (MOVSX) queda muerto: +0 en 0, 0x401040, apaga los ayudantes que lo
// llevan (0x511031..0x51104b, como 0x443d40), suelta el cuervo (0x421d60), las telas (0x4fb1a0, lo de arriba) y la
// capa (0x414340). Sin ninguno, nada.
void RemoveActor(WorldState& world, std::int32_t key);

// 0x401040 → 0x401410: saca de [0x6e5640] los huesos con nombre del personaje `actor`.
void ForgetNamedBones(WorldRuntime& runtime, std::size_t actor);

// 0x401000 → 0x4011c0: el hueso `bone` del modelo de `actor` con el nombre `name`; saca antes el del mismo personaje y
// nombre (0x4016c0 y 0x401350).
void NameABone(WorldRuntime& runtime, std::size_t actor, std::string_view name, std::uint32_t bone);
} // namespace Client::Scenes
