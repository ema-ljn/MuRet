#pragma once

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Scenes/SceneId.h"

#include <cstdint>
#include <optional>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
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
    Render::Models::CrtRandom& random;
};

// Lo que 0x4f2250 pide afuera: el sonido de Fenrir del sufijo, 0x41ed10(id, personaje, 0) en 0x4f37a0.
// MuRet todavía no tiene audio (CL10): quien llama lo recibe y decide.
struct ActorStopResult
{
    std::optional<std::int32_t> sound;
};

// 0x4f2250: Run en 0 y la acción de parado con 0x4ea030, según la mascota, la zona segura, las armas,
// las alas y el mapa; después el sufijo de rand(). Lo que no está leído o MuRet no tiene corta con
// error, después de lo que el original hace antes: un personaje que no es jugador, la celda de los
// mapas de vuelo en la escena 5, sentarse (el C1 .. 18) y la tabla de sonidos de los subtipos 209 a
// 211.
[[nodiscard]] Core::Expected<ActorStopResult, std::string> SetActorStop(ActorState& actor,
                                                                        const ActorStopContext& context);
} // namespace Client::Scenes
