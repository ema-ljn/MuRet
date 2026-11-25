#pragma once

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Scenes/ActionSend.h"
#include "Scenes/SceneId.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Render::Models
{
struct Skeleton;
} // namespace Client::Render::Models

// Las acciones de un personaje: la de parado, 0x4f2250 (el SetPlayerStop de MuMain).
// Contrato: Client/docs/research/06-render-escenas-y-ui.md, "Caminar" (SetPlayerStop) y P-C14 c.
namespace Client::Scenes
{
struct ActorState;
struct WorldRuntime;

// Lo que 0x4f2250 lee y escribe fuera del personaje. Quien llama lo presta durante la llamada.
struct ActorStopContext
{
    // El mundo en curso (DAT_006c9dd0) y el latch de Fenrir (0x7d3a0d0, movementSpeed.itemLatch).
    WorldRuntime& runtime;
    // DAT_006d5dc8: los mapas de vuelo miran la celda solo en la escena 5 (0x4f23a2).
    SceneId scene;
    // Las acciones de Player.bmd: 0x4ea030 no pone una que el modelo no tiene.
    const Render::Models::Skeleton& playerSkeleton;
    // *[0x7758550], Item.bmd en memoria: la matriz de armas lee el byte +0x1e del registro del arma
    // (0x4f2c52). nullptr si no se cargó: la matriz corta con error al leerlo.
    const Data::Tables::ItemTable* items = nullptr;
    // El rand() de la CRT: el sufijo saca uno siempre, y Fenrir hasta dos más (0x4f36be..0x4f3731).
    Crt::CrtRandom& random;
    // 0x4f23c1: lee la celda visible en los mapas de vuelo de la escena 5.
    const Data::Terrain::TerrainLayer<std::uint16_t>* terrainWalls = nullptr;
    // El C1 18 de sentarse (0x4f2416..0x4f2bea). Vacío, sentarse corta con error después de poner la 0xca.
    ActionSender sendAction{};
    // El esqueleto de este actor; nullptr representa un registro de modelo sin acciones.
    const Render::Models::Skeleton* actorSkeleton = nullptr;
    // +0xaa del registro del modelo, los primeros sonidos que lee el sufijo para no jugadores y subtipos 209 a 211.
    std::span<const std::int16_t> modelSounds{};
};

// El ID emitido por 0x41ed10(id, personaje, 0) en el sufijo de 0x4f2250 (0x4f37a0).
// Queda disponible para el diagnóstico después de que lo despachó SetActorStop.
struct ActorStopResult
{
    std::optional<std::int32_t> sound;
};

// 0x4f2250: siempre pone Run en 0. El jugador elige su acción por mascota, zona segura, armas, alas y mapa; los
// otros leen la celda lógica Y y luego X, y el tipo 0x172 consulta TerrainWall para elegir 7 o 0. Después, cada ruta
// corre el sufijo de rand(). Sentado (+0x7c con 0x200000), el jugador pide la 0xca y el C1 05 18 con su dirección y la
// acción 0x82 (CL9 hito 1), o nada si ya la tiene; después corre el sufijo. Error si falta una dependencia consultada:
// TerrainWall para los casos que lo leen, sendAction al sentarse o la tabla de sonidos al abrirse el gate del sufijo.
[[nodiscard]] Core::Expected<ActorStopResult, std::string> SetActorStop(ActorState& actor,
                                                                        const ActorStopContext& context);
} // namespace Client::Scenes
