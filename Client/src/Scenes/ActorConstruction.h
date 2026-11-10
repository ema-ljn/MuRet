#pragma once

#include "Core/Expected.h"
#include "World/TerrainCulling.h"

#include <cstdint>
#include <string>

namespace Client::Render::Models
{
struct ClothWorld;
}

namespace Client::World
{
struct WorldTerrain;
}

// El constructor de personaje, 0x511290. Contrato: Client/docs/research/06-render-escenas-y-ui.md,
// "El héroe" y "Los personajes de la lista".
namespace Client::Scenes
{
struct ActorState;

// Lo que 0x511290 lee fuera del personaje para la altura. Quien llama lo presta durante la llamada.
struct ActorConstructionContext
{
    // Las tablas del mundo cargado: los atributos y las alturas.
    const World::WorldTerrain& terrain;
    // DAT_006c9dd0, el mundo en curso: con −1 no suma el corrimiento de la mascota.
    std::int32_t map;
    // DAT_006d5dc8: 0x5f93c0 no mira las tablas en las escenas 0, 1 y 3.
    World::CullingScene scene;
    // 0x6c9d5c, la altura fija de la cámara (World::GameCamera::fixedHeight).
    float fixedHeight;
};

// Dónde va el personaje: la celda del servidor y el ángulo de +0x24.
struct ActorPlacement
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    float angle = 0.0f;
};

// 0x5f93c0 sale con 0 antes de leer las tablas con estos valores nativos de escena.
[[nodiscard]] bool HeightSkippedIn(World::CullingScene scene);

// La Z de 0x511290 en (x, y): la altura del terreno (0x5f93c0) y, con el word318 de antes en 0x1c15
// (`previousHad1c15`), un mundo y fuera de la zona segura, el corrimiento de la mascota: 90 en los
// mapas 8 y 10 y 30 en los demás. La suma queda en la pila: quien la guarda la pasa a float.
[[nodiscard]] double ConstructionHeight(const ActorConstructionContext& context, float x, float y, bool previousHad1c15,
                                        bool safe);

// 0x511290(personaje, 0x20a, X, Y, ángulo): vivo, la celda en sus dos copias, la zona segura del .att
// de la celda, la posición en el centro de la celda con la Z de ConstructionHeight, el ángulo, el
// estado de partida (con +0x358 en 0) y las partes del equipo sin ítem. Cada acceso de la tabla de protección (las dos
// escrituras de la celda, sus cuatro lecturas y la del contador de pasos) se abre y se cierra, y gasta
// 4 rand() de `cloth` si queda sin accesos: 28 desde cero. No toca la clave (+0x1f4). Otro tipo que el
// 0x20a corta con error sin tocar al personaje: sus casos no están leídos.
[[nodiscard]] Core::Expected<void, std::string> ConstructActor(ActorState& actor, std::uint16_t type,
                                                               const ActorPlacement& placement,
                                                               const ActorConstructionContext& context,
                                                               Render::Models::ClothWorld& cloth);
} // namespace Client::Scenes
