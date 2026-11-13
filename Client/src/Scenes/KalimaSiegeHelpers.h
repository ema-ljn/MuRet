#pragma once

#include "Core/Expected.h"
#include "Scenes/GameDrawTail.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Client::Data::Terrain
{
struct TerrainHeights;
}

namespace Client::Render
{
class ModelBatch;
namespace Models
{
class CrtRandom;
struct Skeleton;
} // namespace Models
} // namespace Client::Render

namespace Client::World
{
class FrameSprites;
class KalimaWorld;
class ModelEffects;
class Particles;
} // namespace Client::World

// Los helpers de efectos por tipo de la cadena de 0x510a1e (0x43c150 y 0x42dfe0) y el lazo de los huesos del 0x16e
// (0x5108b6), sobre el cuerpo ya dibujado de un monstruo (MonsterDrawing.h). Contrato: 06 § Los helpers de Kalima y del
// asedio (0x43c150 y 0x42dfe0). Los efectos con modelo (0x518cf0), los joints (0x530860) y las partículas
// (0x53fe70) que MuRet no tiene cortan con su dirección (CL11) si su tabla tiene lugar; llenas, el original no crea
// nada y sigue.
namespace Client::Scenes
{
struct ActorState;

struct KalimaSiegeHelperPass
{
    Render::ModelBatch& batch;
    // El personaje y el objeto (+0x8 y +0xc de 0x43c150): el mismo registro, o su copia entre frames del original.
    ActorState& actor;
    // El rand() de la CRT del dibujo (o su copia entre frames).
    Render::Models::CrtRandom& random;
    World::FrameSprites* sprites = nullptr;
    World::Particles* particles = nullptr;
    std::span<JointState> joints{};
    World::ModelEffects* effects = nullptr;
    // 0x723c48: sin él, 0x437860 no perturba nada.
    World::KalimaWorld* kalima = nullptr;
    // 0x7d325c8, el temblor de la cámara.
    float* cameraShake = nullptr;
    // Las alturas del .OZB de 0x5fb2f0, para los calcos de 0x42a1b0.
    const Data::Terrain::TerrainHeights* heights = nullptr;
    // 0x41ed10 (CL10): WorldRuntime::lastSoundId en el frame del original; entre frames, nullptr.
    std::optional<std::int32_t>* sound = nullptr;
    // 0x5767b88, en ms.
    float worldClock = 0.0f;
    std::int32_t map = 0;
    // 0x4294b0 (0x7239ec).
    bool siegeActive = false;
};

// 0x510a1e..0x510a36: 0x43c150 y, si no es suyo, 0x42dfe0. Devuelve true si uno de los dos tiene el tipo (+0x2), que
// salta el resto de la cadena. Sin la tabla de sprites corta solo un tipo que la usa.
//
// 0x43c150, por el tipo:
// - 0xc5 (la puerta de Kalima): con la acción 1, si +0x60 está en 0, la partícula 0x6ae (CL11) del hueso 8 (el punto
//   pisa la posición 20 más arriba que el original arma antes); +0x60 cuenta de 0 a 4.
// - 0x191: fuera de la acción 6, de los huesos 74..78 y 62..66 dos 0x5ac cada uno con un rand() & 1 para la partícula
//   0x607/1 (CL11), y los 0x5ca y 0x5fd de los huesos 8, 22 y 15 con +0x68; siempre, los 0x5ac de los huesos 51..55 y
//   29..33.
// - 0x192 (Kundun): en la acción 6 suelta la mano izquierda; si no, los 0x5ca de los huesos 8 y 9, el 0x62b (y en la
//   acción 5 el 0x62d con el pulso de +0x8a y +0x89) del hueso 100 y un rand() & 1 para la partícula 0x5f2/13 (CL11).
//   En la acción 5, con el frame debajo de 2, el sonido 0x1db (CL10); pasado el 4, en Kalima, 0x437860 en su celda;
//   pasado el 3, el temblor con rand() & 7; en Kalima entre el 3 y el 14, dos efectos 9 (0x518cf0) con rand() % 360 y
//   rand() % 600. Las acciones 3 y 4 sacan el efecto 0x128 y los joints 0x6ee/14 (CL11), 0x437860 y el temblor con
//   rand() & 15.
// - 0x193, 0x194, 0x195, 0x196 y 0x197: sus sprites (el 0x194 con dos rand() % 360); el 0x196 saca la neblina 0x5ac/0
//   del hueso 60 y, en las acciones 0 y 1, rand() % 50 por hueso; los joints 0x614/7 y 0x610/5 y las partículas
//   0x5f2/13, 0x5da/1 y 0x5dc/12 cortan (CL11); el 0x195 y el 0x197 sueltan las dos manos en la acción 6.
// 0x42dfe0, solo en el mapa 30: el 0x19c, el 0x1a1, el 0x1a8 y el 0x1fd, con sus sprites, +0x68, +0x42c, +0x58 y
// los calcos de 0x42a1b0, que en el modo 0x80 pisan la luz que reciben (+0xf4 del 0x19c sin el asedio y del 0x1a1
// con él, Q-C876).
[[nodiscard]] Core::Expected<bool, std::string> DrawKalimaSiegeHelpers(const KalimaSiegeHelperPass& pass);

// 0x5108b6..0x510a1c, el 0x16e: por cada hueso que no es dummy, fuera de los 15..20 y 27..32, el 0x5fd de escala 2,5
// aditivo (con +0x1d6 en 0) o el 0x61e de 0,8 verde, y rand() & 3 en 0 en las acciones 3 y 4 para la partícula 0x5ca
// con la misma luz. Después salta la cadena de helpers.
[[nodiscard]] Core::Expected<void, std::string> DrawTheBoneSparks(const KalimaSiegeHelperPass& pass,
                                                                  const Render::Models::Skeleton& skeleton);

// 0x42a1b0(0x61e, modo, x, y, lado, lado, luz): los dos calcos girados de 0x5fb2f0, con 0x620b10 en el modo 0x80 (la
// luz pasa a v y después a v · (1 − v)) y con 0x620a90 en los demás (la luz por v), v = sin(T · 0,0015f) · 0,3f + 0,7f.
[[nodiscard]] Core::Expected<void, std::string> DrawCastleGlow(const KalimaSiegeHelperPass& pass, std::int32_t mode,
                                                               float x, float y, float side, glm::vec3& light);
} // namespace Client::Scenes
